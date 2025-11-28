#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h>

// ------------------------------------------------------------------------
// Macros & Helpers
// ------------------------------------------------------------------------
#define MARK_BIT 1
#define IS_MARKED(p)      ((uintptr_t)(p) & MARK_BIT)
#define GET_UNMARKED(p)   ((Node*)((uintptr_t)(p) & ~MARK_BIT))
#define GET_MARKED(p)     ((Node*)((uintptr_t)(p) | MARK_BIT))

// Architecture-specific backoff to prevent Livelock/Bus Contention
static inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    sched_yield();
#endif
}

// ------------------------------------------------------------------------
// Core Logic
// ------------------------------------------------------------------------

SkipList* skiplist_create_lockfree(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
    }
    
    atomic_init(&list->size, 0);
    list->maxLevel = MAX_LEVEL;
    
    return list;
}

/**
 * Find:
 * - Traverses and prunes marked nodes (Helping).
 * - Returns preds and succs for the key.
 * - Uses Backoff on CAS failure to prevent Livelock.
 */
static bool find(SkipList* list, int key, Node** preds, Node** succs) {
    int bottomLevel = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= bottomLevel; level--) {
            Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
            
            while (true) {
                // Safety: Sentinel Tail check
                if (curr == NULL) break; 

                Node* succ = atomic_load(&curr->next[level]);
                
                // 1. Helping: Check if current node is marked
                while (IS_MARKED(succ)) {
                    // It is marked. Try to physically unlink it.
                    Node* unmarked_succ = GET_UNMARKED(succ);
                    
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                        // CAS failed: pred changed or was marked. 
                        // Backoff and restart search.
                        cpu_relax();
                        goto retry;
                    }
                    
                    // CAS success: curr is physically removed at this level.
                    // Advance to next node.
                    curr = unmarked_succ;
                    if (curr == NULL) break;
                    succ = atomic_load(&curr->next[level]);
                }
                
                if (curr == NULL) break;
                
                // 2. Traversal
                if (curr != list->tail && curr->key < key) {
                    pred = curr;
                    curr = GET_UNMARKED(succ);
                } else {
                    break; // Found position
                }
            }
            
            preds[level] = pred;
            succs[level] = curr;
        }
        
        return (succs[bottomLevel] != list->tail && succs[bottomLevel]->key == key);
    }
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        // 1. Search
        if (find(list, key, preds, succs)) {
            return false; // Key exists
        }
        
        // 2. Create Node
        int topLevel = random_level(); 
        Node* newNode = create_node(key, value, topLevel);
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // 3. Link Level 0 (Linearization Point)
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            // Failed. Someone else changed level 0.
            // Cleanup and Retry.
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            cpu_relax(); // Backoff
            continue; 
        }
        
        atomic_fetch_add(&list->size, 1);
        
        // 4. Build Tower Upwards
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                // Try to link
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success
                }
                
                // Failed: pred changed. Re-find.
                find(list, key, preds, succs);
                
                // Check if we are being deleted concurrently
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    return true; // We are inserted, but effectively dead. Stop.
                }
                
                // Reset next pointer and retry
                atomic_store(&newNode->next[i], succs[i]);
                cpu_relax(); // Backoff
            }
        }
        
        atomic_store(&newNode->fully_linked, true);
        return true;
    }
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* victim;
    
    while (true) {
        // 1. Search
        if (!find(list, key, preds, succs)) {
            return false; // Not found
        }
        
        victim = succs[0];
        
        // 2. Logical Deletion (Marking)
        // We mark top-down. If we fail at level 0, we must retry the whole delete.
        // Marking upper levels is an optimization for 'find' performance (helping).
        
        for (int i = victim->topLevel; i >= 0; i--) {
            Node* succ = atomic_load(&victim->next[i]);
            
            if (IS_MARKED(succ)) {
                if (i == 0) return false; // Already deleted by someone else
                continue;
            }
            
            Node* marked_succ = GET_MARKED(succ);
            if (!atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                // If we failed at Level 0, the node state changed fundamentally.
                if (i == 0) {
                    cpu_relax(); // Backoff
                    goto retry_delete; // Restart search
                }
                // If we failed at upper levels, it's okay, we can proceed to 0.
                // (Assuming search logic handles partially marked nodes correctly)
            }
        }
        
        // 3. Physical Unlinking
        // find() handles this.
        find(list, key, preds, succs);
        
        atomic_fetch_sub(&list->size, 1);
        // Do not free() to prevent UAF
        return true;
        
        retry_delete:;
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (true) {
            if (curr == NULL || curr == list->tail) break;
            
            Node* succ = atomic_load(&curr->next[level]);
            
            // Skip marked nodes
            while (IS_MARKED(succ)) {
                curr = GET_UNMARKED(succ);
                if (curr == NULL || curr == list->tail) goto next_level;
                succ = atomic_load(&curr->next[level]);
            }
            
            if (curr->key < key) {
                pred = curr;
                curr = GET_UNMARKED(succ);
            } else {
                break;
            }
        }
        next_level:;
    }
    
    Node* curr = GET_UNMARKED(atomic_load(&pred->next[0]));
    
    return (curr != list->tail && 
            curr->key == key && 
            !IS_MARKED(atomic_load(&curr->next[0])));
}

void skiplist_destroy_lockfree(SkipList* list) {
    // Single threaded destroy
    Node* curr = list->head;
    while (curr) {
        Node* next = GET_UNMARKED(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}