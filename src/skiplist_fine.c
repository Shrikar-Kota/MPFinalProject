#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>

// ------------------------------------------------------------------------
// Epoch-Based Memory Reclamation (EBR) - Corrected
// ------------------------------------------------------------------------

typedef struct RetiredNode {
    Node* node;
    struct RetiredNode* next;
} RetiredNode;

typedef struct {
    atomic_int global_epoch;
    atomic_int active_readers[3];
    // We need 3 separate limbo lists:
    // [0]: Nodes retired while global_epoch % 3 == 0
    // [1]: Nodes retired while global_epoch % 3 == 1
    // [2]: Nodes retired while global_epoch % 3 == 2
    _Atomic(RetiredNode*) limbo[3];
} EpochManager;

static EpochManager em;

void init_epoch_manager(void) {
    atomic_init(&em.global_epoch, 0);
    for (int i = 0; i < 3; i++) {
        atomic_init(&em.active_readers[i], 0);
        atomic_init(&em.limbo[i], NULL);
    }
}

static int epoch_enter(void) {
    int e = atomic_load(&em.global_epoch) % 3;
    atomic_fetch_add(&em.active_readers[e], 1);
    return e;
}

static void epoch_exit(int e) {
    atomic_fetch_sub(&em.active_readers[e], 1);
}

static void retire_node(Node* node) {
    int current_epoch = atomic_load(&em.global_epoch) % 3;
    
    RetiredNode* r_node = (RetiredNode*)malloc(sizeof(RetiredNode));
    r_node->node = node;
    
    // Push to the limbo list for the CURRENT epoch
    RetiredNode* old_head;
    do {
        old_head = atomic_load(&em.limbo[current_epoch]);
        r_node->next = old_head;
    } while (!atomic_compare_exchange_weak(&em.limbo[current_epoch], &old_head, r_node));
}

static void try_gc(void) {
    int current_global = atomic_load(&em.global_epoch);
    int current_idx = current_global % 3;
    
    // We want to clean up the "oldest" unsafe epoch.
    // If current is 2, (2+1)%3 = 0 is potentially ready to be cleaned.
    // Logic: If we are in epoch E, then E-1 is questionable, but E-2 is DEFINITELY safe 
    // IF and ONLY IF there are no readers left in E-1.
    
    int check_idx = (current_idx + 1) % 3; // The "next" epoch is actually the oldest in circular buffer
    
    // If no readers are lingering in the previous epoch...
    if (atomic_load(&em.active_readers[(current_global + 2) % 3]) == 0) {
        
        // Attempt to advance global epoch
        if (atomic_compare_exchange_strong(&em.global_epoch, &current_global, current_global + 1)) {
            
            // Now it is safe to free the list from 'check_idx' (which is now 2 epochs ago)
            RetiredNode* curr = atomic_exchange(&em.limbo[check_idx], NULL);
            
            while (curr) {
                RetiredNode* next = curr->next;
                omp_destroy_lock(&curr->node->lock); // Destroy lock before free
                free(curr->node);
                free(curr);
                curr = next;
            }
        }
    }
}

// ------------------------------------------------------------------------
// Fine-Grained (Optimistic) Implementation
// ------------------------------------------------------------------------

SkipList* skiplist_create_fine(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    
    // Initialize Head/Tail
    list->head = create_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = create_node(INT_MAX, 0, MAX_LEVEL);
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
        atomic_store(&list->tail->next[i], NULL); // Tail points to NULL
    }
    
    // Initialize locks explicitly for sentinels
    omp_init_lock(&list->head->lock);
    omp_init_lock(&list->tail->lock);

    list->maxLevel = MAX_LEVEL;
    atomic_init(&list->size, 0);
    
    init_epoch_manager();
    return list;
}

/**
 * Optimistic Search:
 * 1. Search without locks.
 * 2. Return preds[] and succs[].
 * 3. Caller must Lock and Validate.
 */
