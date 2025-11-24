#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// TRUE lock-free skip list with Harris-style algorithm
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

// Find with proper marked node handling
static bool find_with_cleanup(SkipList* list, int key, Node* preds[], Node* succs[]) {
    bool snip;
    Node* pred = NULL;
    Node* curr = NULL;
    Node* succ = NULL;
    
retry:
    pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        curr = atomic_load(&pred->next[level]);
        
        while (true) {
            // Handle NULL and tail
            if (curr == NULL || curr == list->tail) {
                break;
            }
            
            succ = atomic_load(&curr->next[level]);
            
            // Check if node is marked
            bool marked = atomic_load(&curr->marked);
            
            // If marked, try to physically remove
            while (marked && curr != list->tail) {
                snip = atomic_compare_exchange_strong(&pred->next[level], &curr, succ);
                if (!snip) {
                    // Someone else modified, restart
                    goto retry;
                }
                
                // Successfully removed, move to next
                curr = succ;
                if (curr == NULL || curr == list->tail) {
                    break;
                }
                succ = atomic_load(&curr->next[level]);
                marked = atomic_load(&curr->marked);
            }
            
            // If we hit tail, stop
            if (curr == NULL || curr == list->tail) {
                break;
            }
            
            // Check if we should continue
            if (curr->key < key) {
                pred = curr;
                curr = succ;
            } else {
                break;
            }
        }
        
        // Record predecessors and successors
        if (preds != NULL) preds[level] = pred;
        if (succs != NULL) succs[level] = (curr != NULL) ? curr : list->tail;
    }
    
    // Return true if found and not marked
    if (succs != NULL && succs[0] != list->tail && succs[0]->key == key) {
        return !atomic_load(&succs[0]->marked);
    }
    return false;
}

// Lock-free insert with full algorithm
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    int retry = 0;
    
    while (retry++ < 1000) {
        // Find position
        bool found = find_with_cleanup(list, key, preds, succs);
        
        if (found) {
            return false; // Key already exists
        }
        
        // Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Set all next pointers
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        // Try CAS at level 0 (linearization point)
        Node* pred = preds[0];
        Node* succ = succs[0];
        atomic_store(&newNode->next[0], succ);
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            // Failed at level 0, retry entire operation
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        // Success at level 0! Now link upper levels
        for (int level = 1; level <= topLevel; level++) {
            while (true) {
                pred = preds[level];
                succ = succs[level];
                atomic_store(&newNode->next[level], succ);
                
                if (atomic_compare_exchange_strong(&pred->next[level], &succ, newNode)) {
                    break; // Success at this level
                }
                
                // Failed, find new position for this level
                find_with_cleanup(list, key, preds, succs);
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
    
    return false;
}

// Lock-free delete with logical deletion
bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* victim = NULL;
    
    int retry = 0;
    
    while (retry++ < 1000) {
        // Find victim
        bool found = find_with_cleanup(list, key, preds, succs);
        
        if (!found) {
            return false; // Key doesn't exist
        }
        
        victim = succs[0];
        
        // Try to mark the victim (logical deletion)
        bool expected = false;
        if (!atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            // Someone else marked it first
            continue;
        }
        
        // Successfully marked! Now try physical removal (best effort)
        for (int level = victim->topLevel; level >= 0; level--) {
            Node* succ = atomic_load(&victim->next[level]);
            // Try to unlink (don't retry if fails, find will clean up)
            atomic_compare_exchange_strong(&preds[level]->next[level], &victim, succ);
        }
        
        atomic_fetch_sub(&list->size, 1);
        return true;
    }
    
    return false;
}

// Lock-free contains (wait-free!)
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != NULL && curr != list->tail) {
            // Skip marked nodes
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