#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

// Thread-local random state
static __thread unsigned int seed = 0;

// Initialize thread-local seed
void init_random_seed(void) {
    seed = (unsigned int)time(NULL) ^ (unsigned int)omp_get_thread_num();
}

// Generate random level for new node
int random_level(void) {
    if (seed == 0) {
        init_random_seed();
    }
    
    int level = 0;
    while (level < MAX_LEVEL && (rand_r(&seed) / (double)RAND_MAX) < P_FACTOR) {
        level++;
    }
    return level;
}

// Create a new node
Node* create_node(int key, int value, int level) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) {
        fprintf(stderr, "Failed to allocate memory for node\n");
        exit(1);
    }
    
    node->key = key;
    node->value = value;
    node->topLevel = level;
    atomic_init(&node->marked, false);
    atomic_init(&node->fully_linked, false);  // Not linked yet
    
    for (int i = 0; i <= MAX_LEVEL; i++) {
        atomic_init(&node->next[i], NULL);
    }
    
    omp_init_lock(&node->lock);
    
    return node;
}

// Print skip list structure (for debugging)
void print_skiplist(SkipList* list) {
    printf("\n=== Skip List Structure ===\n");
    
    for (int level = list->maxLevel; level >= 0; level--) {
        printf("Level %2d: HEAD -> ", level);
        Node* curr = atomic_load(&list->head->next[level]);
        
        while (curr != list->tail) {
            bool marked = atomic_load(&curr->marked);
            printf("%d%s -> ", curr->key, marked ? "(D)" : "");
            curr = atomic_load(&curr->next[level]);
        }
        printf("TAIL\n");
    }
    printf("Size: %d\n", atomic_load(&list->size));
    printf("===========================\n\n");
}

// Validate skip list structure
bool validate_skiplist(SkipList* list) {
    for (int level = 0; level <= list->maxLevel; level++) {
        Node* curr = atomic_load(&list->head->next[level]);
        int prev_key = INT_MIN;
        
        while (curr != list->tail) {
            // Skip nodes that are not fully linked yet or are marked for deletion
            bool fully_linked = atomic_load(&curr->fully_linked);
            bool marked = atomic_load(&curr->marked);
            
            if (fully_linked && !marked) {
                if (curr->key <= prev_key) {
                    fprintf(stderr, "Validation failed: unsorted at level %d\n", level);
                    return false;
                }
                prev_key = curr->key;
            }
            curr = atomic_load(&curr->next[level]);
        }
    }
    return true;
}