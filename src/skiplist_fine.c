#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>

/**
 * Fine-Grained Locking Skip List (Optimistic Approach)
 * 
 * Strategy:
 * 1. Search without locks (Optimistic).
 * 2. Lock the specific nodes involved.
 * 3. Validate that the nodes are still linked and not marked for deletion.
 * 4. Perform operation.
 * 
 * Memory Safety:
 * To prevent Segmentation Faults (Use-After-Free) during concurrent execution,
 * we DO NOT free nodes immediately after deletion. Another thread might be 
 * holding a pointer to the node waiting to lock it.
 * Cleanup happens in skiplist_destroy_fine().
 */

SkipList* skiplist_create_fine(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    // Create sentinels
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    // Explicitly init locks for sentinels (create_node does it, but being explicit helps)
    omp_init_lock(&list->head->lock);
    omp_init_lock(&list->tail->lock);

    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    
    // Link head to tail
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    
    return list;
}

/**
 * Optimistic Search:
 * Traverses the list without acquiring locks.
 * Returns the likely predecessors and successors.
 * Caller MUST lock and validate before using.
 */
static void find_optimistic(SkipList* list, int key, Node** preds, Node** succs) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        // Iterate while key is greater
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        succs[level] = curr;
    }
}

/**
 * Validate Link:
 * Checks if 'pred' still points to 'succ' at 'level' and neither is marked.
 */
static bool validate_link(Node* pred, Node* succ, int level) {
    bool pred_marked = atomic_load(&pred->marked);
    bool succ_marked = atomic_load(&succ->marked);
    Node* actual_next = atomic_load(&pred->next[level]);
    
    return !pred_marked && !succ_marked && (actual_next == succ);
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        // 1. Optimistic Search
        find_optimistic(list, key, preds, succs);
        
        // Check if key exists (optimization before locking)
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false; 
        }
        
        // 2. Lock Level 0 Predecessor (Linearization Point Decision)
        omp_set_lock(&preds[0]->lock);
        
        // 3. Validate Level 0
        if (!validate_link(preds[0], succs[0], 0)) {
            omp_unset_lock(&preds[0]->lock);
            continue; // Retry
        }
        
        // Check for duplicates again while locked
        if (succs[0] != list->tail && succs[0]->key == key) {
            omp_unset_lock(&preds[0]->lock);
            return false;
        }
        
        // 4. Create and Insert Node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Fill next pointers
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // Link at Level 0
        atomic_store(&preds[0]->next[0], newNode);
        omp_unset_lock(&preds[0]->lock);
        
        atomic_fetch_add(&list->size, 1);
        
        // 5. Link Upper Levels (Incremental Locking)
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Validate
                if (!validate_link(preds[i], succs[i], i)) {
                    omp_unset_lock(&preds[i]->lock);
                    // Re-search this level only
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while(c != list->tail && c->key < key) {
                        p = c; 
                        c = atomic_load(&p->next[i]);
                    }
                    preds[i] = p;
                    succs[i] = c;
                    continue; // Retry with new pointers
                }
                
                // Link
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
        
        // Optimistic check
        if (victim == list->tail || victim->key != key) {
            return false;
        }
        
        // 1. Lock Victim
        omp_set_lock(&victim->lock);
        
        if (atomic_load(&victim->marked)) {
            omp_unset_lock(&victim->lock);
            return false; // Already deleted
        }
        
        // Check key again (sanity)
        if (victim->key != key) {
            omp_unset_lock(&victim->lock);
            continue;
        }
        
        // 2. Mark Victim (Logical Deletion)
        atomic_store(&victim->marked, true);
        omp_unset_lock(&victim->lock);
        
        // 3. Unlink from all levels (Physical Deletion)
        // We do this level by level to minimize lock hold time
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Validate predecessor still points to victim
                if (atomic_load(&preds[i]->next[i]) != victim) {
                    omp_unset_lock(&preds[i]->lock);
                    // Re-find
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while(c != list->tail && c->key < key) { p = c; c = atomic_load(&p->next[i]); }
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
        
        // CRITICAL: DO NOT FREE VICTIM HERE
        // Other threads might be currently attempting to lock 'victim'.
        // Freeing it now causes Segmentation Faults.
        // Memory is leaked intentionally for correctness in this implementation type
        // or effectively reclaimed at program exit / skiplist_destroy.
        
        return true;
    }
}

bool skiplist_contains_fine(SkipList* list, int key) {
    Node* pred = list->head;
    Node* curr = NULL;
    
    // Fully lock-free search (optimistic reads are safe if we don't free memory)
    for (int level = list->maxLevel; level >= 0; level--) {
        curr = atomic_load(&pred->next[level]);
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
    }
    
    return (curr != list->tail && curr->key == key && !atomic_load(&curr->marked));
}

void skiplist_destroy_fine(SkipList* list) {
    // 1. Gather all nodes (reachable)
    // 2. We also need to clean up unreachable (deleted) nodes to be perfectly clean,
    //    but without a garbage list, we can only clean reachable ones.
    //    In a benchmark context, OS cleanup is acceptable for deleted nodes.
    
    Node* curr = list->head;
    while (curr) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}