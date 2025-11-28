#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#include <sched.h> 

/**
 * Fine-Grained Locking Skip List
 * 
 * Strategy:
 * 1. Optimistic Search (No locks).
 * 2. Per-Node Locking.
 * 3. Validation:
 *    - Insert: Checks for duplicates strictly (even if !fully_linked).
 *    - Delete/Contains: Uses fully_linked for visibility to avoid locking/returning incomplete nodes.
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
    return !atomic_load(&pred->marked) && 
           !atomic_load(&succ->marked) && 
           (atomic_load(&pred->next[level]) == succ);
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        find_optimistic(list, key, preds, succs);
        
        // Check for existing key
        Node* found = succs[0];
        if (found != list->tail && found->key == key) {
            // FIX: If found is NOT marked, it exists.
            // We do NOT check fully_linked here. If it's at Level 0, it blocks duplicates.
            if (!atomic_load(&found->marked)) {
                return false; 
            }
            // If it IS marked, we proceed to insert (allowing insert before dead node)
        }
        
        omp_set_lock(&preds[0]->lock);
        
        if (!validate_link(preds[0], succs[0], 0)) {
            omp_unset_lock(&preds[0]->lock);
            continue; 
        }
        
        // Double check under lock
        found = succs[0];
        if (found != list->tail && found->key == key) {
            if (!atomic_load(&found->marked)) {
                omp_unset_lock(&preds[0]->lock);
                return false;
            }
        }
        
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        atomic_store(&preds[0]->next[0], newNode);
        omp_unset_lock(&preds[0]->lock);
        
        atomic_fetch_add(&list->size, 1);
        
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                if (!validate_link(preds[i], succs[i], i)) {
                    omp_unset_lock(&preds[i]->lock);
                    
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while (c != list->tail && c->key < key) {
                        p = c;
                        c = atomic_load(&p->next[i]);
                    }
                    preds[i] = p;
                    succs[i] = c;
                    continue; 
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
        
        if (victim == list->tail || victim->key != key) {
            return false;
        }
        
        omp_set_lock(&victim->lock);
        
        if (atomic_load(&victim->marked)) {
            omp_unset_lock(&victim->lock);
            return false;
        }
        
        if (victim->key != key) {
            omp_unset_lock(&victim->lock);
            continue; 
        }

        // Visibility check for deletion:
        // Avoid deleting nodes that are not fully built to prevent races with Insert's upper-level linking.
        if (!atomic_load(&victim->fully_linked)) {
            omp_unset_lock(&victim->lock);
            return false; 
        }
        
        atomic_store(&victim->marked, true);
        omp_unset_lock(&victim->lock);
        
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Ghost predecessor check
                if (atomic_load(&preds[i]->marked) || 
                    atomic_load(&preds[i]->next[i]) != victim) {
                    
                    omp_unset_lock(&preds[i]->lock);
                    
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