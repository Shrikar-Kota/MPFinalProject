#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Correct lock-free skip list
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

// Find with retry
static Node* find_node_safe(SkipList* list, int key, Node* preds[], Node* succs[]) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        // Skip marked nodes
        while (curr != NULL && curr != list->tail) {
            bool marked = atomic_load(&curr->marked);
            if (marked) {
                curr = atomic_load(&curr->next[level]);
                continue;
            }
            
            if (curr->key >= key) {
                break;
            }
            
            pred = curr;
            curr = atomic_load(&curr->next[level]);
        }
        
        if (preds) preds[level] = pred;
        if (succs) succs[level] = (curr != NULL) ? curr : list->tail;
    }
    
    Node* found = succs ? succs[0] : atomic_load(&pred->next[0]);
    if (found && found != list->tail && found->key == key) {
        bool marked = atomic_load(&found->marked);
        if (!marked) {
            return found;
        }
    }
    return NULL;
}

// Lock-free insert - correct version
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    int attempts = 0;
    const int MAX_ATTEMPTS = 100;
    
    while (attempts++ < MAX_ATTEMPTS) {
        Node* found = find_node_safe(list, key, preds, succs);
        
        if (found != NULL) {
            return false; // Already exists
        }
        
        // Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Link all levels of new node
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        // Try to insert at level 0 first (most important)
        Node* pred0 = preds[0];
        Node* succ0 = succs[0];
        
        // Critical: CAS must verify successor is still expected
        if (!atomic_compare_exchange_strong(&pred0->next[0], &succ0, newNode)) {
            // Failed - someone modified, cleanup and retry
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        // Level 0 succeeded! Now link upper levels (best effort)
        for (int level = 1; level <= topLevel; level++) {
            int level_attempts = 0;
            while (level_attempts++ < 10) {
                Node* pred = preds[level];
                Node* succ = succs[level];
                
                // Update newNode's next pointer in case something changed
                atomic_store(&newNode->next[level], succ);
                
                if (atomic_compare_exchange_strong(&pred->next[level], &succ, newNode)) {
                    break; // Success at this level
                }
                
                // Failed, re-search for correct position at this level
                pred = list->head;
                for (int l = list->maxLevel; l >= level; l--) {
                    Node* curr = atomic_load(&pred->next[l]);
                    while (curr != NULL && curr != list->tail && curr->key < key) {
                        bool marked = atomic_load(&curr->marked);
                        if (!marked) {
                            pred = curr;
                        }
                        curr = atomic_load(&curr->next[l]);
                    }
                    if (l == level) {
                        preds[level] = pred;
                        succs[level] = (curr != NULL) ? curr : list->tail;
                    }
                }
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
    
    return false;
}

// Lock-free delete - logical only
bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    int attempts = 0;
    const int MAX_ATTEMPTS = 100;
    
    while (attempts++ < MAX_ATTEMPTS) {
        Node* victim = find_node_safe(list, key, preds, succs);
        
        if (victim == NULL) {
            return false; // Not found
        }
        
        // Try to mark it
        bool expected = false;
        if (atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            atomic_fetch_sub(&list->size, 1);
            // Note: We don't physically remove to avoid race conditions
            // Marked nodes are skipped during traversal
            return true;
        }
        // Someone else marked it, they deleted it
        return false;
    }
    
    return false;
}

// Lock-free contains
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != NULL && curr != list->tail) {
            bool marked = atomic_load(&curr->marked);
            if (!marked && curr->key >= key) {
                if (level == 0 && curr->key == key) {
                    return true;
                }
                break;
            }
            
            if (!marked && curr->key < key) {
                pred = curr;
            }
            curr = atomic_load(&curr->next[level]);
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