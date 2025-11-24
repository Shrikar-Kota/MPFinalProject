#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

// --- OPTIMIZATION: Fast RNG ---
// Thread-local state for XorShift (no locking, very fast)
static __thread uint32_t rng_state = 0;

void init_random_seed(void) {
    uint64_t t = (uint64_t)time(NULL);
    uint64_t tid = (uint64_t)omp_get_thread_num();
    // Mix time and thread ID
    rng_state = (uint32_t)(t ^ (tid << 16) ^ tid);
    if (rng_state == 0) rng_state = 12345;
}

inline uint32_t fast_rand(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}

int random_level(void) {
    if (rng_state == 0) init_random_seed();
    
    int level = 0;
    uint32_t r = fast_rand();
    
    // Geometric distribution using bitwise check
    while (level < MAX_LEVEL && (r & 1)) {
        level++;
        r >>= 1;
    }
    return level;
}

Node* create_node(int key, int value, int level) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) {
        fprintf(stderr, "Failed to allocate memory for node\n");
        exit(1);
    }
    
    node->key = key;
    node->value = value;
    node->topLevel = level;
    atomic_init(&node->fully_linked, false);
    
    // Initialize next pointers to NULL (unmarked)
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_init(&node->next[i], NULL);
    }
    
    omp_init_lock(&node->lock);
    return node;
}

void print_skiplist(SkipList* list) {
    printf("\n=== Skip List Structure ===\n");
    for (int level = list->maxLevel; level >= 0; level--) {
        printf("Level %2d: HEAD -> ", level);
        Node* curr = UNMARK_PTR(atomic_load(&list->head->next[level]));
        
        while (curr != list->tail && curr != NULL) {
            // Check if logically deleted via tag on level 0
            Node* next_ptr = atomic_load(&curr->next[0]);
            bool is_del = IS_MARKED(next_ptr);
            
            printf("%d%s -> ", curr->key, is_del ? "(D)" : "");
            curr = UNMARK_PTR(atomic_load(&curr->next[level]));
        }
        printf("TAIL\n");
    }
    printf("Size: %d\n", atomic_load(&list->size));
    printf("===========================\n\n");
}

bool validate_skiplist(SkipList* list) {
    // Simplified validation
    for (int level = 0; level <= list->maxLevel; level++) {
        Node* curr = UNMARK_PTR(atomic_load(&list->head->next[level]));
        int prev_key = INT_MIN;
        
        while (curr != list->tail && curr != NULL) {
            Node* next_ptr = atomic_load(&curr->next[0]);
            if (!IS_MARKED(next_ptr)) {
                if (curr->key < prev_key) return false;
                prev_key = curr->key;
            }
            curr = UNMARK_PTR(atomic_load(&curr->next[level]));
        }
    }
    return true;
}