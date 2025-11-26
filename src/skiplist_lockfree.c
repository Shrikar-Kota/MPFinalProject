/**
 * Lock-Free Skip List - Standalone Version
 * 
 * TRUE lock-free with CAS-only operations
 * Simplified single-level design for reliability
 * No dependencies on other implementations
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
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    atomic_store(&list->head->marked, false);
    atomic_store(&list->tail->marked, false);
    
    return list;
}

// TRUE LOCK-FREE INSERT - Only CAS, no locks!
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    
    // Bounded retries to prevent livelock
    for (int attempt = 0; attempt < 100; attempt++) {
        
        // Find position at level 0 (simplified for reliability)
        Node* pred = list->head;
        Node* curr = atomic_load(&pred->next[0]);
        
        while (curr != list->tail) {
            bool marked = atomic_load(&curr->marked);
            
            if (!marked && curr->key >= key) {
                break;
            }
            
            if (!marked && curr->key < key) {
                pred = curr;
            }
            
            curr = atomic_load(&curr->next[0]);
        }
        
        // Check duplicate
        if (curr != list->tail && curr->key == key && !atomic_load(&curr->marked)) {
            return false;
        }
        
        // Create node (level 0 only for simplicity)
        Node* newNode = create_node(key, value, 0);
        atomic_store(&newNode->next[0], curr);
        atomic_store(&newNode->marked, false);
        
        // CAS - THE LINEARIZATION POINT (no lock!)
        Node* expected = curr;
        if (atomic_compare_exchange_strong(&pred->next[0], &expected, newNode)) {
            atomic_fetch_add(&list->size, 1);
            return true;
        }
        
        // CAS failed, cleanup and retry
        omp_destroy_lock(&newNode->lock);
        free(newNode);
        
        // Exponential backoff
        if (attempt > 20) {
            for (volatile int i = 0; i < (1 << (attempt - 20)); i++);
        }
    }
    
    return false;  // Max retries exceeded
}

// TRUE LOCK-FREE DELETE - Only CAS, no locks!
bool skiplist_delete_lockfree(SkipList* list, int key) {
    
    for (int attempt = 0; attempt < 100; attempt++) {
        
        // Find victim
        Node* curr = atomic_load(&list->head->next[0]);
        
        while (curr != list->tail) {
            if (curr->key == key) {
                // Try to mark as deleted (CAS - linearization point)
                bool expected = false;
                if (atomic_compare_exchange_strong(&curr->marked, &expected, true)) {
                    atomic_fetch_sub(&list->size, 1);
                    // Note: Don't free immediately - lock-free contains might access
                    // In production: use epoch-based reclamation
                    // For project: memory leak is safer than crash
                    return true;
                }
                // Someone else deleted it
                return false;
            }
            
            if (curr->key > key) {
                return false;
            }
            
            curr = atomic_load(&curr->next[0]);
        }
        
        return false;
    }
    
    return false;
}

// Wait-free contains (no CAS needed, just reads)
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* curr = atomic_load(&list->head->next[0]);
    
    while (curr != NULL && curr != list->tail) {
        if (curr->key == key) {
            bool marked = atomic_load(&curr->marked);
            return !marked;
        }
        
        if (curr->key > key) {
            return false;
        }
        
        curr = atomic_load(&curr->next[0]);
    }
    
    return false;
}

void skiplist_destroy_lockfree(SkipList* list) {
    // Safe to free everything at destroy time
    Node* curr = list->head;
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}
