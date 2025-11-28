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

// PERFORMANCE TUNING
// ------------------
// Base spins: How long to wait on first failure (low latency)
#define BACKOFF_BASE_SPINS 1 
// Max spins: Cap the waiting time so we don't sleep too long
#define BACKOFF_MAX_SPINS  1000
// Tower Retries: Try HARDER to build the skip list tower. 
// Previous value of 2 caused flat lists (O(N) search). 
// 100 ensures we build good O(log N) structures even under load.
#define TOWER_BUILD_RETRIES 100
// Yield Threshold: Only sleep the thread if things are REALLY bad
#define YIELD_THRESHOLD 20

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
    
    // Only yield CPU if we are stuck in a massive CAS storm
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
                        backoff(&attempt); 
                        goto retry; 
                    }
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
        
        // Init next pointers
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
        
        // Build Tower (Performance Optimized)
        for (int i = 1; i <= topLevel; i++) {
            int build_attempts = 0;
            
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success
                }
                
                // Retry significantly more times to ensure good O(log N) structure
                if (++build_attempts >= TOWER_BUILD_RETRIES) {
                    goto stop_building; 
                }
                
                // Re-find path
                find(list, key, preds, succs);
                
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    goto stop_building; // We are dead
                }
                
                atomic_store(&newNode->next[i], succs[i]);
                cpu_relax(); // Light pause only
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
                    if (i == 0) return false; // Already deleted
                    break; 
                }
                
                Node* marked_succ = GET_MARKED(succ);
                if (atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                    break; // Success
                }
                
                // Optimization: If upper level mark fails, ignore it and move down.
                if (i > 0) break;
                
                // Level 0 MUST succeed
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