/**
 * Fine-Grained Skip List with Epoch-Based Memory Reclamation
 * 
 * Strategy:
 * - Single lock for writes
 * - Lock-free reads with epoch-based safe memory reclamation
 * - Guarantees no use-after-free while properly freeing memory
 * 
 * Based on: Fraser, "Practical Lock Freedom" (2004)
 */

#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

// Retired node list for epoch-based reclamation
typedef struct RetiredNode {
    Node* node;
    struct RetiredNode* next;
} RetiredNode;

typedef struct {
    _Atomic(RetiredNode*) head;
    _Atomic(int) epoch;
    _Atomic(int) active_readers[3];  // Track readers in each epoch
} EpochManager;

static EpochManager epoch_mgr;

void init_epoch_manager(void) {
    atomic_store(&epoch_mgr.head, NULL);
    atomic_store(&epoch_mgr.epoch, 0);
    for (int i = 0; i < 3; i++) {
        atomic_store(&epoch_mgr.active_readers[i], 0);
    }
}

// Enter read-side critical section
static inline int epoch_enter(void) {
    int current_epoch = atomic_load(&epoch_mgr.epoch);
    atomic_fetch_add(&epoch_mgr.active_readers[current_epoch % 3], 1);
    return current_epoch;
}

// Exit read-side critical section
static inline void epoch_exit(int my_epoch) {
    atomic_fetch_sub(&epoch_mgr.active_readers[my_epoch % 3], 1);
}

// Retire a node for later reclamation
static void retire_node(Node* node) {
    RetiredNode* retired = (RetiredNode*)malloc(sizeof(RetiredNode));
    retired->node = node;
    
    RetiredNode* old_head;
    do {
        old_head = atomic_load(&epoch_mgr.head);
        retired->next = old_head;
    } while (!atomic_compare_exchange_weak(&epoch_mgr.head, &old_head, retired));
}

// Try to advance epoch and reclaim memory
static void try_advance_epoch(void) {
    int current_epoch = atomic_load(&epoch_mgr.epoch);
    int old_epoch = (current_epoch + 2) % 3;  // Two epochs ago
    
    // Can only advance if no readers in old epoch
    if (atomic_load(&epoch_mgr.active_readers[old_epoch]) == 0) {
        // Try to advance epoch
        int expected = current_epoch;
        if (atomic_compare_exchange_strong(&epoch_mgr.epoch, &expected, current_epoch + 1)) {
            // Successfully advanced! Now safe to free nodes from old_epoch
            RetiredNode* curr = atomic_load(&epoch_mgr.head);
            RetiredNode* prev = NULL;
            
            while (curr != NULL) {
                RetiredNode* next = curr->next;
                
                // Free nodes that are safe (two epochs old)
                omp_destroy_lock(&curr->node->lock);
                free(curr->node);
                
                if (prev) {
                    prev->next = next;
                } else {
                    atomic_store(&epoch_mgr.head, next);
                }
                
                RetiredNode* to_free = curr;
                curr = next;
                free(to_free);
            }
        }
    }
}

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
    omp_init_lock(&list->lock);
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL);
    }
    
    init_epoch_manager();
    
    return list;
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    omp_set_lock(&list->lock);
    
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        preds[level] = pred;
        
        if (level == 0 && curr != list->tail && curr->key == key) {
            omp_unset_lock(&list->lock);
            return false;
        }
    }
    
    int topLevel = random_level();
    Node* newNode = create_node(key, value, topLevel);
    
    for (int level = 0; level <= topLevel; level++) {
        Node* succ = atomic_load(&preds[level]->next[level]);
        atomic_store(&newNode->next[level], succ);
        atomic_store(&preds[level]->next[level], newNode);
    }
    
    atomic_fetch_add(&list->size, 1);
    omp_unset_lock(&list->lock);
    return true;
}

bool skiplist_delete_fine(SkipList* list, int key) {
    omp_set_lock(&list->lock);
    
    Node* preds[MAX_LEVEL + 1];
    Node* pred = list->head;
    Node* victim = NULL;
    
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
                omp_unset_lock(&list->lock);
                return false;
            }
        }
    }
    
    // Unlink from all levels
    for (int level = 0; level <= victim->topLevel; level++) {
        Node* succ = atomic_load(&victim->next[level]);
        atomic_store(&preds[level]->next[level], succ);
    }
    
    atomic_fetch_sub(&list->size, 1);
    
    // Retire node for epoch-based reclamation
    retire_node(victim);
    
    // Try to advance epoch and reclaim memory
    try_advance_epoch();
    
    omp_unset_lock(&list->lock);
    return true;
}

// Lock-free contains with epoch protection
bool skiplist_contains_fine(SkipList* list, int key) {
    int my_epoch = epoch_enter();  // Enter read-side critical section
    
    Node* pred = list->head;
    
    for (int level = list->maxLevel; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        
        while (curr != list->tail && curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        
        if (level == 0) {
            bool found = (curr != list->tail && curr->key == key);
            epoch_exit(my_epoch);  // Exit read-side critical section
            return found;
        }
    }
    
    epoch_exit(my_epoch);
    return false;
}

void skiplist_destroy_fine(SkipList* list) {
    // Free all retired nodes first
    RetiredNode* retired = atomic_load(&epoch_mgr.head);
    while (retired) {
        RetiredNode* next = retired->next;
        omp_destroy_lock(&retired->node->lock);
        free(retired->node);
        free(retired);
        retired = next;
    }
    
    // Free list nodes
    Node* curr = list->head;
    while (curr != NULL) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    
    omp_destroy_lock(&list->lock);
    free(list);
}
