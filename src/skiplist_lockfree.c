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

// Helper function to search and build predecessor/successor arrays
static Node* search(SkipList* list, int key, Node* preds[], Node* succs[]) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail) {
            Node* succ = atomic_load(&curr->next[level]);
            bool marked = atomic_load(&curr->marked);
            
            if (marked) {
                // Help remove marked node
                atomic_compare_exchange_strong(&pred->next[level], &curr, succ);
                curr = atomic_load(&pred->next[level]);
                continue;
            }
            
            if (curr->key >= key) {
                break;
            }
            
            pred = curr;
            curr = succ;
        }
        
        preds[level] = pred;
        succs[level] = curr;
    }
    
    return succs[0];
}

// Lock-free insert using CAS
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        Node* found = search(list, key, preds, succs);
        
        if (found != list->tail && found->key == key) {
            bool marked = atomic_load(&found->marked);
            if (!marked) {
                return false;
            }
        }
        
        int newLevel = random_level();
        Node* newNode = create_node(key, value, newLevel);
        
        for (int level = 0; level <= newLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        for (int level = 1; level <= newLevel; level++) {
            while (true) {
                pred = preds[level];
                succ = succs[level];
                
                if (atomic_compare_exchange_strong(&pred->next[level], &succ, newNode)) {
                    break;
                }
                
                search(list, key, preds, succs);
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
}

// Lock-free delete using logical deletion
bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        Node* victim = search(list, key, preds, succs);
        
        if (victim == list->tail || victim->key != key) {
            return false;
        }
        
        bool marked = atomic_load(&victim->marked);
        if (marked) {
            return false;
        }
        
        bool expected = false;
        if (!atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            continue;
        }
        
        for (int level = victim->topLevel; level >= 0; level--) {
            Node* succ = atomic_load(&victim->next[level]);
            
            while (true) {
                Node* pred = preds[level];
                Node* predNext = atomic_load(&pred->next[level]);
                
                if (predNext == victim) {
                    if (atomic_compare_exchange_strong(&pred->next[level], &victim, succ)) {
                        break;
                    }
                }
                
                search(list, key, preds, succs);
                break;
            }
        }
        
        atomic_fetch_sub(&list->size, 1);
        return true;
    }
}

// Lock-free contains (wait-free operation)
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* curr = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = atomic_load(&curr->next[level]);
        
        while (next != list->tail && next->key < key) {
            curr = next;
            next = atomic_load(&curr->next[level]);
        }
        
        if (level == 0) {
            if (next != list->tail && next->key == key) {
                bool marked = atomic_load(&next->marked);
                return !marked;
            }
            return false;
        }
    }
    
    return false;
}

// Destroy lock-free skip list
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