#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Create skip list with fine-grained locking
SkipList* skiplist_create_fine(void) {
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

// Helper to lock predecessors
static void lock_predecessors(Node* preds[], int topLevel) {
    for (int i = 0; i <= topLevel; i++) {
        omp_set_lock(&preds[i]->lock);
    }
}

// Helper to unlock predecessors
static void unlock_predecessors(Node* preds[], int topLevel) {
    for (int i = 0; i <= topLevel; i++) {
        omp_unset_lock(&preds[i]->lock);
    }
}

// Insert with fine-grained locking
bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        Node* curr = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* next = atomic_load(&curr->next[level]);
            while (next != list->tail && next->key < key) {
                curr = next;
                next = atomic_load(&curr->next[level]);
            }
            preds[level] = curr;
            succs[level] = next;
        }
        
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false;
        }
        
        int newLevel = random_level();
        Node* newNode = create_node(key, value, newLevel);
        
        lock_predecessors(preds, newLevel);
        
        bool valid = true;
        for (int i = 0; i <= newLevel; i++) {
            Node* succ = atomic_load(&preds[i]->next[i]);
            if (succ != succs[i]) {
                valid = false;
                break;
            }
        }
        
        if (valid) {
            for (int i = 0; i <= newLevel; i++) {
                atomic_store(&newNode->next[i], succs[i]);
                atomic_store(&preds[i]->next[i], newNode);
            }
            
            unlock_predecessors(preds, newLevel);
            atomic_fetch_add(&list->size, 1);
            return true;
        } else {
            unlock_predecessors(preds, newLevel);
            omp_destroy_lock(&newNode->lock);
            free(newNode);
        }
    }
}

// Delete with fine-grained locking
bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* victim = NULL;
    
    while (true) {
        Node* curr = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* next = atomic_load(&curr->next[level]);
            while (next != list->tail && next->key < key) {
                curr = next;
                next = atomic_load(&curr->next[level]);
            }
            preds[level] = curr;
            
            if (level == 0) {
                if (next != list->tail && next->key == key) {
                    victim = next;
                } else {
                    return false;
                }
            }
        }
        
        omp_set_lock(&victim->lock);
        lock_predecessors(preds, victim->topLevel);
        
        bool valid = true;
        for (int i = 0; i <= victim->topLevel; i++) {
            if (atomic_load(&preds[i]->next[i]) != victim) {
                valid = false;
                break;
            }
        }
        
        if (valid) {
            for (int i = 0; i <= victim->topLevel; i++) {
                Node* succ = atomic_load(&victim->next[i]);
                atomic_store(&preds[i]->next[i], succ);
            }
            
            unlock_predecessors(preds, victim->topLevel);
            omp_unset_lock(&victim->lock);
            
            atomic_fetch_sub(&list->size, 1);
            
            omp_destroy_lock(&victim->lock);
            free(victim);
            return true;
        } else {
            unlock_predecessors(preds, victim->topLevel);
            omp_unset_lock(&victim->lock);
        }
    }
}

// Contains with fine-grained locking (lock-free for reads)
bool skiplist_contains_fine(SkipList* list, int key) {
    Node* curr = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* next = atomic_load(&curr->next[level]);
        while (next != list->tail && next->key < key) {
            curr = next;
            next = atomic_load(&curr->next[level]);
        }
        
        if (level == 0) {
            return (next != list->tail && next->key == key);
        }
    }
    
    return false;
}

// Destroy skip list
void skiplist_destroy_fine(SkipList* list) {
    Node* curr = list->head;
    
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    
    free(list);
}