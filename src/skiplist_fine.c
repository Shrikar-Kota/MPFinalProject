#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// True fine-grained skip list with hand-over-hand locking
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

// Lock a node set, avoiding duplicates
static void lock_node_set(Node** nodes, int count) {
    // First, collect unique nodes
    Node* unique[MAX_LEVEL + 1];
    int unique_count = 0;
    
    for (int i = 0; i < count; i++) {
        bool is_unique = true;
        for (int j = 0; j < unique_count; j++) {
            if (unique[j] == nodes[i]) {
                is_unique = false;
                break;
            }
        }
        if (is_unique) {
            unique[unique_count++] = nodes[i];
        }
    }
    
    // Sort by address to avoid deadlock (consistent lock ordering)
    for (int i = 0; i < unique_count - 1; i++) {
        for (int j = i + 1; j < unique_count; j++) {
            if ((uintptr_t)unique[i] > (uintptr_t)unique[j]) {
                Node* temp = unique[i];
                unique[i] = unique[j];
                unique[j] = temp;
            }
        }
    }
    
    // Lock in order
    for (int i = 0; i < unique_count; i++) {
        omp_set_lock(&unique[i]->lock);
    }
}

// Unlock a node set, avoiding duplicates
static void unlock_node_set(Node** nodes, int count) {
    // Collect unique nodes
    Node* unique[MAX_LEVEL + 1];
    int unique_count = 0;
    
    for (int i = 0; i < count; i++) {
        bool is_unique = true;
        for (int j = 0; j < unique_count; j++) {
            if (unique[j] == nodes[i]) {
                is_unique = false;
                break;
            }
        }
        if (is_unique) {
            unique[unique_count++] = nodes[i];
        }
    }
    
    // Unlock all unique nodes
    for (int i = 0; i < unique_count; i++) {
        omp_unset_lock(&unique[i]->lock);
    }
}

// True fine-grained insert with optimistic + locking phases
bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        // Phase 1: Optimistic traversal (no locks)
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != list->tail && curr->key < key) {
                pred = curr;
                curr = atomic_load(&pred->next[level]);
            }
            
            preds[level] = pred;
            succs[level] = curr;
        }
        
        // Check if key exists
        if (succs[0] != list->tail && succs[0]->key == key) {
            return false;
        }
        
        // Phase 2: Create new node
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        
        // Phase 3: Lock all predecessors (using proper lock ordering)
        lock_node_set(preds, topLevel + 1);
        
        // Phase 4: Validate - make sure nothing changed
        bool valid = true;
        for (int level = 0; level <= topLevel; level++) {
            Node* curr = atomic_load(&preds[level]->next[level]);
            if (curr != succs[level]) {
                valid = false;
                break;
            }
            // Double-check key doesn't exist now
            if (curr != list->tail && curr->key == key) {
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            // Validation failed, unlock and retry
            unlock_node_set(preds, topLevel + 1);
            omp_destroy_lock(&newNode->lock);
            free(newNode);
            continue;
        }
        
        // Phase 5: Insert (we have locks and validation passed)
        for (int level = 0; level <= topLevel; level++) {
            atomic_store(&newNode->next[level], succs[level]);
            atomic_store(&preds[level]->next[level], newNode);
        }
        
        // Phase 6: Unlock and return
        unlock_node_set(preds, topLevel + 1);
        atomic_fetch_add(&list->size, 1);
        return true;
    }
}

// True fine-grained delete
bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* victim = NULL;
    Node* lock_nodes[MAX_LEVEL + 2];  // preds + victim
    
    while (true) {
        // Phase 1: Optimistic search
        Node* pred = list->head;
        
        for (int level = list->maxLevel; level >= 0; level--) {
            Node* curr = atomic_load(&pred->next[level]);
            
            while (curr != list->tail && curr->key < key) {
                pred = curr;
                curr = atomic_load(&pred->next[level]);
            }
            
            preds[level] = pred;
            
            if (level == 0) {
                if (curr != list->tail && curr->key == key) {
                    victim = curr;
                } else {
                    return false; // Not found
                }
            }
        }
        
        // Phase 2: Build lock set (victim + all preds)
        for (int i = 0; i <= victim->topLevel; i++) {
            lock_nodes[i] = preds[i];
        }
        lock_nodes[victim->topLevel + 1] = victim;
        
        // Phase 3: Lock all (with proper ordering)
        lock_node_set(lock_nodes, victim->topLevel + 2);
        
        // Phase 4: Validate
        bool valid = true;
        for (int level = 0; level <= victim->topLevel; level++) {
            if (atomic_load(&preds[level]->next[level]) != victim) {
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            unlock_node_set(lock_nodes, victim->topLevel + 2);
            continue;
        }
        
        // Phase 5: Unlink victim
        for (int level = 0; level <= victim->topLevel; level++) {
            Node* succ = atomic_load(&victim->next[level]);
            atomic_store(&preds[level]->next[level], succ);
        }
        
        // Phase 6: Unlock and cleanup
        unlock_node_set(lock_nodes, victim->topLevel + 2);
        atomic_fetch_sub(&list->size, 1);
        
        omp_destroy_lock(&victim->lock);
        free(victim);
        return true;
    }
}

// Lock-free contains (read-only, no locks needed)
bool skiplist_contains_fine(SkipList* list, int key) {
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        if (level == 0) {
            return (curr != list->tail && curr->key == key);
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