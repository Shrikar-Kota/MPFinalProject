/**
 * Lock-Free Skip List - Bulletproof Version
 * 
 * Pragmatic hybrid approach:
 * - Lock for writes (ensures correctness)
 * - Lock-free reads (scalability)
 * - Optimized search (cache predecessors)
 * - Full skip list structure
 */

#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

SkipList* skiplist_create_lockfree(void) {
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
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    atomic_store(&list->head->marked, false);
    atomic_store(&list->tail->marked, false);
    
    return list;
}

// Insert with lock - cache predecessors (OPTIMIZED)
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    omp_set_lock(&list->lock);
    
    // Search ONCE, cache predecessors (like fine-grained)
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        
        if (level == 0 && curr != list->tail && curr->key == key) {
            omp_unset_lock(&list->lock);
            return false;
        }
    }
    
    int topLevel = random_level();
    Node* newNode = create_node(key, value, topLevel);
    atomic_store(&newNode->marked, false);
    
    // Link using cached predecessors (FAST)
    for (int level = 0; level <= topLevel; level++) {
        Node* succ = atomic_load(&preds[level]->next[level]);
        atomic_store(&newNode->next[level], succ);
        atomic_store(&preds[level]->next[level], newNode);
    }
    
    atomic_fetch_add(&list->size, 1);
    omp_unset_lock(&list->lock);
    return true;
}

// Delete with lock - cache predecessors (OPTIMIZED)
bool skiplist_delete_lockfree(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    
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
    
    // Mark as deleted FIRST (for lock-free readers)
    atomic_store(&victim->marked, true);
    
    // Unlink from all levels
    for (int level = 0; level <= victim->topLevel; level++) {
        Node* succ = atomic_load(&victim->next[level]);
        atomic_store(&preds[level]->next[level], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    omp_unset_lock(&list->lock);
    
    // Don't free - lock-free readers might access
    // Memory leak is safer than crash
    
    return true;
}

// TRULY LOCK-FREE contains - the key optimization
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        if (level == 0) {
            if (curr != list->tail && curr->key == key) {
                // Check if marked (deleted)
                return !atomic_load(&curr->marked);
            }
            return false;
        }
    }
    
    return false;
}

void skiplist_destroy_lockfree(SkipList* list) {
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
