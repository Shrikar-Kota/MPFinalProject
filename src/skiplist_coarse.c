/**
 * Coarse-Grained Locking Skip List
 * 
 * Implementation Strategy:
 * - Single global lock protects all operations
 * - Simple and provably correct
 * - Baseline for performance comparison
 * 
 * Correctness: Sequential consistency guaranteed by global mutex
 * Performance: Limited scalability due to serialization
 */

#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

SkipList* skiplist_create_coarse(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) {
        fprintf(stderr, "Failed to allocate skip list\n");
        exit(1);
    }
    
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    omp_init_lock(&list->lock);
    
    // Initialize sentinel nodes
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    
    return list;
}

/**
 * Insert operation
 * Linearization point: When global lock is held and node is linked
 */
bool skiplist_insert_coarse(SkipList* list, int key, int value) {
    omp_set_lock(&list->lock);
    
    // Search phase - find predecessors at all levels
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        
        // Check for duplicate at level 0
        if (level == 0 && curr != list->tail && curr->key == key) {
            omp_unset_lock(&list->lock);
            return false;
        }
    }
    
    // Create new node with random level
    int topLevel = random_level();
    Node* newNode = create_node(key, value, topLevel);
    atomic_store(&newNode->fully_linked, true);
    
    // Link at all levels atomically (protected by global lock)
    for (int level = 0; level <= topLevel; level++) {
        Node* succ = atomic_load(&preds[level]->next[level]);
        atomic_store(&newNode->next[level], succ);
        atomic_store(&preds[level]->next[level], newNode);
    }
    
    atomic_fetch_add(&list->size, 1);
    omp_unset_lock(&list->lock);
    return true;
}

/**
 * Delete operation
 * Linearization point: When global lock is held and node is unlinked
 */
bool skiplist_delete_coarse(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    
    // Search for victim
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    Node* victim = NULL;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        
        if (level == 0) {
            if (curr != list->tail && curr->key == key) {
                victim = curr;
            } else {
                omp_unset_lock(&list->lock);
                return false;
            }
        }
    }
    
    // Unlink from all levels
    for (int level = 0; level <= victim->topLevel; level++) {
        Node* succ = atomic_load(&victim->next[level]);
        atomic_store(&preds[level]->next[level], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    omp_unset_lock(&list->lock);
    
    // Safe to free - no concurrent access possible
    omp_destroy_lock(&victim->lock);
    free(victim);
    
    return true;
}

/**
 * Contains operation
 * Linearization point: When global lock is held and search completes
 */
bool skiplist_contains_coarse(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    
    Node* curr = list->head;
    
    // Skip list search from top level down
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = atomic_load(&curr->next[level]);
        
        while (next != list->tail && next->key < key) {
            curr = next;
            next = atomic_load(&curr->next[level]);
        }
        
        if (level == 0) {
            bool found = (next != list->tail && next->key == key);
            omp_unset_lock(&list->lock);
            return found;
        }
    }
    
    omp_unset_lock(&list->lock);
    return false;
}

void skiplist_destroy_coarse(SkipList* list) {
    Node* curr = list->head;
    
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    
    omp_destroy_lock(&list->lock);
    free(list);
}
