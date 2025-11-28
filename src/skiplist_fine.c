#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h> 

/**
 * Fine-Grained Locking Skip List
 * 
 * Strategy: Optimistic Locking with "Fully Linked" Visibility
 * 
 * To prevent hangs/deadlocks between Insert and Delete:
 * 1. Insert: Links Level 0 -> Links Upper Levels -> Sets fully_linked=true.
 * 2. Delete/Contains: If a node is found but !fully_linked, we treat it 
 *    as "not found" (effectively invisible). This prevents Delete from 
 *    spinning/waiting on a partially inserted node.
 * 
 * Safety:
 * - Use-After-Free is prevented by NOT freeing nodes until skiplist_destroy.
 */

SkipList* skiplist_create_fine(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    omp_init_lock(&list->head->lock);
    omp_init_lock(&list->tail->lock);

    // Sentinels are always visible
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
 * traverses without locks. Returns preds/succs.
 */
static void find_optimistic(SkipList* list, int key, Node** preds, Node** succs) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        succs[level] = curr;
    }
}

static bool validate_link(Node* pred, Node* succ, int level) {
    // Standard validation: neither is marked, and link is preserved
    return !atomic_load(&pred->marked) && 
           !atomic_load(&succ->marked) && 
           (atomic_load(&pred->next[level]) == succ);
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        find_optimistic(list, key, preds, succs);
        
        // Check if key exists and is visible
        Node* found = succs[0];
        if (found != list->tail && found->key == key) {
            if (atomic_load(&found->fully_linked) && !atomic_load(&found->marked)) {
                return false; // Already exists
            }
            // If it exists but !fully_linked or marked, we can't insert a duplicate key 
            // until the previous one is fully gone or fully stable. 
            // Simplified strategy: treat as "exists" to avoid duplicates during race.
            return false;
        }
        
        // Lock Level 0 (Linearization Point)
        omp_set_lock(&preds[0]->lock);
        
        if (!validate_link(preds[0], succs[0], 0)) {
            omp_unset_lock(&preds[0]->lock);
            continue; // Retry
        }
        
        // Double check existence under lock
        found = succs[0];
        if (found != list->tail && found->key == key) {
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
                    
                    // Robust Re-search: Start from head to avoid getting lost in deleted paths
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while (c != list->tail && c->key < key) {
                        p = c;
                        c = atomic_load(&p->next[i]);
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
        
        // Make visible to Delete/Contains
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
        
        // Basic check
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
            continue; // Node changed identity (unlikely but safe)
        }

        // VISIBILITY CHECK:
        // If the node is not fully linked, we treat it as "not found".
        // This avoids the spin-wait livelock.
        if (!atomic_load(&victim->fully_linked)) {
            omp_unset_lock(&victim->lock);
            return false; 
        }
        
        // Logically delete
        atomic_store(&victim->marked, true);
        omp_unset_lock(&victim->lock);
        
        // Physically unlink
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Validate predecessor
                if (atomic_load(&preds[i]->next[i]) != victim) {
                    omp_unset_lock(&preds[i]->lock);
                    
                    // Robust Re-search
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while (c != list->tail && c->key < key) {
                        p = c;
                        c = atomic_load(&p->next[i]);
                    }
                    preds[i] = p;
                    continue;
                }
                
                Node* next = atomic_load(&victim->next[i]);
                atomic_store(&preds[i]->next[i], next);
                omp_unset_lock(&preds[i]->lock);
                break;
            }
        }
        
        atomic_fetch_sub(&list->size, 1);
        // Do not free()
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
    
    // Strict visibility: Must be fully linked AND not marked
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