#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>

// ------------------------------------------------------------------------
// Pointer Marking Macros (Harris Algorithm)
// ------------------------------------------------------------------------
#define MARK_BIT 1
#define IS_MARKED(p)      ((uintptr_t)(p) & MARK_BIT)
#define GET_UNMARKED(p)   ((Node*)((uintptr_t)(p) & ~MARK_BIT))
#define GET_MARKED(p)     ((Node*)((uintptr_t)(p) | MARK_BIT))

/**
 * Helper: Find
 * 
 * Traverses the list to find the position for 'key'.
 * Performs physical removal (helping) of marked nodes.
 * 
 * Returns: true if key found, false otherwise.
 * Populates: preds[] and succs[] arrays.
 */
static bool find(SkipList* list, int key, Node** preds, Node** succs) {
    int bottomLevel = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        
        // Traverse from Top Level down to Bottom
        for (int level = list->maxLevel; level >= bottomLevel; level--) {
            // Start traversing at this level
            Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
            
            while (true) {
                // Sentinel check
                if (curr == NULL) break; 

                Node* succ = atomic_load(&curr->next[level]);
                
                // 1. Check if 'curr' is marked for deletion
                while (IS_MARKED(succ)) {
                    // Physical deletion: Try to swing pred->next over curr
                    Node* unmarked_succ = GET_UNMARKED(succ);
                    
                    // CAS: If pred->next is still curr, make it unmarked_succ
                    // Note: If pred is also marked, this CAS will fail automatically 
                    // because pred->next would contain a mark bit, but we compare against unmarked curr.
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                        goto retry; // CAS failed, retry search from scratch
                    }
                    
                    // Move forward (curr is now effectively skipped)
                    curr = unmarked_succ;
                    if (curr == NULL) break;
                    succ = atomic_load(&curr->next[level]);
                }
                
                if (curr == NULL) break;
                
                // 2. Compare keys
                // Stop if we found a node >= key, or hit the tail
                if (curr != list->tail && curr->key < key) {
                    pred = curr;
                    curr = GET_UNMARKED(succ);
                } else {
                    break; // Found our position between pred and curr
                }
            }
            
            // Record path
            preds[level] = pred;
            succs[level] = curr;
        }
        
        // Return true if found at level 0
        return (succs[bottomLevel] != list->tail && succs[bottomLevel]->key == key);
    }
}

// ------------------------------------------------------------------------
// Lock-Free Implementation Public API
// ------------------------------------------------------------------------

SkipList* skiplist_create_lockfree(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    // Use the common create_node to ensure struct layout matches utils
    // Using INT_MIN/INT_MAX for sentinels
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    // Link Head to Tail at all levels
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
    }
    
    atomic_init(&list->size, 0);
    list->maxLevel = MAX_LEVEL;
    
    return list;
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        // 1. Search
        if (find(list, key, preds, succs)) {
            return false; // Key already exists
        }
        
        // 2. Create Node
        // Use external random_level and create_node from utils
        int topLevel = random_level(); 
        Node* newNode = create_node(key, value, topLevel);
        
        // Initialize next pointers
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // 3. Link at Level 0 (Linearization Point)
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            // Failed to insert at level 0.
            // Safe to free newNode here because it was never effectively linked.
            // Note: create_node initializes a lock, we should clean it up if strictly needed,
            // but for this specific test flow, free is acceptable here.
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue; // Retry loop
        }
        
        atomic_fetch_add(&list->size, 1);
        
        // 4. Build the tower upwards (Best Effort)
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                // Try to splice in
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success at this level
                }
                
                // Failure: References are stale. Re-find.
                find(list, key, preds, succs);
                
                // Check if our new node was deleted concurrently
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    // We are being deleted, stop building the tower
                    return true;
                }
                
                // Reset next pointer to the NEW successor
                atomic_store(&newNode->next[i], succs[i]);
            }
        }
        
        atomic_store(&newNode->fully_linked, true);
        return true;
    }
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* victim;
    
    while (true) {
        // 1. Search
        if (!find(list, key, preds, succs)) {
            return false; // Not found
        }
        
        victim = succs[0];
        
        // 2. Logical Deletion (Marking)
        // Mark from top down or bottom up? Harris usually marks next pointers.
        // We only strictly need to mark level 0 for correctness.
        // If we mark level 0 successfully, the node is "deleted".
        
        // We try to mark all levels to aid helping, but Level 0 is the authority.
        for (int i = victim->topLevel; i >= 0; i--) {
            Node* succ = atomic_load(&victim->next[i]);
            
            // If already marked, another thread is working on it.
            if (IS_MARKED(succ)) {
                if (i == 0) return false; // Already deleted
                continue;
            }
            
            Node* marked_succ = GET_MARKED(succ);
            if (!atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                // Failed at Level 0 means proper concurrent modification/delete. Retry.
                if (i == 0) goto retry_delete;
            }
        }
        
        // 3. Physical Unlinking (Helping)
        // Calling find() again triggers the physical cleanup logic internally.
        find(list, key, preds, succs);
        
        atomic_fetch_sub(&list->size, 1);
        
        // CRITICAL: DO NOT FREE 'victim' here.
        // In a lock-free C implementation without Epoch Based Reclamation (EBR) 
        // or Hazard Pointers, freeing a node that other threads might be traversing
        // causes Segmentation Faults. 
        // We rely on the test teardown (destroy) to clean up memory.
        
        return true;
        
        retry_delete:;
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    // Top-down search
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (true) {
            if (curr == NULL || curr == list->tail) break;
            
            Node* succ = atomic_load(&curr->next[level]);
            
            // Skip marked nodes
            while (IS_MARKED(succ)) {
                curr = GET_UNMARKED(succ);
                if (curr == NULL || curr == list->tail) goto next_level;
                succ = atomic_load(&curr->next[level]);
            }
            
            if (curr->key < key) {
                pred = curr;
                curr = GET_UNMARKED(succ);
            } else {
                break;
            }
        }
        next_level:;
    }
    
    // Final check at level 0
    Node* curr = GET_UNMARKED(atomic_load(&pred->next[0]));
    
    return (curr != list->tail && 
            curr->key == key && 
            !IS_MARKED(atomic_load(&curr->next[0])));
}

void skiplist_destroy_lockfree(SkipList* list) {
    // Only safe to call when no other threads are accessing the list
    Node* curr = list->head;
    while (curr) {
        // We must strip marks because delete() marked pointers but didn't unlink/free them
        Node* next = GET_UNMARKED(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}