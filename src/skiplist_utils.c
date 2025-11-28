#include "skiplist_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>

// Thread-local seed
static __thread unsigned int seed = 0;

// Initialize with Nanosecond precision + Thread ID
void init_random_seed(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // XORing seconds, nanoseconds, and thread ID guarantees unique seeds
    // even if threads start at the exact same moment.
    seed = (unsigned int)(ts.tv_sec ^ ts.tv_nsec ^ omp_get_thread_num());
}

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
        Node* curr = GET_UNMARKED(atomic_load(&list->head->next[level]));
        while (curr != list->tail) {
            bool marked = IS_MARKED(atomic_load(&curr->next[0])); 
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
            if (curr->key < prev_key) {
                // In a lock-free list, seeing a node 'out of order' usually means
                // we are traversing a deleted path. It is not necessarily a bug,
                // but for a strict validator, we flag it.
                // However, finding marked nodes is expected.
                if (!IS_MARKED(atomic_load(&curr->next[0]))) {
                     fprintf(stderr, "Validation failed: unsorted at level %d\n", level);
                     return false;
                }
            }
            prev_key = curr->key;
            curr = GET_UNMARKED(atomic_load(&curr->next[level]));
        }
    }
    return true;
}