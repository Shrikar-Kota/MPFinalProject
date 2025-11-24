#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Create lock-free skip list
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
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    
    return list;
}

// Simple find without physical removal
static Node* find_node(SkipList* list, int key, Node* preds[], Node* succs[]) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != NULL && curr != list->tail) {
            // Safety check
            if (curr->key >= key) {
                break;
            }
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        if (preds != NULL) preds[level] = pred;
        if (succs != NULL) succs[level] = (curr != NULL) ? curr : list->tail;
    }
    
    Node* found = (succs != NULL) ? succs[0] : atomic_load(&pred->next[0]);
    if (found != NULL && found != list->tail && found->key == key) {
        return found;
    }
    return NULL;
}

// Lock-free insert
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    int retry = 0;
    const int MAX_RETRY = 100;
    
    while (retry++ < MAX_RETRY) {
        // Find position
        Node* found = find_node(list, key, preds, succs);
        
        // Key already exists?
        if (found != NULL) {
            bool marked = atomic_load(&found->marked);
            if (!marked) {
                return false;
            }
        }
        
        // Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Set next pointers
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        // Try to insert at level 0 first
        Node* pred0 = preds[0];
        Node* succ0 = succs[0];
        
        if (atomic_compare_exchange_strong(&pred0->next[0], &succ0, newNode)) {
            // Success at level 0, link upper levels
            for (int level = 1; level <= topLevel; level++) {
                // Best effort linking for upper levels
                Node* pred = preds[level];
                Node* succ = succs[level];
                atomic_store(&newNode->next[level], succ);
                
                int attempts = 0;
                while (!atomic_compare_exchange_strong(&pred->next[level], &succ, newNode)) {
                    if (++attempts > 3) break; // Give up after few tries
                    
                    // Re-search at this level
                    pred = list->head;
                    for (int l = list->maxLevel; l >= level; l--) {
                        Node* curr = atomic_load(&pred->next[l]);
                        while (curr != NULL && curr != list->tail && curr->key < key) {
                            pred = curr;
                            curr = atomic_load(&pred->next[l]);
                        }
                        if (l == level) {
                            succ = (curr != NULL) ? curr : list->tail;
                            atomic_store(&newNode->next[level], succ);
                        }
                    }
                }
            }
            
            atomic_fetch_add(&list->size, 1);
            return true;
        } else {
            // Failed at level 0, clean up and retry
            omp_destroy_lock(&newNode->lock);
            free(newNode);
        }
    }
    
    return false;
}

// Lock-free delete (logical only)
bool skiplist_delete_lockfree(SkipList* list, int key) {
    int retry = 0;
    const int MAX_RETRY = 100;
    
    while (retry++ < MAX_RETRY) {
        Node* preds[MAX_LEVEL + 1];
        Node* succs[MAX_LEVEL + 1];
        
        Node* victim = find_node(list, key, preds, succs);
        
        if (victim == NULL) {
            return false;
        }
        
        // Check if already marked
        bool marked = atomic_load(&victim->marked);
        if (marked) {
            return false;
        }
        
        // Try to mark it
        bool expected = false;
        if (atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            atomic_fetch_sub(&list->size, 1);
            return true;
        }
    }
    
    return false;
}

// Lock-free contains
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* curr = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = atomic_load(&curr->next[level]);
        
        while (next != NULL && next != list->tail && next->key < key) {
            curr = next;
            next = atomic_load(&curr->next[level]);
        }
        
        if (level == 0) {
            if (next != NULL && next != list->tail && next->key == key) {
                return !atomic_load(&next->marked);
            }
            return false;
        }
    }
    
    return false;
}

// Destroy
void skiplist_destroy_lockfree(SkipList* list) {
    Node* curr = list->head;
    
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    
    free(list);
}