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
// IS_MARKED, GET_UNMARKED are in skiplist_common.h

// Tuning for High Contention (16+ Threads)
#define BACKOFF_BASE_SPINS 1
#define BACKOFF_MAX_SPINS  2048

// CRITICAL: Yield sooner. 
// With 16 threads, spinning too long creates a "Convoy Effect".
// Yielding lets the thread holding the 'state' finish faster.
#define YIELD_THRESHOLD 3 

#define TOWER_BUILD_RETRIES 50 

static inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

static void backoff(int *attempt) {
    (*attempt)++;
    if (*attempt > YIELD_THRESHOLD) {
        sched_yield();
        return;
    }
    int spins = BACKOFF_BASE_SPINS << *attempt;
    if (spins > BACKOFF_MAX_SPINS) spins = BACKOFF_MAX_SPINS;
    for (volatile int i = 0; i < spins; i++) {
        cpu_relax();
    }
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

/**
 * Robust Find with Local Recovery.
 * Prevents Livelock by avoiding global restarts when possible.
 */
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
                    
                    // Attempt physical removal
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                        // CAS FAILED.
                        backoff(&attempt);
                        
                        // CRITICAL FIX: Local Recovery.
                        // Instead of blindly 'goto retry' (Head), check if pred is dead.
                        Node* ref = atomic_load(&pred->next[level]);
                        
                        if (IS_MARKED(ref)) {
                            // Pred is dead. Must restart from Head.
                            goto retry;
                        }
                        
                        // Pred is alive! 'curr' just changed (e.g. insert or another delete).
                        // We update 'curr' and stay in the loop.
                        curr = GET_UNMARKED(ref);
                        if (curr == NULL) break;
                        succ = atomic_load(&curr->next[level]);
                        continue; // Re-evaluate loop with new curr
                    }
                    
                    // Success. Move forward.
                    curr = unmarked_succ;
                    if (curr == NULL) break;
                    succ = atomic_load(&curr->next[level]);
                }
                
                if (curr == NULL) break;
                
                // Traversal
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
            // Found a node with the key.
            // Check if it's a "Zombie" (marked for deletion but physically present).
            Node* found = succs[0];
            if (!IS_MARKED(atomic_load(&found->next[0]))) {
                return false; // It's alive. Key exists.
            }
            // It's a Zombie. Ignore it and proceed to insert our new node.
            // (Standard behavior: insert physically before the zombie, zombie eventually vanishes).
        }
        
        int topLevel = random_level(); 
        Node* newNode = create_node(key, value, topLevel);
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // Link Level 0 (Linearization Point)
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            backoff(&attempt); 
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
                    break; 
                }
                
                // Stop if too difficult
                if (++build_attempts >= TOWER_BUILD_RETRIES) {
                    goto stop_building; 
                }
                
                find(list, key, preds, succs);
                
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    goto stop_building; 
                }
                
                atomic_store(&newNode->next[i], succs[i]);
                cpu_relax();
            }
        }
        
    stop_building:
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
        
        // Logical Deletion (Marking)
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                Node* succ = atomic_load(&victim->next[i]);
                
                if (IS_MARKED(succ)) {
                    if (i == 0) return false; 
                    break; 
                }
                
                Node* marked_succ = GET_MARKED(succ);
                if (atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                    break; 
                }
                
                // Optimization: If upper level fails, ignore.
                if (i > 0) break;
                
                backoff(&attempt);
            }
        }
        
        // Physical Cleanup
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