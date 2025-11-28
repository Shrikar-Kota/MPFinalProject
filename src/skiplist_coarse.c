#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/**
 * Coarse-Grained Lock Skip List
 * 
 * Logic:
 * 1. Acquire global lock.
 * 2. Perform operation (Search/Insert/Delete).
 * 3. Release global lock.
 * 
 * Pros: Simple, easy to prove correct (sequential consistency).
 * Cons: Zero concurrency. Readers block writers, writers block readers.
 */

SkipList* skiplist_create_coarse(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) {
        perror("Failed to allocate skip list");
        exit(1);
    }
    
    // Create sentinels
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    // Using static max level for simplicity
    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    
    // Initialize the Global Lock
    omp_init_lock(&list->lock);
    
    // Link head to tail
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    
    // Optional: Mark sentinels as fully linked (consistency with other impls)
    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    
    return list;
}

bool skiplist_insert_coarse(SkipList* list, int key, int value) {
    // 1. Acquire Global Lock
    omp_set_lock(&list->lock);
    
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    
    // 2. Search for position
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        
        // Check for duplicates
        if (level == 0 && curr != list->tail && curr->key == key) {
            omp_unset_lock(&list->lock);
            return false;
        }
    }
    
    // 3. Create Node
    // Note: Allocating inside the lock increases critical section time,
    // but ensures we don't allocate if the key already exists.
    int topLevel = random_level();
    Node* newNode = create_node(key, value, topLevel);
    atomic_store(&newNode->fully_linked, true);
    
    // 4. Link Node
    for (int level = 0; level <= topLevel; level++) {
        Node* succ = atomic_load(&preds[level]->next[level]);
        atomic_store(&newNode->next[level], succ);
        atomic_store(&preds[level]->next[level], newNode);
    }
    
    atomic_fetch_add(&list->size, 1);
    
    // 5. Release Lock
    omp_unset_lock(&list->lock);
    return true;
}

bool skiplist_delete_coarse(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    Node* victim = NULL;
    
    // Search
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
                return false; // Not found
            }
        }
    }
    
    // Unlink
    for (int level = 0; level <= victim->topLevel; level++) {
        Node* succ = atomic_load(&victim->next[level]);
        atomic_store(&preds[level]->next[level], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    omp_unset_lock(&list->lock);
    
    // Safe to free outside lock because node is now unreachable
    // and we are not using lock-free optimistic readers.
    omp_destroy_lock(&victim->lock); // Clean up the unused node lock
    free(victim);
    
    return true;
}

bool skiplist_contains_coarse(SkipList* list, int key) {
    // Crucial: Readers must acquire lock in Coarse-Grained
    // Otherwise a writer could free a node while we are traversing it.
    omp_set_lock(&list->lock);
    
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
    }
    
    // Check level 0
    Node* curr = atomic_load(&pred->next[0]);
    bool found = (curr != list->tail && curr->key == key);
    
    omp_unset_lock(&list->lock);
    return found;
}

void skiplist_destroy_coarse(SkipList* list) {
    // No lock needed if we assume only one thread calls destroy
    Node* curr = list->head;
    
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock); // Clean up unused node locks
        free(curr);
        curr = next;
    }
    
    omp_destroy_lock(&list->lock);
    free(list);
}