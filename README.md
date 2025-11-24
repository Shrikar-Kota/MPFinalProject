# Lock-Free Skip List Implementation in OpenMP

A comprehensive implementation and evaluation of concurrent skip list algorithms using OpenMP, including coarse-grained locking, fine-grained locking, and lock-free approaches.

## Project Overview

This project implements three variants of concurrent skip lists:
1. **Coarse-grained locking**: Single global lock protecting the entire data structure
2. **Fine-grained locking**: Hand-over-hand locking with optimistic validation
3. **Lock-free**: Non-blocking algorithm using atomic operations and logical deletion

The implementations are benchmarked across various workloads and thread counts to evaluate scalability, throughput, and performance characteristics.

## Features

- ✅ Three complete skip list implementations
- ✅ Comprehensive correctness testing suite
- ✅ Detailed performance benchmarking framework
- ✅ Automated experiment scripts
- ✅ Visualization tools for results analysis
- ✅ Thread sanitizer support for race condition detection

## Project Structure

```
skiplist-lockfree/
├── src/
│   ├── skiplist_common.h          # Common data structures and interfaces
│   ├── skiplist_utils.c           # Utility functions
│   ├── skiplist_coarse.c          # Coarse-grained locking implementation
│   ├── skiplist_fine.c            # Fine-grained locking implementation
│   ├── skiplist_lockfree.c        # Lock-free implementation
│   └── benchmark.c                # Performance benchmarking tool
├── tests/
│   └── correctness_test.c         # Correctness validation tests
├── scripts/
│   ├── run_experiments.sh         # Automated benchmark suite
│   └── plot_results.py            # Results visualization
├── Makefile                       # Build configuration
└── README.md                      # This file
```

## Building the Project

### Prerequisites

- GCC with OpenMP support (gcc >= 7.0)
- Make
- Python 3 with matplotlib, pandas, seaborn (for visualization)

### Compilation

```bash
# Build all executables
make

# Build with debug symbols
make debug

# Build with Thread Sanitizer (for race detection)
make sanitize

# Clean build artifacts
make clean
```

## Running Tests

### Correctness Tests

```bash
# Run all correctness tests
make test

# Or run directly
./bin/correctness_test
```

The correctness test suite includes:
- Basic operations (insert, delete, search)
- Sequential operations
- Concurrent inserts, deletes, and searches
- Mixed concurrent operations
- High contention scenarios
- Edge cases (empty list, duplicate keys, etc.)

### Performance Benchmarks

#### Quick Benchmark

```bash
# Run basic benchmark with default parameters
make benchmark
```

#### Custom Benchmark

```bash
./bin/benchmark [OPTIONS]

Options:
  --impl <type>        Implementation: coarse, fine, lockfree (default: lockfree)
  --threads <n>        Number of threads (default: 4)
  --ops <n>            Operations per thread (default: 100000)
  --key-range <n>      Range of keys (default: 10000)
  --workload <type>    Workload: insert, delete, readonly, mixed (default: mixed)
  --insert-pct <n>     Insert percentage for mixed workload (default: 30)
  --delete-pct <n>     Delete percentage for mixed workload (default: 20)
  --initial-size <n>   Pre-populate list (default: 0)
  --warmup <n>         Warmup operations (default: 1000)
  --csv                Output in CSV format
  --help               Show help message
```

#### Example Benchmarks

```bash
# Test lock-free implementation with 16 threads, insert-only workload
./bin/benchmark --impl lockfree --threads 16 --workload insert --ops 500000

# Test all implementations with mixed workload
for impl in coarse fine lockfree; do
    ./bin/benchmark --impl $impl --threads 8 --workload mixed
done

# High contention test (small key range)
./bin/benchmark --impl lockfree --threads 32 --key-range 1000 --workload mixed

# CSV output for further analysis
./bin/benchmark --impl lockfree --threads 4 --csv > results.csv
```

### Comprehensive Experiments

```bash
# Run full benchmark suite (takes ~10-30 minutes)
./scripts/run_experiments.sh

# Results saved to: results/results_TIMESTAMP.csv
```

The experiment suite includes:
1. **Scalability Test**: Throughput vs thread count (1, 2, 4, 8, 16, 32 threads)
2. **Workload Comparison**: Insert, delete, read-only, and mixed workloads
3. **Contention Study**: Performance under different key ranges
4. **Insert Performance**: Pure insertion workload
5. **Read Performance**: Read-heavy workload (90% reads)

## Visualizing Results

```bash
# Generate plots from benchmark results
python3 scripts/plot_results.py results/results_TIMESTAMP.csv
```

Generated plots:
- **scalability.png**: Throughput vs thread count
- **speedup.png**: Speedup relative to single thread
- **workload_comparison.png**: Performance across different workloads
- **contention.png**: Impact of contention on throughput
- **success_rate.png**: Operation success rates
- **throughput_heatmap.png**: Heatmap of performance

