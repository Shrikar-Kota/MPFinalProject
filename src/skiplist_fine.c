#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>

// True fine-grained skip list with safe locking
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

// Safely lock unique nodes in address order
static int lock_nodes_safe(Node** nodes, int count, Node** locked_out, int* locked_count_out) {
    // Extract unique nodes
    Node* unique[MAX_LEVEL + 2];
    int unique_count = 0;
    
    for (int i = 0; i < count; i++) {
        if (nodes[i] == NULL) continue;
        
        bool found = false;
        for (int j = 0; j < unique_count; j++) {
            if (unique[j] == nodes[i]) {
                found = true;
                break;
            }
        }
        if (!found) {
            unique[unique_count++] = nodes[i];
        }
    }
    
    // Sort by address for consistent ordering
    for (int i = 0; i < unique_count - 1; i++) {
        for (int j = i + 1; j < unique_count; j++) {
            if ((uintptr_t)unique[i] > (uintptr_t)unique[j]) {
                Node* temp = unique[i];
                unique[i] = unique[j];
                unique[j] = temp;
            }
        }
    }
    
    // Lock all unique nodes
    for (int i = 0; i < unique_count; i++) {
        omp_set_lock(&unique[i]->lock);
        locked_out[i] = unique[i];
    }
    
    *locked_count_out = unique_count;
    return unique_count;
}

// Unlock nodes
static void unlock_nodes_safe(Node** locked, int count) {
    for (int i = 0; i < count; i++) {
        if (locked[i] != NULL) {
            omp_unset_lock(&locked[i]->lock);
        }
    }
}

// Fine-grained insert
bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* locked[MAX_LEVEL + 1];
    int locked_count = 0;
    
    int retries = 0;
    const int MAX_RETRIES = 100;
    
    while (retries++ < MAX_RETRIES) {
        // Phase 1: Optimistic search
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != NULL && curr != list->tail && curr->key < key) {
                pred = curr;
                curr = atomic_load(&pred->next[level]);
            }
            
            preds[level] = pred;
            succs[level] = (curr != NULL) ? curr : list->tail;
        }
        
        // Check if exists
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false;
        }
        
        // Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Lock predecessors
        lock_nodes_safe(preds, topLevel + 1, locked, &locked_count);
        
        // Validate
        bool valid = true;
        for (int level = 0; level <= topLevel; level++) {
            Node* curr = atomic_load(&preds[level]->next[level]);
            if (curr != succs[level]) {
                valid = false;
                break;
            }
            if (curr != list->tail && curr->key == key) {
                valid = false;
                break;
            }
        }
        
        if (valid) {
            // Insert
            for (int level = 0; level <= topLevel; level++) {
                atomic_store(&newNode->next[level], succs[level]);
                atomic_store(&preds[level]->next[level], newNode);
            }
            
            unlock_nodes_safe(locked, locked_count);
            atomic_fetch_add(&list->size, 1);
            return true;
        }
        
        // Failed validation
        unlock_nodes_safe(locked, locked_count);
        omp_destroy_lock(&newNode->lock);
        free(newNode);
    }
    
    return false;
}

// Fine-grained delete
bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* victim = NULL;
    Node* to_lock[MAX_LEVEL + 2];
    Node* locked[MAX_LEVEL + 2];
    int locked_count = 0;
    
    int retries = 0;
    const int MAX_RETRIES = 100;
    
    while (retries++ < MAX_RETRIES) {
        // Phase 1: Optimistic search
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != NULL && curr != list->tail && curr->key < key) {
                pred = curr;
                curr = atomic_load(&pred->next[level]);
            }
            
            preds[level] = pred;
            
            if (level == 0) {
                if (curr != NULL && curr != list->tail && curr->key == key) {
                    victim = curr;
                } else {
                    return false;
                }
            }
        }
        
        // Build lock set
        for (int i = 0; i <= victim->topLevel; i++) {
            to_lock[i] = preds[i];
        }
        to_lock[victim->topLevel + 1] = victim;
        
        // Lock all
        lock_nodes_safe(to_lock, victim->topLevel + 2, locked, &locked_count);
        
        // Validate
        bool valid = true;
        for (int level = 0; level <= victim->topLevel; level++) {
            if (atomic_load(&preds[level]->next[level]) != victim) {
                valid = false;
                break;
            }
        }
        
        if (valid) {
            // Delete
            for (int level = 0; level <= victim->topLevel; level++) {
                Node* succ = atomic_load(&victim->next[level]);
                atomic_store(&preds[level]->next[level], succ);
            }
            
            unlock_nodes_safe(locked, locked_count);
            atomic_fetch_sub(&list->size, 1);
            
            omp_destroy_lock(&victim->lock);
            free(victim);
            return true;
        }
        
        // Failed validation
        unlock_nodes_safe(locked, locked_count);
    }
    
    return false;
}

// Lock-free contains
bool skiplist_contains_fine(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != NULL && curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        if (level == 0) {
            return (curr != NULL && curr != list->tail && curr->key == key);
        }
    }
    
    return false;
}

// Destroy
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