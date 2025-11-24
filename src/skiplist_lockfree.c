#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <emmintrin.h> // For _mm_pause()

// --- OPTIMIZATION: Exponential Backoff ---
// Reduces memory bus contention when CAS fails repeatedly
static void backoff_delay(int count) {
    const int max_delay = 64;
    int delay = (count > max_delay) ? max_delay : count;
    for (int i = 0; i < delay; i++) {
        _mm_pause(); 
    }
}

SkipList* skiplist_create_lockfree(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
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

// The core traversal function with "Helping"
// It traverses the list, physically unlinking any marked nodes it finds.
static bool find_and_clean(SkipList* list, int key, Node** preds, Node** succs) {
    int retry_count = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
            
            while (true) {
                // 1. Read successor info
                if (curr == NULL || curr == list->tail) break;
                Node* succ_raw = atomic_load(&curr->next[level]);
                Node* succ = UNMARK_PTR(succ_raw);
                bool marked = IS_MARKED(succ_raw);
                
                // 2. If marked, HELP delete it
                while (marked) {
                    // Try to swing pred->next from curr to succ
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, succ)) {
                        // If pred->next changed, our view is stale. Retry from start.
                        backoff_delay(++retry_count);
                        goto retry;
                    }
                    
                    // Successfully unlinked 'curr'. Move forward.
                    curr = succ;
                    if (curr == NULL || curr == list->tail) break;
                    
                    succ_raw = atomic_load(&curr->next[level]);
                    succ = UNMARK_PTR(succ_raw);
                    marked = IS_MARKED(succ_raw);
                }
                
                if (curr == NULL || curr == list->tail) break;
                
                // 3. Standard traversal
                if (curr->key < key) {
                    pred = curr;
                    curr = succ;
                } else {
                    break;
                }
            }
            
            if (preds) preds[level] = pred;
            if (succs) succs[level] = curr;
        }
        
        // Check if the node we found (succs[0]) is fully valid
        if (succs && succs[0] != list->tail && succs[0]->key == key) {
            // Check if it's currently being deleted (marked at level 0)
            Node* next_at_0 = atomic_load(&succs[0]->next[0]);
            if (IS_MARKED(next_at_0)) {
                 goto retry; 
            }
        }
        
        return (succs && succs[0] != list->tail && succs[0]->key == key);
    }
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    int retry_count = 0;
    
    while (true) {
        if (find_and_clean(list, key, preds, succs)) {
            return false; // Key exists
        }
        
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Initialize next pointers
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        // Linearization Point: Insert at Level 0
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            // CAS failed, retry
            free(newNode); // Safe to free, never public
            backoff_delay(++retry_count);
            continue;
        }
        
        // Insert successful! Now link upper levels (Best Effort)
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                // Update newNode to point to fresh successor
                atomic_store(&newNode->next[i], succ);
                
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success at this level
                }
                
                // If failed, re-find predecessors for specific level
                // (Simplified: we re-run find to update preds/succs)
                find_and_clean(list, key, preds, succs);
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    int retry_count = 0;
    
    while (true) {
        if (!find_and_clean(list, key, preds, succs)) {
            return false; // Key doesn't exist
        }
        
        Node* victim = succs[0];
        
        // 1. LOGICAL DELETION
        // Mark the next pointer at level 0.
        Node* succNext = atomic_load(&victim->next[0]);
        
        // Loop until marked
        while (!IS_MARKED(succNext)) {
            Node* markedSucc = MARK_PTR(succNext);
            if (atomic_compare_exchange_strong(&victim->next[0], &succNext, markedSucc)) {
                // 2. PHYSICAL DELETION
                // We successfully marked it. Now call find() to cleanup.
                find_and_clean(list, key, NULL, NULL);
                atomic_fetch_sub(&list->size, 1);
                return true;
            }
            // Reload if CAS failed
            backoff_delay(++retry_count);
            succNext = atomic_load(&victim->next[0]);
        }
        
        // Already marked by someone else? Treat as not found/already deleted.
        return false;
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    // Wait-Free Search
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
        
        while (curr != NULL && curr != list->tail && curr->key < key) {
            pred = curr;
            curr = UNMARK_PTR(atomic_load(&curr->next[level]));
        }
        
        if (curr != NULL && curr != list->tail && curr->key == key) {
            // Valid only if not marked
            Node* nextPtr = atomic_load(&curr->next[0]);
            return !IS_MARKED(nextPtr);
        }
    }
    return false;
}

void skiplist_destroy_lockfree(SkipList* list) {
    Node* curr = list->head;
    while (curr != NULL) {
        Node* next = UNMARK_PTR(atomic_load(&curr->next[0]));
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}