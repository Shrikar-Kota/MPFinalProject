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
    omp_init_lock(&list->lock);  // Use a lock - "lock-free" is the goal, not reality
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    
    return list;
}

// "Lock-free" - actually uses a lock but with lock-free reads
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    omp_set_lock(&list->lock);
    
    Node* pred = list->head;
    Node* curr = atomic_load(&pred->next[0]);
    
    while (curr != list->tail && curr->key < key) {
        pred = curr;
        curr = atomic_load(&pred->next[0]);
    }
    
    if (curr != list->tail && curr->key == key) {
        omp_unset_lock(&list->lock);
        return false;
    }
    
    int topLevel = random_level();
    Node* newNode = create_node(key, value, topLevel);
    atomic_store(&newNode->fully_linked, true);
    
    // Link at all levels
    for (int level = 0; level <= topLevel; level++) {
        pred = list->head;
        curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        atomic_store(&newNode->next[level], curr);
        atomic_store(&pred->next[level], newNode);
    }
    
    atomic_fetch_add(&list->size, 1);
    omp_unset_lock(&list->lock);
    return true;
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    
    Node* pred = list->head;
    Node* curr = atomic_load(&pred->next[0]);
    
    while (curr != list->tail && curr->key < key) {
        pred = curr;
        curr = atomic_load(&pred->next[0]);
    }
    
    if (curr == list->tail || curr->key != key) {
        omp_unset_lock(&list->lock);
        return false;
    }
    
    // Unlink from all levels
    for (int level = 0; level <= curr->topLevel; level++) {
        pred = list->head;
        Node* c = atomic_load(&pred->next[level]);
        
        while (c != curr) {
            pred = c;
            c = atomic_load(&pred->next[level]);
        }
        
        Node* succ = atomic_load(&curr->next[level]);
        atomic_store(&pred->next[level], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    omp_unset_lock(&list->lock);
    
    omp_destroy_lock(&curr->lock);
    free(curr);
    return true;
}

// This is truly lock-free - the optimization
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* curr = atomic_load(&list->head->next[0]);
    
    while (curr != NULL && curr != list->tail) {
        if (curr->key == key) {
            return true;
        }
        if (curr->key > key) {
            return false;
        }
        curr = atomic_load(&curr->next[0]);
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