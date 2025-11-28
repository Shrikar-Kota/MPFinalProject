#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h> 

/**
 * Fine-Grained Locking Skip List
 * 
 * Key Features:
 * - Optimistic Search (No locks during traversal)
 * - Per-Node Locking (omp_lock_t)
 * - Validation (Check-Wait-Act pattern)
 * - Livelock Prevention: Search restarts if it encounters marked nodes
 * - Use-After-Free Prevention: Defer free() to destroy time
 */

SkipList* skiplist_create_fine(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    omp_init_lock(&list->head->lock);
    omp_init_lock(&list->tail->lock);

    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    atomic_init(&list->head->marked, false);
    atomic_init(&list->tail->marked, false);

    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    
    return list;
}

/**
 * Optimistic Search
 * 
 * Populates preds[] and succs[] for the given key.
 * CRITICAL FIX: If we encounter a marked 'pred', we restart the search.
 * This prevents returning a deleted node as a valid predecessor, 
 * which would cause infinite validation failures in the caller.
 */
static void find_optimistic(SkipList* list, int key, Node** preds, Node** succs) {
    while (true) {
        bool restart = false;
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != list->tail && curr->key < key) {
                pred = curr;
                curr = atomic_load(&pred->next[level]);
                
                // If we traversed to a marked node, our path is invalid.
                if (atomic_load(&pred->marked)) {
                    restart = true;
                    break;
                }
            }
            
            if (restart || atomic_load(&pred->marked)) {
                restart = true;
                break;
            }
            
            preds[level] = pred;
            succs[level] = curr;
        }
        
        if (!restart) return; // Success
        // Implicit continue: retry search from head
    }
}

/**
 * Validates that:
 * 1. pred is not marked
 * 2. succ is not marked
 * 3. pred->next points to succ
 */
static bool validate_link(Node* pred, Node* succ, int level) {
    return !atomic_load(&pred->marked) && 
           !atomic_load(&succ->marked) && 
           (atomic_load(&pred->next[level]) == succ);
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        find_optimistic(list, key, preds, succs);
        
        // Optimistic check for duplicates
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false; 
        }
        
        // Lock Level 0 (Linearization point)
        omp_set_lock(&preds[0]->lock);
        
        if (!validate_link(preds[0], succs[0], 0)) {
            omp_unset_lock(&preds[0]->lock);
            continue; // Validation failed, retry
        }
        
        // Check for duplicates again under lock
        if (succs[0] != list->tail && succs[0]->key == key) {
            omp_unset_lock(&preds[0]->lock);
            return false;
        }
        
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // Link Level 0
        atomic_store(&preds[0]->next[0], newNode);
        omp_unset_lock(&preds[0]->lock);
        
        atomic_fetch_add(&list->size, 1);
        
        // Link Upper Levels
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                if (!validate_link(preds[i], succs[i], i)) {
                    omp_unset_lock(&preds[i]->lock);
                    // Re-search this level specifically
                    // We must apply the same "restart if marked" logic here
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    bool level_restart = false;
                    
                    while (c != list->tail && c->key < key) {
                        p = c;
                        c = atomic_load(&p->next[i]);
                        if (atomic_load(&p->marked)) {
                            // Pred is dead, we must restart search from head for this level
                            p = list->head;
                            c = atomic_load(&p->next[i]);
                        }
                    }
                    
                    // Final check on p
                    if (atomic_load(&p->marked)) {
                         // Extremely unlucky race, just loop again
                         continue;
                    }
                    
                    preds[i] = p;
                    succs[i] = c;
                    continue; // Retry lock
                }
                
                atomic_store(&newNode->next[i], succs[i]);
                atomic_store(&preds[i]->next[i], newNode);
                omp_unset_lock(&preds[i]->lock);
                break;
            }
        }
        
        atomic_store(&newNode->fully_linked, true);
        return true;
    }
}

bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        find_optimistic(list, key, preds, succs);
        Node* victim = succs[0];
        
        // If not found or key mismatch
        if (victim == list->tail || victim->key != key) {
            return false;
        }
        
        omp_set_lock(&victim->lock);
        
        if (atomic_load(&victim->marked)) {
            omp_unset_lock(&victim->lock);
            return false; // Already deleted
        }
        
        if (victim->key != key) {
            omp_unset_lock(&victim->lock);
            continue; // Node changed identity (unlikely with this allocator, but safe)
        }

        // Wait for node to be fully inserted
        if (!atomic_load(&victim->fully_linked)) {
            omp_unset_lock(&victim->lock);
            sched_yield(); 
            continue;
        }
        
        // Logically delete
        atomic_store(&victim->marked, true);
        omp_unset_lock(&victim->lock);
        
        // Physically unlink
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Validate predecessor points to victim
                if (atomic_load(&preds[i]->next[i]) != victim) {
                    omp_unset_lock(&preds[i]->lock);
                    
                    // Re-search logic with safety against marked nodes
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while (c != list->tail && c->key < key) {
                        p = c;
                        c = atomic_load(&p->next[i]);
                        if (atomic_load(&p->marked)) {
                            p = list->head;
                            c = atomic_load(&p->next[i]);
                        }
                    }
                    preds[i] = p;
                    continue;
                }
                
                // Unlink
                Node* next = atomic_load(&victim->next[i]);
                atomic_store(&preds[i]->next[i], next);
                omp_unset_lock(&preds[i]->lock);
                break;
            }
        }
        
        atomic_fetch_sub(&list->size, 1);
        // Do not free() to prevent UAF
        return true;
    }
}

bool skiplist_contains_fine(SkipList* list, int key) {
    Node* pred = list->head;
    Node* curr = NULL;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        curr = atomic_load(&pred->next[level]);
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
    }
    
    return (curr != list->tail && 
            curr->key == key && 
            atomic_load(&curr->fully_linked) && 
            !atomic_load(&curr->marked));
}

void skiplist_destroy_fine(SkipList* list) {
    Node* curr = list->head;
    while (curr) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}