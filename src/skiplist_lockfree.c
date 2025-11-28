#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>

// ------------------------------------------------------------------------
// Pointer Marking Macros (Harris Algorithm)
// We use the LSB of the 'next' pointer to mark nodes for deletion atomically.
// ------------------------------------------------------------------------
#define MARK_BIT 1
#define IS_MARKED(p)      ((uintptr_t)(p) & MARK_BIT)
#define GET_UNMARKED(p)   ((Node*)((uintptr_t)(p) & ~MARK_BIT))
#define GET_MARKED(p)     ((Node*)((uintptr_t)(p) | MARK_BIT))

/**
 * Internal helper to allocate a node specifically for the lock-free implementation.
 * We do not use the generic create_node if it initializes locks we don't need,
 * though we must respect the struct layout.
 */
static Node* alloc_node(int key, int value, int level) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) {
        perror("Failed to allocate node");
        exit(1);
    }
    
    node->key = key;
    node->value = value;
    node->topLevel = level;
    atomic_init(&node->marked, false);       // Redundant for Harris algo, but kept for struct consistency
    atomic_init(&node->fully_linked, false); // Optional helper
    
    // Initialize atomic pointers for the specific height
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_init(&node->next[i], NULL);
    }
    
    // We ignore node->lock for lock-free
    return node;
}

// ------------------------------------------------------------------------
// Core Helper: Find
// Traverses the list, physically removing (helping) marked nodes.
// Populates preds[] and succs[] for the target key.
// ------------------------------------------------------------------------
static bool find(SkipList* list, int key, Node** preds, Node** succs) {
    int bottomLevel = 0;
    
retry:
    while (true) {
        Node* pred = list->head;
        
        // Traverse from Top Level down to Bottom
        for (int level = MAX_LEVEL - 1; level >= bottomLevel; level--) {
            Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
            
            while (true) {
                // If we reached the tail, stop at this level
                if (curr == NULL) break; // Should theoretically hit tail sentinel, but safety check

                Node* succ = atomic_load(&curr->next[level]);
                
                // 1. HELPING: Check if the current node is marked for deletion
                while (IS_MARKED(succ)) {
                    // Physical deletion: Try to swing pred->next over curr
                    Node* unmarked_succ = GET_UNMARKED(succ);
                    
                    // CAS: If pred->next is still curr, make it unmarked_succ
                    if (!atomic_compare_exchange_strong(&pred->next[level], &curr, unmarked_succ)) {
                        // If CAS failed, the world changed. Retry the whole search.
                        goto retry;
                    }
                    
                    // Cleanup: In a production system with SMR (Hazard Pointers), 
                    // we would retire 'curr' here. 
                    
                    // Move to next
                    curr = unmarked_succ;
                    if (curr == NULL) break;
                    succ = atomic_load(&curr->next[level]);
                }
                
                if (curr == NULL) break; // Reached end of line

                // 2. Traversal Logic
                if (curr->key < key) {
                    pred = curr;
                    curr = GET_UNMARKED(succ);
                } else {
                    break; // Found position at this level
                }
            }
            
            // Store path
            preds[level] = pred;
            succs[level] = curr;
        }
        
        // Check if we found it at the bottom level
        return (succs[bottomLevel] != NULL && succs[bottomLevel]->key == key);
    }
}

// ------------------------------------------------------------------------
// Lock-Free Implementation
// ------------------------------------------------------------------------

SkipList* skiplist_create_lockfree(void) {
    SkipList* list = (SkipList*)malloc(sizeof(SkipList));
    if (!list) exit(1);
    
    // Create Sentinel Head (Min Key) and Tail (Max Key)
    // Note: Using INT_MIN/INT_MAX acts as sentinels
    list->head = alloc_node(INT_MIN, 0, MAX_LEVEL);
    list->tail = alloc_node(INT_MAX, 0, MAX_LEVEL);
    
    // Link Head to Tail at all levels
    for (int i = 0; i < MAX_LEVEL; i++) {
        atomic_store(&list->head->next[i], list->tail);
    }
    
    atomic_init(&list->size, 0);
    list->maxLevel = MAX_LEVEL; // Static max level
    
    return list;
}

