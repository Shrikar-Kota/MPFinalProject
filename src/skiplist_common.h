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

// Node structure for skip list
typedef struct Node {
    int key;
    int value;
    int topLevel;
    _Atomic(bool) marked;  // For logical deletion in lock-free version
    _Atomic(struct Node*) next[MAX_LEVEL + 1];
    omp_lock_t lock;  // For fine-grained locking version
} Node;

// Skip list structure
typedef struct SkipList {
    Node* head;
    Node* tail;
    int maxLevel;
    _Atomic(int) size;
    omp_lock_t lock;  // For coarse-grained locking
} SkipList;

// Function prototypes for all implementations
// Coarse-grained
SkipList* skiplist_create_coarse(void);
bool skiplist_insert_coarse(SkipList* list, int key, int value);
bool skiplist_delete_coarse(SkipList* list, int key);
bool skiplist_contains_coarse(SkipList* list, int key);
void skiplist_destroy_coarse(SkipList* list);

// Fine-grained
SkipList* skiplist_create_fine(void);
bool skiplist_insert_fine(SkipList* list, int key, int value);
bool skiplist_delete_fine(SkipList* list, int key);
bool skiplist_contains_fine(SkipList* list, int key);
void skiplist_destroy_fine(SkipList* list);

// Lock-free
SkipList* skiplist_create_lockfree(void);
bool skiplist_insert_lockfree(SkipList* list, int key, int value);
bool skiplist_delete_lockfree(SkipList* list, int key);
bool skiplist_contains_lockfree(SkipList* list, int key);
void skiplist_destroy_lockfree(SkipList* list);

// Utility functions
int random_level(void);
Node* create_node(int key, int value, int level);
void print_skiplist(SkipList* list);
bool validate_skiplist(SkipList* list);

#endif // SKIPLIST_COMMON_H