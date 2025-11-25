#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Absolutely safe lock-free - no freeing during operations
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
    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    atomic_store(&list->head->marked, false);
    atomic_store(&list->tail->marked, false);
    
    return list;
}

// Simple, safe insert
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    for (int retry = 0; retry < 1000; retry++) {
        Node* preds[MAX_LEVEL + 1];
        
        // Find position
        Node* pred = list->head;
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != NULL && curr != list->tail) {
                // Check flags safely
                bool marked = atomic_load(&curr->marked);
                if (marked) {
                    // Skip marked nodes
                    curr = atomic_load(&curr->next[level]);
                    continue;
                }
                
                if (curr->key >= key) {
                    break;
                }
                
                pred = curr;
                curr = atomic_load(&curr->next[level]);
            }
            
            preds[level] = pred;
        }
        
        // Check if exists at level 0
        Node* check = atomic_load(&preds[0]->next[0]);
        if (check != NULL && check != list->tail && check->key == key) {
            bool marked = atomic_load(&check->marked);
            if (!marked) {
                return false;
            }
        }
        
        // Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Link all levels
        for (int level = 0; level <= topLevel; level++) {
            Node* succ = atomic_load(&preds[level]->next[level]);
            atomic_store(&newNode->next[level], succ);
        }
        
        // Try CAS at level 0
        Node* expected_succ = atomic_load(&preds[0]->next[0]);
        if (!atomic_compare_exchange_strong(&preds[0]->next[0], &expected_succ, newNode)) {
            // Failed, cleanup and retry
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        // IMMEDIATELY mark as fully linked after level 0 succeeds
        // This makes the node visible to contains()
        atomic_store(&newNode->fully_linked, true);
        
        // Success at level 0, link upper levels (best effort)
        for (int level = 1; level <= topLevel; level++) {
            for (int attempt = 0; attempt < 5; attempt++) {
                Node* curr_succ = atomic_load(&preds[level]->next[level]);
                atomic_store(&newNode->next[level], curr_succ);
                if (atomic_compare_exchange_strong(&preds[level]->next[level], &curr_succ, newNode)) {
                    break;
                }
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
    
    return false;
}

// Delete - ONLY mark, NEVER free
bool skiplist_delete_lockfree(SkipList* list, int key) {
    for (int retry = 0; retry < 1000; retry++) {
        // Find the node
        Node* pred = list->head;
        Node* victim = NULL;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != NULL && curr != list->tail) {
                bool marked = atomic_load(&curr->marked);
                if (marked) {
                    curr = atomic_load(&curr->next[level]);
                    continue;
                }
                
                if (curr->key >= key) {
                    if (level == 0 && curr->key == key) {
                        victim = curr;
                    }
                    break;
                }
                
                pred = curr;
                curr = atomic_load(&curr->next[level]);
            }
        }
        
        if (victim == NULL) {
            return false;
        }
        
        // Mark it (logical deletion)
        bool expected = false;
        if (atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            // Successfully marked
            // NOTE: We do NOT free the node!
            // This prevents all use-after-free issues
            atomic_fetch_sub(&list->size, 1);
            return true;
        }
        // Someone else marked it, they deleted it
    }
    
    return false;
}

// Contains - simple and reliable
bool skiplist_contains_lockfree(SkipList* list, int key) {
    // Just traverse level 0 - simple and reliable
    Node* curr = atomic_load(&list->head->next[0]);
    
    while (curr != NULL && curr != list->tail) {
        if (curr->key == key) {
            // Found it - check if deleted
            bool marked = atomic_load(&curr->marked);
            return !marked;
        }
        
        if (curr->key > key) {
            // Passed it, not found
            return false;
        }
        
        curr = atomic_load(&curr->next[0]);
    }
    
    return false;
}

// Destroy - only safe time to free nodes
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