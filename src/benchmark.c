#include "skiplist_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

typedef struct {
    char impl[20];
    int num_threads;
    int ops_per_thread;
    int key_range;
    char workload[20];
    int insert_percent;
    int delete_percent;
    int search_percent;
    int initial_size;
    int warmup_ops;
} BenchmarkConfig;

typedef struct {
    double total_time;
    double throughput;
    int successful_ops;
    int failed_ops;
} BenchmarkResult;

typedef struct {
    SkipList* (*create)(void);
    bool (*insert)(SkipList*, int, int);
    bool (*delete)(SkipList*, int);
    bool (*contains)(SkipList*, int);
    void (*destroy)(SkipList*);
} SkipListOps;

SkipListOps get_operations(const char* impl) {
    SkipListOps ops;
    
    if (strcmp(impl, "coarse") == 0) {
        ops.create = skiplist_create_coarse;
        ops.insert = skiplist_insert_coarse;
        ops.delete = skiplist_delete_coarse;
        ops.contains = skiplist_contains_coarse;
        ops.destroy = skiplist_destroy_coarse;
    } else if (strcmp(impl, "fine") == 0) {
        ops.create = skiplist_create_fine;
        ops.insert = skiplist_insert_fine;
        ops.delete = skiplist_delete_fine;
        ops.contains = skiplist_contains_fine;
        ops.destroy = skiplist_destroy_fine;
    } else if (strcmp(impl, "lockfree") == 0) {
        ops.create = skiplist_create_lockfree;
        ops.insert = skiplist_insert_lockfree;
        ops.delete = skiplist_delete_lockfree;
        ops.contains = skiplist_contains_lockfree;
        ops.destroy = skiplist_destroy_lockfree;
    } else {
        fprintf(stderr, "Unknown implementation: %s\n", impl);
        exit(1);
    }
    
    return ops;
}

void prepopulate_list(SkipList* list, SkipListOps* ops, int size, int key_range) {
    #pragma omp parallel for
    for (int i = 0; i < size; i++) {
        unsigned int seed = i;
        int key = rand_r(&seed) % key_range;
        ops->insert(list, key, key);
    }
}

BenchmarkResult run_insert_workload(SkipList* list, SkipListOps* ops, BenchmarkConfig* config) {
    BenchmarkResult result = {0};
    int successful = 0;
    
    double start = omp_get_wtime();
    
    #pragma omp parallel num_threads(config->num_threads) reduction(+:successful)
    {
        unsigned int seed = omp_get_thread_num() * 12345;
        
        for (int i = 0; i < config->ops_per_thread; i++) {
            int key = rand_r(&seed) % config->key_range;
            if (ops->insert(list, key, key)) {
                successful++;
            }
        }
    }
    
    double end = omp_get_wtime();
    
    result.total_time = end - start;
    result.successful_ops = successful;
    result.failed_ops = (config->num_threads * config->ops_per_thread) - successful;
    result.throughput = (config->num_threads * config->ops_per_thread) / result.total_time;
    
    return result;
}

BenchmarkResult run_delete_workload(SkipList* list, SkipListOps* ops, BenchmarkConfig* config) {
    BenchmarkResult result = {0};
    int successful = 0;
    
    double start = omp_get_wtime();
    
    #pragma omp parallel num_threads(config->num_threads) reduction(+:successful)
    {
        unsigned int seed = omp_get_thread_num() * 23456;
        
        for (int i = 0; i < config->ops_per_thread; i++) {
            int key = rand_r(&seed) % config->key_range;
            if (ops->delete(list, key)) {
                successful++;
            }
        }
    }
    
    double end = omp_get_wtime();
    
    result.total_time = end - start;
    result.successful_ops = successful;
    result.failed_ops = (config->num_threads * config->ops_per_thread) - successful;
    result.throughput = (config->num_threads * config->ops_per_thread) / result.total_time;
    
    return result;
}

BenchmarkResult run_readonly_workload(SkipList* list, SkipListOps* ops, BenchmarkConfig* config) {
    BenchmarkResult result = {0};
    int successful = 0;
    
    double start = omp_get_wtime();
    
    #pragma omp parallel num_threads(config->num_threads) reduction(+:successful)
    {
        unsigned int seed = omp_get_thread_num() * 34567;
        
        for (int i = 0; i < config->ops_per_thread; i++) {
            int key = rand_r(&seed) % config->key_range;
            if (ops->contains(list, key)) {
                successful++;
            }
        }
    }
    
    double end = omp_get_wtime();
    
    result.total_time = end - start;
    result.successful_ops = successful;
    result.failed_ops = (config->num_threads * config->ops_per_thread) - successful;
    result.throughput = (config->num_threads * config->ops_per_thread) / result.total_time;
    
    return result;
}

