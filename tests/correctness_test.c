#include "../src/skiplist_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <omp.h>

#define TEST_SIZE 500
#define NUM_THREADS 4

static int tests_passed = 0;

typedef struct {
    SkipList* (*create)(void);
    bool (*insert)(SkipList*, int, int);
    bool (*delete)(SkipList*, int);
    bool (*contains)(SkipList*, int);
    void (*destroy)(SkipList*);
} SkipListOps;

void test_basic(SkipListOps* ops) {
    SkipList* list = ops->create();
    
    assert(ops->insert(list, 10, 100));
    assert(ops->insert(list, 20, 200));
    assert(!ops->insert(list, 10, 999));
    
    assert(ops->contains(list, 10));
    assert(ops->contains(list, 20));
    assert(!ops->contains(list, 15));
    
    assert(ops->delete(list, 10));
    assert(!ops->contains(list, 10));
    assert(!ops->delete(list, 10));
    
    ops->destroy(list);
}

void test_sequential(SkipListOps* ops) {
    SkipList* list = ops->create();
    
    for (int i = 0; i < TEST_SIZE; i++) {
        assert(ops->insert(list, i, i));
    }
    
    for (int i = 0; i < TEST_SIZE; i++) {
        assert(ops->contains(list, i));
    }
    
    for (int i = 0; i < TEST_SIZE; i += 2) {
        assert(ops->delete(list, i));
    }
    
    for (int i = 0; i < TEST_SIZE; i++) {
        if (i % 2 == 0) {
            assert(!ops->contains(list, i));
        } else {
            assert(ops->contains(list, i));
        }
    }
    
    ops->destroy(list);
}

void test_concurrent(SkipListOps* ops) {
    SkipList* list = ops->create();
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < TEST_SIZE; i++) {
            int key = tid * TEST_SIZE + i;
            ops->insert(list, key, key);
        }
    }
    
    for (int tid = 0; tid < NUM_THREADS; tid++) {
        for (int i = 0; i < TEST_SIZE; i++) {
            int key = tid * TEST_SIZE + i;
            assert(ops->contains(list, key));
        }
    }
    
    ops->destroy(list);
}

void test_mixed(SkipListOps* ops) {
    SkipList* list = ops->create();
    
    for (int i = 0; i < TEST_SIZE / 2; i++) {
        ops->insert(list, i, i);
    }
    
    #pragma omp parallel num_threads(NUM_THREADS)
    {
        unsigned int seed = omp_get_thread_num();
        for (int i = 0; i < TEST_SIZE; i++) {
            int key = rand_r(&seed) % TEST_SIZE;
            int op = rand_r(&seed) % 3;
            
            if (op == 0) {
                ops->insert(list, key, key);
            } else if (op == 1) {
                ops->delete(list, key);
            } else {
                ops->contains(list, key);
            }
        }
    }
    
    assert(validate_skiplist(list));
    ops->destroy(list);
}

#define RUN_TEST(name, ops) do { \
    printf("  %s... ", #name); \
    fflush(stdout); \
    test_##name(ops); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

void run_tests(const char* name, SkipListOps* ops) {
    printf("\n%s Implementation:\n", name);
    RUN_TEST(basic, ops);
    RUN_TEST(sequential, ops);
    RUN_TEST(concurrent, ops);
    RUN_TEST(mixed, ops);
}

int main(void) {
    printf("Skip List Correctness Tests\n");
    printf("============================\n");
    
    SkipListOps coarse_ops = {
        skiplist_create_coarse,
        skiplist_insert_coarse,
        skiplist_delete_coarse,
        skiplist_contains_coarse,
        skiplist_destroy_coarse
    };
    run_tests("Coarse-Grained", &coarse_ops);
    
    SkipListOps fine_ops = {
        skiplist_create_fine,
        skiplist_insert_fine,
        skiplist_delete_fine,
        skiplist_contains_fine,
        skiplist_destroy_fine
    };
    run_tests("Fine-Grained", &fine_ops);
    
    SkipListOps lockfree_ops = {
        skiplist_create_lockfree,
        skiplist_insert_lockfree,
        skiplist_delete_lockfree,
        skiplist_contains_lockfree,
        skiplist_destroy_lockfree
    };
    run_tests("Lock-Free", &lockfree_ops);
    
    printf("\n============================\n");
    printf("All %d tests PASSED ✓\n", tests_passed);
    return 0;
}