bool skiplist_insert_lockfree(SkipList* list, int key, int value) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    
    while (true) {
        // 1. Search to find insertion point
        if (find(list, key, preds, succs)) {
            return false; // Key already exists
        }
        
        // 2. Prepare new node
        int topLevel = random_level();
        // Ensure random level doesn't exceed config
        if (topLevel >= MAX_LEVEL) topLevel = MAX_LEVEL - 1;
        
        Node* newNode = alloc_node(key, value, topLevel);
        
        // Initialize next pointers for the new node to point to successors
        for (int i = 0; i <= topLevel; i++) {
            atomic_store(&newNode->next[i], succs[i]);
        }
        
        // 3. Link at Level 0 (Linearization Point)
        Node* pred = preds[0];
        Node* succ = succs[0];
        
        // Attempt to splice in new node at bottom level
        if (!atomic_compare_exchange_strong(&pred->next[0], &succ, newNode)) {
            // Failed: Back off and retry loop (alloc_node leak handled by GC in real impl, here we free)
            free(newNode);
            continue; 
        }
        
        // Success at Level 0! The node is now logically in the list.
        atomic_fetch_add(&list->size, 1);
        
        // 4. Build the tower upwards (Best Effort)
        for (int i = 1; i <= topLevel; i++) {
            while (true) {
                pred = preds[i];
                succ = succs[i];
                
                // Attempt to link
                if (atomic_compare_exchange_strong(&pred->next[i], &succ, newNode)) {
                    break; // Success at this level
                }
                
                // If CAS failed, our 'preds' and 'succs' are stale.
                // We re-run find to get fresh pointers.
                find(list, key, preds, succs);
                
                // Check if our node was deleted concurrently while we were building it
                // If our level 0 next pointer is marked, we are dead.
                if (IS_MARKED(atomic_load(&newNode->next[0]))) {
                    // Stop building, we are being deleted
                    return true; 
                }
                
                // Reset the new node's next ptr to the new successor before retrying CAS
                atomic_store(&newNode->next[i], succs[i]);
            }
        }
        
        atomic_store(&newNode->fully_linked, true);
        return true;
    }
}

bool skiplist_delete_lockfree(SkipList* list, int key) {
    Node* preds[MAX_LEVEL + 1];
    Node* succs[MAX_LEVEL + 1];
    Node* victim;
    
    while (true) {
        // 1. Find the node
        if (!find(list, key, preds, succs)) {
            return false; // Key not found
        }
        
        victim = succs[0];
        
        // 2. Mark for deletion (Logical Delete)
        // Strictly only Level 0 marking is required for correctness,
        // but marking all levels is good practice.
        // We start from the top level of the victim.
        for (int i = victim->topLevel; i >= 0; i--) {
            Node* succ = atomic_load(&victim->next[i]);
            
            // If already marked, someone else is deleting it
            if (IS_MARKED(succ)) {
                if (i == 0) return false; // Already deleted
                continue;
            }
            
            Node* marked_succ = GET_MARKED(succ);
            
            // Attempt to mark the pointer
            if (!atomic_compare_exchange_strong(&victim->next[i], &succ, marked_succ)) {
                // If CAS fails at level 0, the node might have been modified or deleted.
                // We must restart the main loop.
                if (i == 0) goto retry_delete;
            }
        }
        
        // 3. Physical Unlink (Helping)
        // Call find() one more time. Find() automatically unlinks marked nodes.
        find(list, key, preds, succs);
        
        atomic_fetch_sub(&list->size, 1);
        return true;
        
        retry_delete:;
        // Check loop continues
    }
}

bool skiplist_contains_lockfree(SkipList* list, int key) {
    Node* pred = list->head;
    
    // Traverse from top down
    for (int level = MAX_LEVEL - 1; level >= 0; level--) {
        Node* curr = GET_UNMARKED(atomic_load(&pred->next[level]));
        
        while (true) {
            // Safety check for end of list
            if (curr == NULL || curr == list->tail) break; 
            
            Node* succ = atomic_load(&curr->next[level]);
            
            // Skip marked nodes (treat as logically deleted)
            while (IS_MARKED(succ)) {
                curr = GET_UNMARKED(succ);
                if (curr == NULL || curr == list->tail) goto next_level;
                succ = atomic_load(&curr->next[level]);
            }
            
            if (curr->key < key) {
                pred = curr;
                curr = GET_UNMARKED(succ);
            } else {
                break;
            }
        }
        next_level:;
    }
    
    // Check level 0
    Node* curr = GET_UNMARKED(atomic_load(&pred->next[0]));
    
    // Must check:
    // 1. Not tail
    // 2. Key matches
    // 3. Not marked (logically deleted)
    return (curr != list->tail && 
            curr->key == key && 
            !IS_MARKED(atomic_load(&curr->next[0])));
}

void skiplist_destroy_lockfree(SkipList* list) {
    // Note: This is not thread-safe. Should only be called when single-threaded.
    Node* curr = list->head;
    while (curr) {
        Node* next = GET_UNMARKED(atomic_load(&curr->next[0]));
        free(curr);
        curr = next;
    }
    free(list);
}