static void find_optimistic(SkipList* list, int key, Node** preds, Node** succs) {
    Node* pred = list->head;
    for (int level = MAX_LEVEL; level >= 0; level--) {
        Node* curr = atomic_load(&pred->next[level]);
        while (curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
        preds[level] = pred;
        succs[level] = curr;
    }
}

/**
 * Validation:
 * Ensure that 'pred' still points to 'succ' and 'pred' isn't marked for deletion.
 */
static bool validate_link(Node* pred, Node* succ, int level) {
    return !atomic_load(&pred->marked) && 
           !atomic_load(&succ->marked) &&
           atomic_load(&pred->next[level]) == succ;
}

bool skiplist_insert_fine(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        // 1. Search without locks
        int epoch = epoch_enter();
        find_optimistic(list, key, preds, succs);
        epoch_exit(epoch);
        
        if (succs[0]->key == key) {
            return false; // Key exists
        }
        
        // 2. Lock Predecessors and Validate
        // Fine-grained strategy: Lock only the nodes we modify.
        // We start with Level 0 (Linearization point).
        
        omp_set_lock(&preds[0]->lock);
        
        if (!validate_link(preds[0], succs[0], 0)) {
            omp_unset_lock(&preds[0]->lock);
            continue; // Validation failed, retry
        }
        
        // 3. Create Node and Link Level 0
        int topLevel = random_level();
        Node* newNode = create_node(key, value, topLevel);
        omp_init_lock(&newNode->lock); // Initialize lock for new node
        
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // Physical link at level 0
        atomic_store(&preds[0]->next[0], newNode);
        omp_unset_lock(&preds[0]->lock);
        
        atomic_fetch_add(&list->size, 1);
        
        // 4. Link upper levels
        // We do this incrementally. If validation fails, we re-search.
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Validate
                if (!validate_link(preds[i], succs[i], i)) {
                    omp_unset_lock(&preds[i]->lock);
                    // Re-search ONLY for this level to find new pred
                    epoch = epoch_enter();
                    // Simplified: fast search from head for this level
                    Node* pred = list->head;
                    Node* curr = atomic_load(&pred->next[i]);
                    while(curr->key < key) {
                        pred = curr;
                        curr = atomic_load(&pred->next[i]);
                    }
                    preds[i] = pred;
                    succs[i] = curr;
                    epoch_exit(epoch);
                    continue; // Retry lock
                }
                
                // Link
                atomic_store(&newNode->next[i], succs[i]); // Update next just in case
                atomic_store(&preds[i]->next[i], newNode);
                omp_unset_lock(&preds[i]->lock);
                break;
            }
        }
        
        return true;
    }
}

bool skiplist_delete_fine(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        int epoch = epoch_enter();
        find_optimistic(list, key, preds, succs);
        epoch_exit(epoch);
        
        Node* victim = succs[0];
        
        if (victim->key != key || victim == list->tail) {
            return false;
        }
        
        // 1. Lock Victim
        omp_set_lock(&victim->lock);
        if (atomic_load(&victim->marked)) {
            omp_unset_lock(&victim->lock);
            return false; // Already deleted
        }
        
        // 2. Mark Victim (Logical Deletion)
        atomic_store(&victim->marked, true);
        omp_unset_lock(&victim->lock);
        
        // 3. Lock Predecessors and Unlink (Physical Deletion)
        // We traverse levels top-down or bottom-up, usually top-down for delete
        for (int i = victim->topLevel; i >= 0; i--) {
            while (true) {
                omp_set_lock(&preds[i]->lock);
                
                // Validate: Pred must still point to victim
                if (atomic_load(&preds[i]->next[i]) != victim) {
                    omp_unset_lock(&preds[i]->lock);
                    
                    // Re-find predecessor for this level
                    epoch = epoch_enter();
                    Node* p = list->head;
                    Node* c = atomic_load(&p->next[i]);
                    while(c->key < key) { p = c; c = atomic_load(&p->next[i]); }
                    preds[i] = p;
                    epoch_exit(epoch);
                    continue; 
                }
                
                // Unlink
                atomic_store(&preds[i]->next[i], atomic_load(&victim->next[i]));
                omp_unset_lock(&preds[i]->lock);
                break;
            }
        }
        
        atomic_fetch_sub(&list->size, 1);
        
        // 4. Retire Memory
        retire_node(victim);
        try_gc();
        
        return true;
    }
}

bool skiplist_contains_fine(SkipList* list, int key) {
    int epoch = epoch_enter();
    
    Node* pred = list->head;
    Node* curr = NULL;
    
    for (int level = MAX_LEVEL; level >= 0; level--) {
        curr = atomic_load(&pred->next[level]);
        while (curr->key < key) {
            pred = curr;
            curr = atomic_load(&pred->next[level]);
        }
    }
    
    bool found = (curr->key == key && !atomic_load(&curr->marked));
    
    epoch_exit(epoch);
    return found;
}

void skiplist_destroy_fine(SkipList* list) {
    // 1. Force cleanup of all limbo lists (unsafe, assumes single thread)
    for(int i=0; i<3; i++) {
        RetiredNode* r = atomic_load(&em.limbo[i]);
        while(r) {
            RetiredNode* next = r->next;
            omp_destroy_lock(&r->node->lock);
            free(r->node);
            free(r);
            r = next;
        }
    }

    Node* curr = list->head;
    while (curr) {
        Node* next = atomic_load(&curr->next[0]);
        omp_destroy_lock(&curr->lock);
        free(curr);
        curr = next;
    }
    free(list);
}