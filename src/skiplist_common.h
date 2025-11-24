#ifndef SKIPLIST_COMMON_H
#define SKIPLIST_COMMON_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <omp.h>

// Configuration
#define MAX_LEVEL 16
#define P_FACTOR 0.5
#define CACHE_LINE_SIZE 64

// --- OPTIMIZATION: Tagged Pointer Macros ---
// We use the last bit of the pointer (which is always 0 in 64-bit alignment)
// to store the "marked" status.
// 0 = Unmarked (Valid), 1 = Marked (Deleted)
#define IS_MARKED(ptr)        ((uintptr_t)(ptr) & 1)
#define MARK_PTR(ptr)         ((Node*)((uintptr_t)(ptr) | 1))
#define UNMARK_PTR(ptr)       ((Node*)((uintptr_t)(ptr) & ~1))

typedef struct Node {
    int key;
    int value;
    int topLevel;
    
    // 'fully_linked' is mainly for the Fine/Coarse implementations 
    // or debugging; Lock-free relies on pointer tags.
    _Atomic(bool) fully_linked;

    // The next pointers. In lock-free mode, these may contain tags (Bit 0 = 1).
    // Always use UNMARK_PTR() before dereferencing!
    _Atomic(struct Node*) next[MAX_LEVEL + 1];

    omp_lock_t lock; // Only used for locking implementations

    // --- OPTIMIZATION: Padding ---
    // Ensures nodes don't share cache lines, preventing false sharing.
    // Size calculation approximates avoiding cache line overlap.
    char pad[CACHE_LINE_SIZE]; 
} Node;

typedef struct SkipList {
    Node* head;
    Node* tail;
    int maxLevel;
    
    // --- OPTIMIZATION: Padding ---
    // Separates constant data (head/tail) from frequently updated data (size)
    char pad1[CACHE_LINE_SIZE]; 
    
    _Atomic(int) size;
    
    char pad2[CACHE_LINE_SIZE];
    
    omp_lock_t lock; // For coarse-grained
} SkipList;

// Prototypes
SkipList* skiplist_create_coarse(void);
bool skiplist_insert_coarse(SkipList* list, int key, int value);
bool skiplist_delete_coarse(SkipList* list, int key);
bool skiplist_contains_coarse(SkipList* list, int key);
void skiplist_destroy_coarse(SkipList* list);

SkipList* skiplist_create_fine(void);
bool skiplist_insert_fine(SkipList* list, int key, int value);
bool skiplist_delete_fine(SkipList* list, int key);
bool skiplist_contains_fine(SkipList* list, int key);
void skiplist_destroy_fine(SkipList* list);

SkipList* skiplist_create_lockfree(void);
bool skiplist_insert_lockfree(SkipList* list, int key, int value);
bool skiplist_delete_lockfree(SkipList* list, int key);
bool skiplist_contains_lockfree(SkipList* list, int key);
void skiplist_destroy_lockfree(SkipList* list);

int random_level(void);
Node* create_node(int key, int value, int level);
void print_skiplist(SkipList* list);
bool validate_skiplist(SkipList* list);

#endif // SKIPLIST_COMMON_H