## Algorithm Details

### Coarse-Grained Locking

**Approach**: Single global lock protects all operations.

**Pros**:
- Simple implementation
- Easy to reason about correctness

**Cons**:
- Poor scalability
- High contention
- Sequential bottleneck

### Fine-Grained Locking

**Approach**: Hand-over-hand locking with optimistic validation.

**Key Features**:
- Lock predecessors during modifications
- Optimistic search phase (no locks)
- Validation before committing changes
- Retry on validation failure

**Pros**:
- Better scalability than coarse-grained
- Lock-free reads possible

**Cons**:
- More complex than coarse-grained
- Potential for lock contention
- Risk of deadlock if not careful

### Lock-Free

**Approach**: Non-blocking algorithm using atomic CAS operations.

**Key Features**:
- Logical deletion (mark-then-unlink)
- Bottom-up insertion (link level 0 first)
- Wait-free search operations
- No locks, only atomic operations

**Synchronization Primitives**:
- Atomic compare-and-swap (CAS)
- Atomic loads/stores
- Memory barriers (via atomic operations)

**Progress Guarantees**:
- **Search**: Wait-free (always completes in bounded steps)
- **Insert/Delete**: Lock-free (system-wide progress guaranteed)

**Pros**:
- Excellent scalability
- No deadlock/livelock
- Wait-free reads

**Cons**:
- Complex implementation
- Memory reclamation challenges
- ABA problem (mitigated with marked pointers)

## Performance Expectations

Based on typical multi-core systems:

### Scalability
- **Coarse-grained**: Limited speedup (~1.5-2x at 8 threads)
- **Fine-grained**: Moderate speedup (~3-5x at 8 threads)
- **Lock-free**: Near-linear speedup (~6-7x at 8 threads)

### Workload Performance
- **Read-heavy**: Lock-free >> Fine-grained > Coarse-grained
- **Write-heavy**: Lock-free > Fine-grained >> Coarse-grained
- **Mixed**: Lock-free > Fine-grained > Coarse-grained

### Contention
- **Low contention** (large key range): All implementations perform well
- **High contention** (small key range): Lock-free maintains performance

## Memory Safety

### Current Implementation
- Deleted nodes are marked but not immediately freed
- Safe for concurrent access during deletion
- Memory leak in long-running scenarios

### Production Considerations
For production use, implement proper memory reclamation:
- **Hazard Pointers**: Thread announces nodes being accessed
- **Epoch-Based Reclamation**: Batch deallocation after grace period
- **Reference Counting**: Track active references per node

## Known Limitations

1. **Memory Reclamation**: Deleted nodes not freed (safe but leaks memory)
2. **ABA Problem**: Partially addressed with marked flag
3. **Memory Model**: Relies on x86-64 memory ordering
4. **Max Level**: Fixed at compile time (MAX_LEVEL = 16)

## Running on VT's HPC Cluster

### Access ARC Resources

```bash
# SSH to login node
ssh username@cascades.arc.vt.edu

# Load required modules
module load gcc/11.2.0

# Clone/upload project
# Build and run as usual
```

### Submit Batch Job

Create a SLURM script (`run_benchmark.sh`):

```bash
#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=32
#SBATCH --time=01:00:00
#SBATCH --partition=normal_q

module load gcc/11.2.0

cd $SLURM_SUBMIT_DIR
make clean && make

export OMP_NUM_THREADS=32
./scripts/run_experiments.sh
```

Submit job:
```bash
sbatch run_benchmark.sh
```

## Troubleshooting

### Thread Sanitizer Warnings

```bash
# Build with sanitizer
make sanitize

# Run with sanitizer options
TSAN_OPTIONS='history_size=7' ./bin/correctness_test
```

### Performance Issues

1. **Check CPU affinity**: `export OMP_PROC_BIND=true`
2. **Disable dynamic threads**: `export OMP_DYNAMIC=false`
3. **Set thread count**: `export OMP_NUM_THREADS=8`
4. **Check CPU frequency scaling**: May need to disable for consistent results

### Build Errors

- Ensure GCC >= 7.0 with OpenMP support
- Check that `-fopenmp` flag is working: `gcc -fopenmp -v`

## References

1. Herlihy, M., Shavit, N. (2008). *The Art of Multiprocessor Programming*
2. Harris, T. (2001). "A Pragmatic Implementation of Non-Blocking Linked-Lists"
3. Michael, M. (2004). "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects"
4. Pugh, W. (1990). "Skip Lists: A Probabilistic Alternative to Balanced Trees"

## Team Members

[Add your team member names here]

## License

This project is for educational purposes as part of a concurrent programming course.

## Acknowledgments

- Virginia Tech Advanced Research Computing for computational resources
- OpenMP community for parallel programming support