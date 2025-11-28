#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h>

// Backoff configuration
#define BACKOFF_BASE_SPINS 1
#define BACKOFF_MAX_SPINS  4096
#define YIELD_THRESHOLD 3
#define MAX_RETRIES 100

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
    int spins = BACKOFF_BASE_SPINS << (*attempt);
    if (spins > BACKOFF_MAX_SPINS) spins = BACKOFF_MAX_SPINS;
    for (volatile int i = 0; i < spins; i++) {
        cpu_relax();
    }
}

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
retry:
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (curr != list->tail) {
            Node* succ = atomic_load(&curr->next[level]);
            
            // Physical helping
            while (IS_MARKED(succ)) {
                Node* unmarked_succ = GET_UNMARKED(succ);
                if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                    goto retry;
                }
                curr = unmarked_succ;
                if (curr == list->tail) break;
                succ = atomic_load(&curr->next[level]);
            }
            
            if (curr == list->tail) break;
            
            if (curr->key < key) {
                pred = curr;
                curr = GET_UNMARKED(succ);
            } else {
                break;
            }
        }
        
        preds[level] = pred;
        succs[level] = curr;
    }
    
    return (succs[0] != list->tail && succs[0]->key == key);
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    int attempt = 0;
    
    while (attempt++ < MAX_RETRIES) {
        if (find(list, key, preds, succs)) {
            // FIX: Check if found node is marked (zombie)
            Node* found = succs[0];
            Node* next = atomic_load(&found->next[0]);
            if (!IS_MARKED(next)) {
                return false; // Live node exists
            }
            // Zombie found, continue to insert
        }
        
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Initialize all next pointers
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // Link at level 0 (linearization point)
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            backoff(&attempt);
            continue;
        }
        
        atomic_fetch_add(&list->size, 1);
        
        // Build tower with validation
        for (int i = 1; i <= topLevel; i++) {
            int tower_attempts = 0;
            
            while (tower_attempts++ < 3) {
                // FIX: Check if node was deleted while building tower
                Node* curr_next = atomic_load(&newNode->next[0]);
                if (IS_MARKED(curr_next)) {
                    // Node was deleted, stop building
                    goto tower_done;
                }
                
                pred = preds[i];
                succ = succs[i];
                
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success at this level
                }
                
                // FIX: Refresh preds/succs AND update newNode's next pointer
                find(list, key, preds, succs);
                atomic_store(&newNode->next[i], succs[i]);
            }
        }
        
    tower_done:
        atomic_store(&newNode->fully_linked, true);
        return true;
    }
    
    return false; // Max retries exceeded
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    int attempt = 0;
    
    while (attempt++ < MAX_RETRIES) {
        if (!find(list, key, preds, succs)) {
            return false;
        }
        
        Node* victim = succs[0];
        
        // Mark from top to bottom
        for (int i = victim->topLevel; i >= 0; i--) {
            Node* succ;
            do {
                succ = atomic_load(&victim->next[i]);
                if (IS_MARKED(succ)) {
                    // Already marked at this level
                    if (i == 0) return false; // Someone else deleted
                    break;
                }
            } while (!atomic_compare_exchange_strong(&victim->next[i], &succ, GET_MARKED(succ)));
        }
        
        // Physical removal (helping)
        find(list, key, preds, succs);
        
        atomic_fetch_sub(&list->size, 1);
        return true;
    }
    
    return false;
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (curr != list->tail) {
            Node* succ = atomic_load(&curr->next[level]);
            
            // Skip marked nodes
            while (IS_MARKED(succ)) {
                curr = GET_UNMARKED(succ);
                if (curr == list->tail) goto next_level;
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