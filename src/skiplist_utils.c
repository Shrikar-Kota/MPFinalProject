#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>

// Thread-local random state
static __thread unsigned int seed = 0;

// Initialize thread-local seed with Nanosecond precision
// This prevents "Duplicate Run" errors when executed fast.
void init_random_seed(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // Combine seconds, nanoseconds, and thread ID for a unique seed
    seed = (unsigned int)(ts.tv_sec ^ ts.tv_nsec ^ omp_get_thread_num());
}

int random_level(void) {
    if (seed == 0) {
        init_random_seed();
    }
    
    int level = 0;
    // Standard skip list level generation
    while (level < MAX_LEVEL && (rand_r(&seed) / (double)RAND_MAX) < P_FACTOR) {
        level++;
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
    atomic_init(&node->marked, false);
    atomic_init(&node->fully_linked, false);
    
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
        Node* curr = atomic_load(&list->head->next[level]);
        while (curr != list->tail) {
            bool marked = IS_MARKED(atomic_load(&curr->next[0])); // Check LSB for lock-free
            printf("%d%s -> ", curr->key, marked ? "(D)" : "");
            curr = GET_UNMARKED(atomic_load(&curr->next[level]));
        }
        printf("TAIL\n");
    }
    printf("Size: %d\n", atomic_load(&list->size));
    printf("===========================\n\n");
}

bool validate_skiplist(SkipList* list) {
    for (int level = 0; level <= list->maxLevel; level++) {
        Node* curr = GET_UNMARKED(atomic_load(&list->head->next[level]));
        int prev_key = INT_MIN;
        
        while (curr != list->tail) {
            // In lock-free, we might traverse logically deleted nodes.
            // We only validate order.
            if (curr->key < prev_key) {
                fprintf(stderr, "Validation failed: unsorted at level %d (Prev: %d, Curr: %d)\n", level, prev_key, curr->key);
                return false;
            }
            prev_key = curr->key;
            curr = GET_UNMARKED(atomic_load(&curr->next[level]));
        }
    }
    return true;
}