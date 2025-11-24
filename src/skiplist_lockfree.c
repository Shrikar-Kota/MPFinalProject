#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <emmintrin.h>

// Exponential Backoff
static void backoff_delay(int count) {
    const int max_delay = 128;
    int delay = (count > max_delay) ? max_delay : count;
    // _mm_pause hints to the CPU that we are in a spin-wait loop
    for (int i = 0; i < delay; i++) { _mm_pause(); }
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
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, succ)) {
                        backoff_delay(++retry_count);
                        goto retry;
                    }
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
        
        if (succs && succs[0] != list->tail && succs[0]->key == key) {
            Node* next_at_0 = atomic_load(&succs[0]->next[0]);
            if (IS_MARKED(next_at_0)) {
                // The node is logically deleted.
                // If we have retried too many times, assume it's gone and return FALSE.
                // This prevents infinite hangs in rare livelock cases.
                if (retry_count > 1000) return false;
                
                backoff_delay(++retry_count);
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
            backoff_delay(++retry_count);
            continue;
        }
        
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                Node* pred = preds[i];
                Node* succ = succs[i];
                atomic_store(&newNode->next[i], succ);
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) break;
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
        if (!find_and_clean(list, key, preds, succs)) return false;
        
        Node* victim = succs[0];
        Node* succNext = atomic_load(&victim->next[0]);
        
        while (!IS_MARKED(succNext)) {
            Node* markedSucc = MARK_PTR(succNext);
            if (atomic_compare_exchange_strong(&victim->next[0], &succNext, markedSucc)) {
                find_and_clean(list, key, NULL, NULL); // Cleanup
                atomic_fetch_sub(&list->size, 1);
                return true;
            }
            backoff_delay(++retry_count);
            succNext = atomic_load(&victim->next[0]);
        }
        return false;
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