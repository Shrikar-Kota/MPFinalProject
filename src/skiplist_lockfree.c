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

// Find position for key
static bool find(SkipList* list, int key, Node* preds[], Node* succs[]) {
    bool marked = false;
    bool snip;
    Node* pred = NULL;
    Node* curr = NULL;
    Node* succ = NULL;

retry:
    pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        curr = atomic_load(&pred->next[level]);
        
        while (true) {
            if (curr == list->tail) {
                break;
            }
            
            succ = atomic_load(&curr->next[level]);
            marked = atomic_load(&curr->marked);
            
            // If marked, try to physically remove
            while (marked) {
                snip = atomic_compare_exchange_strong(&pred->next[level], &curr, succ);
                if (!snip) {
                    goto retry;
                }
                curr = atomic_load(&pred->next[level]);
                if (curr == list->tail) {
                    break;
                }
                succ = atomic_load(&curr->next[level]);
                marked = atomic_load(&curr->marked);
            }
            
            if (curr == list->tail) {
                break;
            }
            
            if (curr->key < key) {
                pred = curr;
                curr = succ;
            } else {
                break;
            }
        }
        
        preds[level] = pred;
        succs[level] = curr;
    }
    
    return (curr != list->tail && curr->key == key);
}

// Lock-free insert
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* newNode = NULL;
    int topLevel = random_level();
    
    while (true) {
        bool found = find(list, key, preds, succs);
        
        if (found) {
            // Key already exists and not marked
            if (!atomic_load(&succs[0]->marked)) {
                if (newNode != NULL) {
                    omp_destroy_lock(&newNode->lock);
                    free(newNode);
                }
                return false;
            }
            // If marked, continue and insert anyway
        }
        
        if (newNode == NULL) {
            newNode = create_node(key, value, topLevel);
            for (int level = 0; level <= topLevel; level++) {
                atomic_store(&newNode->next[level], succs[level]);
            }
        }
        
        // Try to insert at bottom level
        Node* pred = preds[0];
        Node* succ = succs[0];
        atomic_store(&newNode->next[0], succ);
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            continue; // Retry
        }
        
        // Successfully inserted at level 0, now insert at higher levels
        for (int level = 1; level <= topLevel; level++) {
            while (true) {
                pred = preds[level];
                succ = succs[level];
                atomic_store(&newNode->next[level], succ);
                
                if (atomic_compare_exchange_strong(&pred->next[level], &succ, newNode)) {
                    break;
                }
                
                // Failed, find again for this key
                find(list, key, preds, succs);
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
}

// Lock-free delete
bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* victim = NULL;
    
    while (true) {
        bool found = find(list, key, preds, succs);
        
        if (!found) {
            return false;
        }
        
        victim = succs[0];
        
        // Check if already marked
        bool marked = atomic_load(&victim->marked);
        if (marked) {
            return false;
        }
        
        // Try to mark the victim
        bool expected = false;
        if (!atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            continue; // Someone else marked it or we failed, retry
        }
        
        // Successfully marked, now try to physically remove (best effort)
        for (int level = victim->topLevel; level >= 0; level--) {
            Node* succ = atomic_load(&victim->next[level]);
            // Try to unlink, but don't retry if it fails
            // The find() will clean it up later
            atomic_compare_exchange_strong(&preds[level]->next[level], &victim, succ);
        }
        
        atomic_fetch_sub(&list->size, 1);
        return true;
    }
}

// Lock-free contains
bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* curr = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        curr = atomic_load(&curr->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            curr = atomic_load(&curr->next[level]);
        }
        
        if (level == 0) {
            if (curr != list->tail && curr->key == key) {
                return !atomic_load(&curr->marked);
            }
            return false;
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