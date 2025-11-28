#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h>
#include <time.h>

// ------------------------------------------------------------------------
// Configuration & Macros
// ------------------------------------------------------------------------
#define MARK_BIT 1
#define IS_MARKED(p)      ((uintptr_t)(p) & MARK_BIT)
#define GET_UNMARKED(p)   ((Node*)((uintptr_t)(p) & ~MARK_BIT))
#define GET_MARKED(p)     ((Node*)((uintptr_t)(p) | MARK_BIT))

// Tune these for your specific hardware if needed
#define BACKOFF_BASE_SPINS 4
#define BACKOFF_MAX_SPINS  1024
#define TOWER_BUILD_RETRIES 50

/**
 * Exponential Backoff Helper
 * 
 * When a CAS fails, we call this. It spins for a short duration
 * to reduce bus contention, allowing other threads to finish.
 */
static void backoff(int *attempt) {
    int spins = BACKOFF_BASE_SPINS << *attempt;
    if (spins > BACKOFF_MAX_SPINS) spins = BACKOFF_MAX_SPINS;
    
    for (volatile int i = 0; i < spins; i++) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        __asm__ __volatile__("yield");
#else
        // Generic compiler barrier
        __asm__ __volatile__("" ::: "memory");
#endif
    }
    
    (*attempt)++;
}

// ------------------------------------------------------------------------
// Lock-Free Implementation
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

static bool find(SkipList* list, int key, Node** preds, Node** succs) {
    int bottomLevel = 0;
    int attempt = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= bottomLevel; level--) {
            Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
            
            while (true) {
                if (curr == NULL) break; 

                Node* succ = atomic_load(&curr->next[level]);
                
                // Helping: Check if marked
                while (IS_MARKED(succ)) {
                    Node* unmarked_succ = GET_UNMARKED(succ);
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                        backoff(&attempt); // Backoff before restarting
                        goto retry; 
                    }
                    
                    // CAS success, move forward
                    curr = unmarked_succ;
                    if (curr == NULL) break;
                    succ = atomic_load(&curr->next[level]);
                }
                
                if (curr == NULL) break;
                
                if (curr != list->tail && curr->key < key) {
                    pred = curr;
                    curr = GET_UNMARKED(succ);
                } else {
                    break; 
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
    int attempt = 0;
    
    while (true) {
        if (find(list, key, preds, succs)) {
            return false; // Key exists
        }
        
        int topLevel = random_level(); 
        Node* newNode = create_node(key, value, topLevel);
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // Link Level 0
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            backoff(&attempt); // Backoff on CAS failure
            continue; 
        }
        
        atomic_fetch_add(&list->size, 1);
        
        // Build Tower
        for (int i = 1; i <= topLevel; i++) {
            int build_attempts = 0;
            
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success
                }
                
                // If we fail too many times to link this level, we give up.
                // It is better to have a slightly shorter node than to livelock the system.
                // The node is already safely in Level 0, so it is correct.
                if (++build_attempts > TOWER_BUILD_RETRIES) {
                    break; 
                }
                
                find(list, key, preds, succs);
                
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    return true; 
                }
                
                atomic_store(&newNode->next[i], succs[i]);
                // No backoff here, finding is expensive enough
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
    int attempt = 0;
    
    while (true) {
        if (!find(list, key, preds, succs)) {
            return false; 
        }
        
        victim = succs[0];
        
        // Logical Deletion
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                Node* succ = atomic_load(&victim->next[i]);
                
                if (IS_MARKED(succ)) {
                    if (i == 0) return false; // Already deleted
                    break; // Already marked at this level, proceed to next
                }
                
                Node* marked_succ = GET_MARKED(succ);
                if (atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                    break; // Successfully marked
                }
                
                // CAS failed.
                // If Level 0, we MUST retry until success or we discover it's already deleted.
                // If Upper Level, we also retry to ensure consistent state for 'find' helper.
                backoff(&attempt);
            }
        }
        
        // Physical Deletion
        find(list, key, preds, succs);
        
        atomic_fetch_sub(&list->size, 1);
        return true;
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (true) {
            if (curr == NULL || curr == list->tail) break;
            Node* succ = atomic_load(&curr->next[level]);
            
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
    Node* curr = list->head;
    while (curr) {
        Node* next = GET_UNMARKED(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}