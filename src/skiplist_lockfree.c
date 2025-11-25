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
    atomic_store(&list->head->fully_linked, true);
    atomic_store(&list->tail->fully_linked, true);
    
    return list;
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    for (int retry = 0; retry < 1000; retry++) {
        // Find position at level 0 only - simple and correct
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
        
        // Check if key exists
        if (curr != list->tail && curr->key == key && !atomic_load(&curr->marked)) {
            return false;
        }
        
        // Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        atomic_store(&newNode->next[0], curr);
        atomic_store(&newNode->fully_linked, true);
        
        // CAS at level 0 - this is the linearization point
        if (atomic_compare_exchange_strong(&pred->next[0], &curr, newNode)) {
            // Success! Link upper levels (best effort)
            for (int level = 1; level <= topLevel; level++) {
                Node* lpred = list->head;
                Node* lcurr = atomic_load(&lpred->next[level]);
                
                while (lcurr != list->tail && lcurr->key < key) {
                    bool marked = atomic_load(&lcurr->marked);
                    if (!marked) {
                        lpred = lcurr;
                    }
                    lcurr = atomic_load(&lpred->next[level]);
                }
                
                atomic_store(&newNode->next[level], lcurr);
                Node* exp = lcurr;
                atomic_compare_exchange_strong(&lpred->next[level], &exp, newNode);
            }
            
            atomic_fetch_add(&list->size, 1);
            return true;
        }
        
        // CAS failed, cleanup and retry
        omp_destroy_lock(&newNode->lock);
        free(newNode);
    }
    
    return false;
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* curr = atomic_load(&list->head->next[0]);
    
    while (curr != list->tail) {
        if (curr->key == key) {
            bool expected = false;
            if (atomic_compare_exchange_strong(&curr->marked, &expected, true)) {
                atomic_fetch_sub(&list->size, 1);
                return true;
            }
            return false;
        }
        curr = atomic_load(&curr->next[0]);
    }
    
    return false;
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* curr = atomic_load(&list->head->next[0]);
    
    while (curr != NULL && curr != list->tail) {
        if (curr->key == key) {
            return !atomic_load(&curr->marked);
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
    free(list);
}