BenchmarkResult run_mixed_workload(SkipList* list, SkipListOps* ops, BenchmarkConfig* config) {
    BenchmarkResult result = {0};
    int successful = 0;
    
    double start = omp_get_wtime();
    
    #pragma omp parallel num_threads(config->num_threads) reduction(+:successful)
    {
        unsigned int seed = omp_get_thread_num() * 45678;
        
        for (int i = 0; i < config->ops_per_thread; i++) {
            int op_type = rand_r(&seed) % 100;
            int key = rand_r(&seed) % config->key_range;
            
            if (op_type < config->insert_percent) {
                if (ops->insert(list, key, key)) successful++;
            } else if (op_type < config->insert_percent + config->delete_percent) {
                if (ops->delete(list, key)) successful++;
            } else {
                if (ops->contains(list, key)) successful++;
            }
        }
    }
    
    double end = omp_get_wtime();
    
    result.total_time = end - start;
    result.successful_ops = successful;
    result.failed_ops = (config->num_threads * config->ops_per_thread) - successful;
    result.throughput = (config->num_threads * config->ops_per_thread) / result.total_time;
    
    return result;
}

void print_results(BenchmarkConfig* config, BenchmarkResult* result) {
    printf("\n=== Benchmark Results ===\n");
    printf("Implementation: %s\n", config->impl);
    printf("Threads: %d\n", config->num_threads);
    printf("Workload: %s\n", config->workload);
    printf("Operations: %d\n", config->num_threads * config->ops_per_thread);
    printf("Key Range: %d\n", config->key_range);
    printf("Time: %.4f seconds\n", result->total_time);
    printf("Throughput: %.2f ops/sec\n", result->throughput);
    printf("Successful: %d\n", result->successful_ops);
    printf("Failed: %d\n", result->failed_ops);
    printf("========================\n\n");
}

void print_csv_header() {
    printf("impl,threads,workload,ops,key_range,time,throughput,successful,failed\n");
}

void print_csv_results(BenchmarkConfig* config, BenchmarkResult* result) {
    printf("%s,%d,%s,%d,%d,%.4f,%.2f,%d,%d\n",
           config->impl, config->num_threads, config->workload,
           config->num_threads * config->ops_per_thread,
           config->key_range, result->total_time, result->throughput,
           result->successful_ops, result->failed_ops);
}

void run_benchmark(BenchmarkConfig* config, bool csv_output) {
    SkipListOps ops = get_operations(config->impl);
    SkipList* list = ops.create();
    
    if (config->initial_size > 0) {
        prepopulate_list(list, &ops, config->initial_size, config->key_range);
    }
    
    BenchmarkResult result;
    
    if (strcmp(config->workload, "insert") == 0) {
        result = run_insert_workload(list, &ops, config);
    } else if (strcmp(config->workload, "delete") == 0) {
        result = run_delete_workload(list, &ops, config);
    } else if (strcmp(config->workload, "readonly") == 0) {
        result = run_readonly_workload(list, &ops, config);
    } else if (strcmp(config->workload, "mixed") == 0) {
        result = run_mixed_workload(list, &ops, config);
    } else {
        fprintf(stderr, "Unknown workload: %s\n", config->workload);
        ops.destroy(list);
        exit(1);
    }
    
    if (csv_output) {
        print_csv_results(config, &result);
    } else {
        print_results(config, &result);
    }
    
    ops.destroy(list);
}

void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  --impl <type>        Implementation: coarse, fine, lockfree (default: lockfree)\n");
    printf("  --threads <n>        Number of threads (default: 4)\n");
    printf("  --ops <n>            Operations per thread (default: 100000)\n");
    printf("  --key-range <n>      Range of keys (default: 10000)\n");
    printf("  --workload <type>    Workload: insert, delete, readonly, mixed (default: mixed)\n");
    printf("  --insert-pct <n>     Insert percentage for mixed (default: 30)\n");
    printf("  --delete-pct <n>     Delete percentage for mixed (default: 20)\n");
    printf("  --initial-size <n>   Pre-populate list (default: 0)\n");
    printf("  --warmup <n>         Warmup operations (default: 1000)\n");
    printf("  --csv                Output in CSV format\n");
    printf("  --help               Show this help message\n");
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config = {
        .impl = "lockfree",
        .num_threads = 4,
        .ops_per_thread = 100000,
        .key_range = 10000,
        .workload = "mixed",
        .insert_percent = 30,
        .delete_percent = 20,
        .search_percent = 50,
        .initial_size = 0,
        .warmup_ops = 1000
    };
    
    strcpy(config.impl, "lockfree");
    strcpy(config.workload, "mixed");
    
    bool csv_output = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--impl") == 0 && i + 1 < argc) {
            strcpy(config.impl, argv[++i]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            config.num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc) {
            config.ops_per_thread = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--key-range") == 0 && i + 1 < argc) {
            config.key_range = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--workload") == 0 && i + 1 < argc) {
            strcpy(config.workload, argv[++i]);
        } else if (strcmp(argv[i], "--insert-pct") == 0 && i + 1 < argc) {
            config.insert_percent = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--delete-pct") == 0 && i + 1 < argc) {
            config.delete_percent = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--initial-size") == 0 && i + 1 < argc) {
            config.initial_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            config.warmup_ops = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv_output = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    config.search_percent = 100 - config.insert_percent - config.delete_percent;
    
    if (csv_output) {
        print_csv_header();
    }
    
    run_benchmark(&config, csv_output);
    
    return 0;
}