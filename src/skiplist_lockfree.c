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

// Helper function to search
static Node* search(SkipList* list, int key, Node* preds[], Node* succs[]) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            // Check if marked
            bool marked = atomic_load(&curr->marked);
            if (!marked) {
                pred = curr;
            }
            curr = atomic_load(&curr->next[level]);
        }
        
        preds[level] = pred;
        succs[level] = curr;
    }
    
    return succs[0];
}

// Lock-free insert
bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        Node* found = search(list, key, preds, succs);
        
        // Key exists and not marked
        if (found != list->tail && found->key == key) {
            bool marked = atomic_load(&found->marked);
            if (!marked) {
                return false;
            }
        }
        
        // Create new node
        int newLevel = random_level();
        Node* newNode = create_node(key, value, newLevel);
        
        // Link pointers
        for (int level = 0; level <= newLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        // Try CAS at bottom level
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            // CAS failed, free and retry
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        // Bottom level successful, link upper levels
        for (int level = 1; level <= newLevel; level++) {
            while (true) {
                pred = preds[level];
                succ = succs[level];
                
                if (atomic_compare_exchange_strong(&pred->next[level], &succ, newNode)) {
                    break;
                }
                
                // Failed, search again for this level only
                Node* p = list->head;
                for (int l = list->maxLevel; l >= level; l--) {
                    Node* curr = atomic_load(&p->next[l]);
                    while (curr != list->tail && curr->key < key) {
                        bool marked = atomic_load(&curr->marked);
                        if (!marked) {
                            p = curr;
                        }
                        curr = atomic_load(&curr->next[l]);
                    }
                    if (l == level) {
                        preds[level] = p;
                        succs[level] = curr;
                    }
                }
            }
        }
        
        atomic_fetch_add(&list->size, 1);
        return true;
    }
}

// Lock-free delete with logical deletion only (no physical removal)
bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        Node* victim = search(list, key, preds, succs);
        
        // Not found
        if (victim == list->tail || victim->key != key) {
            return false;
        }
        
        // Already marked
        bool marked = atomic_load(&victim->marked);
        if (marked) {
            return false;
        }
        
        // Try to mark it
        bool expected = false;
        if (atomic_compare_exchange_strong(&victim->marked, &expected, true)) {
            atomic_fetch_sub(&list->size, 1);
            // Note: We don't free the node to avoid use-after-free
            // In production, use hazard pointers or epoch-based reclamation
            return true;
        }
    }
}

// Lock-free contains
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

// Destroy (safe because no concurrent access during destroy)
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