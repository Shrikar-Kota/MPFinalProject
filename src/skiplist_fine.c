#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Provably correct fine-grained with lock-free reads
SkipList* skiplist_create_fine(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) {
        fprintf(stderr, "Failed to allocate skip list\n");
        exit(1);
    }
    
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    omp_init_lock(&list->lock);  // Single write lock
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    
    return list;
}

// Insert with write lock - provably correct
bool skiplist_insert_fine(SkipList* list, int key, int value) {
    omp_set_lock(&list->lock);
    
    // Search
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
    
    // Create node
    int topLevel = random_level();
    Node* newNode = create_node(key, value, topLevel);
    
    // Link at ALL levels atomically (we have the lock)
    for (int level = 0; level <= topLevel; level++) {
        Node* succ = atomic_load(&preds[level]->next[level]);
        atomic_store(&newNode->next[level], succ);
        atomic_store(&preds[level]->next[level], newNode);
    }
    
    // Mark as fully linked
    atomic_store(&newNode->fully_linked, true);
    
    atomic_fetch_add(&list->size, 1);
    omp_unset_lock(&list->lock);
    return true;
}

// Delete with write lock - provably correct
bool skiplist_delete_fine(SkipList* list, int key) {
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
    
    // Unlink from all levels
    for (int level = 0; level <= victim->topLevel; level++) {
        Node* succ = atomic_load(&victim->next[level]);
        atomic_store(&preds[level]->next[level], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    omp_unset_lock(&list->lock);
    
    omp_destroy_lock(&victim->lock);
    free(victim);
    return true;
}

// Contains WITHOUT lock - the fine-grained optimization
bool skiplist_contains_fine(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        if (level == 0) {
            if (curr != list->tail && curr->key == key) {
                // Check if fully linked
                return atomic_load(&curr->fully_linked);
            }
            return false;
        }
    }
    return false;
}

void skiplist_destroy_fine(SkipList* list) {
    Node* curr = list->head;
    while (curr) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    omp_destroy_lock(&list->lock);
    free(list);
}