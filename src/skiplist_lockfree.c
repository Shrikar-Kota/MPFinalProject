#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <emmintrin.h>

static void backoff_delay(int count) {
    int delay = (1 << count);
    if (delay > 64) delay = 64;
    for (int i = 0; i < delay; i++) _mm_pause();
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

// Robust find that aggressively cleans marked nodes
static bool find_and_clean(SkipList* list, int key, Node** preds, Node** succs) {
    int retry_count = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
            
            while (true) {
                if (curr == NULL || curr == list->tail) break;
                
                Node* succ_raw = atomic_load(&curr->next[level]);
                Node* succ = UNMARK_PTR(succ_raw);
                bool marked = IS_MARKED(succ_raw);
                
                while (marked) {
                    // Helper: Physical deletion
                    // We must ensure 'pred' is not pointing to a marked node if possible,
                    // but here we are linking 'pred' to 'succ'.
                    // Note: We are using 'curr' (unmarked) as expected value.
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, succ)) {
                        // CAS failed - our view of the world is old. Restart.
                        if (++retry_count > 5000) return false; // Emergency break
                        backoff_delay(retry_count % 10);
                        goto retry;
                    }
                    
                    // Successfully unlinked 'curr'.
                    curr = succ;
                    if (curr == NULL || curr == list->tail) break;
                    
                    succ_raw = atomic_load(&curr->next[level]);
                    succ = UNMARK_PTR(succ_raw);
                    marked = IS_MARKED(succ_raw);
                }
                
                if (curr == NULL || curr == list->tail) break;
                
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
        
        // Check if the node found is valid
        if (succs && succs[0] != list->tail && succs[0]->key == key) {
            Node* next_at_0 = atomic_load(&succs[0]->next[0]);
            if (IS_MARKED(next_at_0)) {
                // Node is logically deleted but not physically removed yet
                // We must not return "found" for a deleted node.
                // But we must also ensure it gets cleaned up.
                // Simply retry to let the helper logic clean it.
                if (++retry_count > 5000) return false;
                backoff_delay(retry_count % 10);
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
        if (find_and_clean(list, key, preds, succs)) return false;
        
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        for (int i = 0; i <= topLevel; i++) atomic_store(&newNode->next[i], succs[i]);
        
        if (!atomic_compare_exchange_strong(&preds[0]->next[0], &succs[0], newNode)) {
            free(newNode);
            if (++retry_count > 5000) return false; // Safety
            backoff_delay(retry_count % 10);
            continue;
        }
        
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                Node* pred = preds[i];
                Node* succ = succs[i];
                atomic_store(&newNode->next[i], succ);
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) break;
                // If failed, refresh view
                find_and_clean(list, key, preds, succs);
                // Note: We don't retry infinitely for upper levels. 
                // If we can't link, the node is just reachable via level 0 (valid but slower).
                if (preds[i]->key >= key || succs[i]->key <= key) break; // Heuristic break
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
        if (!find_and_clean(list, key, preds, succs)) return false;
        
        Node* victim = succs[0];
        Node* succNext = atomic_load(&victim->next[0]);
        
        // 1. Logical Deletion (Mark bit 0)
        while (!IS_MARKED(succNext)) {
            Node* markedSucc = MARK_PTR(succNext);
            if (atomic_compare_exchange_strong(&victim->next[0], &succNext, markedSucc)) {
                // 2. Physical Deletion
                find_and_clean(list, key, NULL, NULL);
                atomic_fetch_sub(&list->size, 1);
                return true;
            }
            // If CAS fails, reload
            succNext = atomic_load(&victim->next[0]);
            if (++retry_count > 5000) return false;
            backoff_delay(retry_count % 10);
        }
        return false; // Already marked
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = UNMARK_PTR(atomic_load(&pred->next[level]));
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = UNMARK_PTR(atomic_load(&pred->next[level]));
        }
        if (curr != list->tail && curr->key == key) {
            return !IS_MARKED(atomic_load(&curr->next[0]));
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