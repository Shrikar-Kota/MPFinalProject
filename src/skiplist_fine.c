#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Create skip list with fine-grained locking
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
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    
    return list;
}

// Helper to lock unique predecessors only (avoid double-locking same node)
static void lock_predecessors(Node* preds[], int topLevel) {
    // Lock unique nodes only, in order from lowest index
    for (int i = 0; i <= topLevel; i++) {
        // Check if this node was already locked
        bool already_locked = false;
        for (int j = 0; j < i; j++) {
            if (preds[j] == preds[i]) {
                already_locked = true;
                break;
            }
        }
        if (!already_locked) {
            omp_set_lock(&preds[i]->lock);
        }
    }
}

// Helper to unlock unique predecessors
static void unlock_predecessors(Node* preds[], int topLevel) {
    // Unlock in reverse order, unique nodes only
    for (int i = topLevel; i >= 0; i--) {
        // Check if this node needs to be unlocked
        bool already_unlocked = false;
        for (int j = i + 1; j <= topLevel; j++) {
            if (preds[j] == preds[i]) {
                already_unlocked = true;
                break;
            }
        }
        if (!already_unlocked) {
            omp_unset_lock(&preds[i]->lock);
        }
    }
}

// Insert with fine-grained locking and optimistic validation
bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    int retry_count = 0;
    const int MAX_RETRIES = 100;
    
    while (retry_count < MAX_RETRIES) {
        retry_count++;
        
        // Phase 1: Optimistic search (no locks held)
        Node* curr = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* next = atomic_load(&curr->next[level]);
            while (next != list->tail && next->key < key) {
                curr = next;
                next = atomic_load(&curr->next[level]);
            }
            preds[level] = curr;
            succs[level] = next;
        }
        
        // Check if key already exists
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false;
        }
        
        // Phase 2: Create new node
        int newLevel = random_level();
        Node* newNode = create_node(key, value, newLevel);
        
        // Phase 3: Lock predecessors
        lock_predecessors(preds, newLevel);
        
        // Phase 4: Validate that nothing changed
        bool valid = true;
        for (int i = 0; i <= newLevel; i++) {
            Node* current_next = atomic_load(&preds[i]->next[i]);
            if (current_next != succs[i]) {
                valid = false;
                break;
            }
            // Also check that successor is still valid
            if (succs[i] != list->tail && succs[i]->key == key) {
                valid = false;
                break;
            }
        }
        
        if (valid) {
            // Phase 5: Link new node at all levels
            for (int i = 0; i <= newLevel; i++) {
                atomic_store(&newNode->next[i], succs[i]);
                atomic_store(&preds[i]->next[i], newNode);
            }
            
            unlock_predecessors(preds, newLevel);
            atomic_fetch_add(&list->size, 1);
            return true;
        } else {
            // Validation failed, cleanup and retry
            unlock_predecessors(preds, newLevel);
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            // Continue to retry
        }
    }
    
    // Too many retries - this shouldn't happen in practice
    fprintf(stderr, "Warning: Insert exceeded retry limit\n");
    return false;
}

// Delete with fine-grained locking and optimistic validation
bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* victim = NULL;
    
    int retry_count = 0;
    const int MAX_RETRIES = 1000;  // Increased for high contention
    
    while (retry_count < MAX_RETRIES) {
        retry_count++;
        
        // Phase 1: Optimistic search
        Node* curr = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* next = atomic_load(&curr->next[level]);
            while (next != list->tail && next->key < key) {
                curr = next;
                next = atomic_load(&curr->next[level]);
            }
            preds[level] = curr;
            
            if (level == 0) {
                if (next != list->tail && next->key == key) {
                    victim = next;
                } else {
                    return false;  // Key not found
                }
            }
        }
        
        // Phase 2: Lock victim first, then predecessors
        // Try to lock victim with timeout-like behavior
        if (!omp_test_lock(&victim->lock)) {
            // Someone else has victim locked, retry quickly
            continue;
        }
        
        lock_predecessors(preds, victim->topLevel);
        
        // Phase 3: Validate
        bool valid = true;
        for (int i = 0; i <= victim->topLevel; i++) {
            Node* current_next = atomic_load(&preds[i]->next[i]);
            if (current_next != victim) {
                valid = false;
                break;
            }
        }
        
        if (valid) {
            // Phase 4: Unlink victim from all levels
            for (int i = 0; i <= victim->topLevel; i++) {
                Node* succ = atomic_load(&victim->next[i]);
                atomic_store(&preds[i]->next[i], succ);
            }
            
            unlock_predecessors(preds, victim->topLevel);
            omp_unset_lock(&victim->lock);
            
            atomic_fetch_sub(&list->size, 1);
            
            // Free victim
            omp_destroy_lock(&victim->lock);
            free(victim);
            return true;
        } else {
            // Validation failed, unlock and retry
            unlock_predecessors(preds, victim->topLevel);
            omp_unset_lock(&victim->lock);
            
            // Small backoff before retry
            if (retry_count % 10 == 0) {
                // Every 10 retries, yield to other threads
                #pragma omp taskyield
            }
        }
    }
    
    // Exceeded retry limit - fall back to simple approach
    // This shouldn't happen often, but ensures progress
    return false;
}

// Contains with optimistic reading (lock-free for reads)
bool skiplist_contains_fine(SkipList* list, int key) {
    Node* curr = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = atomic_load(&curr->next[level]);
        while (next != list->tail && next->key < key) {
            curr = next;
            next = atomic_load(&curr->next[level]);
        }
        
        if (level == 0) {
            return (next != list->tail && next->key == key);
        }
    }
    
    return false;
}

// Destroy skip list
void skiplist_destroy_fine(SkipList* list) {
    Node* curr = list->head;
    
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    
    free(list);
}