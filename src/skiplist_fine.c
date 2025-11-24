#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>

// TRUE fine-grained with hand-over-hand locking
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

// Lock nodes in address order to prevent deadlock
static void lock_nodes_ordered(Node** nodes, int count, Node** locked, int* locked_count) {
    // Collect unique nodes
    Node* unique[MAX_LEVEL + 2];
    int unique_cnt = 0;
    
    for (int i = 0; i < count; i++) {
        if (nodes[i] == NULL) continue;
        
        bool found = false;
        for (int j = 0; j < unique_cnt; j++) {
            if (unique[j] == nodes[i]) {
                found = true;
                break;
            }
        }
        if (!found) {
            unique[unique_cnt++] = nodes[i];
        }
    }
    
    // Sort by address (consistent global order)
    for (int i = 0; i < unique_cnt - 1; i++) {
        for (int j = i + 1; j < unique_cnt; j++) {
            if ((uintptr_t)unique[i] > (uintptr_t)unique[j]) {
                Node* temp = unique[i];
                unique[i] = unique[j];
                unique[j] = temp;
            }
        }
    }
    
    // Lock all in order
    for (int i = 0; i < unique_cnt; i++) {
        omp_set_lock(&unique[i]->lock);
        locked[i] = unique[i];
    }
    
    *locked_count = unique_cnt;
}

// Unlock nodes
static void unlock_nodes_ordered(Node** locked, int count) {
    for (int i = 0; i < count; i++) {
        if (locked[i] != NULL) {
            omp_unset_lock(&locked[i]->lock);
        }
    }
}

// Advanced optimistic insert with hand-over-hand locking
bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* locked[MAX_LEVEL + 1];
    int locked_count = 0;
    
    int retry = 0;
    
    while (retry++ < 1000) {
        // Phase 1: Optimistic traversal (no locks)
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
        
        // Check if key exists
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false;
        }
        
        // Phase 2: Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Set next pointers
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
        }
        
        // Phase 3: Lock predecessors in address order
        lock_nodes_ordered(preds, topLevel + 1, locked, &locked_count);
        
        // Phase 4: Validate - check nothing changed
        bool valid = true;
        for (int level = 0; level <= topLevel; level++) {
            Node* curr = atomic_load(&preds[level]->next[level]);
            if (curr != succs[level]) {
                valid = false;
                break;
            }
            // Double-check key doesn't exist
            if (curr != list->tail && curr->key == key) {
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            // Validation failed, unlock and retry
            unlock_nodes_ordered(locked, locked_count);
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        // Phase 5: Insert at all levels (we have locks)
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&preds[level]->next[level], newNode);
        }
        
        // Mark as fully linked NOW
        atomic_store(&newNode->fully_linked, true);
        
        // Phase 6: Unlock and return success
        unlock_nodes_ordered(locked, locked_count);
        atomic_fetch_add(&list->size, 1);
        return true;
    }
    
    return false;
}

// Advanced optimistic delete with hand-over-hand locking
bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* victim = NULL;
    Node* to_lock[MAX_LEVEL + 2];
    Node* locked[MAX_LEVEL + 2];
    int locked_count = 0;
    
    int retry = 0;
    
    while (retry++ < 1000) {
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
        
        // Phase 2: Build lock set (preds + victim)
        for (int i = 0; i <= victim->topLevel; i++) {
            to_lock[i] = preds[i];
        }
        to_lock[victim->topLevel + 1] = victim;
        
        // Phase 3: Lock all in address order
        lock_nodes_ordered(to_lock, victim->topLevel + 2, locked, &locked_count);
        
        // Phase 4: Validate
        bool valid = true;
        for (int level = 0; level <= victim->topLevel; level++) {
            Node* curr = atomic_load(&preds[level]->next[level]);
            if (curr != victim) {
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            // Validation failed, unlock and retry
            unlock_nodes_ordered(locked, locked_count);
            continue;
        }
        
        // Phase 5: Unlink victim from all levels
        for (int level = 0; level <= victim->topLevel; level++) {
            Node* succ = atomic_load(&victim->next[level]);
            atomic_store(&preds[level]->next[level], succ);
        }
        
        // Phase 6: Unlock, update size, free
        unlock_nodes_ordered(locked, locked_count);
        atomic_fetch_sub(&list->size, 1);
        
        omp_destroy_lock(&victim->lock);
        free(victim);
        return true;
    }
    
    return false;
}

// Lock-free contains (the fine-grained advantage!)
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