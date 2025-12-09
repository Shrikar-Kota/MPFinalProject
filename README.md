# Lock-Free Skip List: Concurrent Data Structure Implementation

A comprehensive implementation and performance analysis of three concurrent skip list variants with different synchronization strategies, achieving 28× speedup over coarse-grained locking and 6× advantage under extreme contention.

## Author
Shrikar Reddy Kota | Rohit Kumar Salla  
CS/ECE 5510 - Multiprocessor Programming  
Virginia Tech  
November 2024

---

## Overview

This project implements and evaluates three concurrent skip list variants:

1. **Coarse-Grained Locking** - Single global lock protecting all operations
2. **Fine-Grained Locking** - Per-node locks with optimistic validation and lock-free reads
3. **Lock-Free** - CAS-based synchronization with mark-before-unlink deletion and local recovery optimization

### Key Achievements

- **28× speedup** over coarse-grained at 32 threads (7.86M vs 0.28M ops/sec)
- **6× throughput advantage** under extreme contention (9.18M vs 1.54M ops/sec)
- **True lock-free implementation** with formal verification of progress guarantees
- **Novel local recovery optimization** preventing restart cascades on CAS failures

---

## Project Structure

```
MPFinalProject/
├── src/
│   ├── skiplist_coarse.c       # Coarse-grained implementation (global lock)
│   ├── skiplist_fine.c         # Fine-grained locking (per-node locks)
│   ├── skiplist_lockfree.c     # Lock-free (CAS-based, Harris algorithm)
│   ├── skiplist_common.h       # Shared data structures and macros
│   ├── skiplist_utils.c        # Node creation, random level, validation
│   └── benchmark.c             # Performance benchmarking framework
├── tests/
│   └── correctness_test.c      # Correctness validation (12 tests)
├── scripts/
│   ├── run_experiments.sh      # Automated experiment runner (42 configs)
│   └── plot_results.py         # Figure generation for report
├── figures/                     # Publication-quality figures (300 DPI)
│   ├── figure1_scalability.png
│   ├── figure2_speedup.png
│   ├── figure3_workload.png
│   ├── figure4_contention.png
│   └── figure5_comparison.png
├── results/
│   └── results_TIMESTAMP.csv   # Experimental data (42 configurations)
├── Makefile                     # Build system
├── README.md                    # This file
└── REPORT.md    # Comprehensive 5-page analysis report (MD File)
└── REPORT.pdf   # Comprehensive 5-page analysis report (PDF File)
```

---

## Quick Start

### Prerequisites

**System Requirements:**
- GCC compiler with OpenMP support (GCC 7+)
- Linux/Unix environment (tested on Virginia Tech ARC cluster)
- Make build system

**Python Requirements (for plotting):**
```bash
pip3 install pandas matplotlib seaborn
```

### Build

```bash
# Clone/navigate to project directory
cd MPFinalProject

# Build all targets
make clean && make
```

**Build outputs:**
- `bin/benchmark` - Performance benchmarking tool
- `bin/correctness_test` - Correctness validation suite

### Run Correctness Tests

```bash
make test
```

**Expected output:**
```
Skip List Correctness Tests
============================
Coarse-Grained Implementation:
  test_basic... PASS
  test_sequential... PASS
  test_concurrent... PASS
  test_mixed... PASS
Fine-Grained Implementation:
  test_basic... PASS
  test_sequential... PASS
  test_concurrent... PASS
  test_mixed... PASS
Lock-Free Implementation:
  test_basic... PASS
  test_sequential... PASS
  test_concurrent... PASS
  test_mixed... PASS
============================
All 12 tests PASSED ✓
```

---

## Usage

### Run Single Benchmark

```bash
./bin/benchmark --impl [coarse|fine|lockfree] \
                --threads <num_threads> \
                --ops <operations_per_thread> \
                --key-range <key_space_size> \
                --workload [insert|readonly|mixed|delete] \
                [--csv]
```

**Example - Lock-free with 16 threads:**
```bash
./bin/benchmark --impl lockfree \
                --threads 16 \
                --ops 1000000 \
                --key-range 100000 \
                --workload mixed \
                --csv
```

**Example output:**
```
impl,threads,workload,ops,key_range,time,throughput,successful,failed
lockfree,16,mixed,16000000,100000,2.3520,6802775.89,8594953,7405047
```

### Run Complete Experimental Suite

```bash
./scripts/run_experiments.sh
```

**This runs 42 benchmark configurations:**
- **Experiment 1:** Scalability (6 thread counts × 3 implementations = 18 runs)
- **Experiment 2:** Workload sensitivity (4 workloads × 3 implementations = 12 runs)
- **Experiment 3:** Contention study (4 key ranges × 3 implementations = 12 runs)

**Runtime:** ~30-60 minutes (depending on hardware)

**Output:** `results/results_TIMESTAMP.csv` with complete experimental data

### Generate Publication-Quality Figures

```bash
python3 scripts/plot_results.py results/results_TIMESTAMP.csv
```

**Output:** 5 figures in `figures/` directory:
- `figure1_scalability.png` - Throughput vs thread count (mixed workload)
- `figure2_speedup.png` - Speedup relative to single-threaded baseline
- `figure3_workload.png` - Performance across insert/readonly/mixed/delete workloads
- `figure4_contention.png` - Performance under varying contention levels (★ 6× result)
- `figure5_comparison.png` - Peak performance comparison bar chart

All figures are 300 DPI, publication-ready with annotations.

---

## Benchmark Parameters

### Command-Line Options

| Parameter | Description | Default |
|-----------|-------------|---------|
| `--impl` | Implementation type: coarse, fine, lockfree | lockfree |
| `--threads` | Number of parallel threads | 4 |
| `--ops` | Operations per thread | 100,000 |
| `--key-range` | Size of key space [0, N) | 10,000 |
| `--workload` | Workload type (see below) | mixed |
| `--initial-size` | Pre-populate list with N elements | 0 |
| `--warmup` | Warmup operations before timing | 1,000 |
| `--insert-pct` | Insert percentage (mixed only) | 30 |
| `--delete-pct` | Delete percentage (mixed only) | 20 |
| `--csv` | Output in CSV format | (off) |

### Workload Types

| Workload | Composition | Use Case |
|----------|-------------|----------|
| `insert` | 100% insert | Write-heavy stress test |
| `readonly` | 100% contains | Read-only scalability |
| `mixed` | 50% insert, 25% delete, 25% contains | Realistic concurrent usage |
| `delete` | 100% delete | Requires `--initial-size` |

**Note:** Mixed workload shows ~50% success rate due to random keys—duplicates and non-existent keys correctly return false.

### Example Custom Configuration

```bash
# High contention test (16 threads, small key space)
./bin/benchmark --impl lockfree \
                --threads 16 \
                --ops 1000000 \
                --key-range 1000 \
                --workload mixed \
                --csv
```

---

## Troubleshooting

### Build Errors

```bash
# Clean and rebuild
make clean && make

# Check GCC version (need 7+)
gcc --version

# Verify OpenMP support
echo '#include <omp.h>
int main() { return omp_get_num_threads(); }' | gcc -xc -fopenmp - && echo "OpenMP OK"
```

### Test Failures

```bash
# Run tests multiple times to detect race conditions
for i in {1..10}; do 
    make test || break
done

# Enable verbose output
./bin/correctness_test 2>&1 | tee test_output.log
```

### Benchmark Issues

**Problem:** Segmentation fault
```bash
# Increase stack size (skip lists can be tall)
ulimit -s unlimited
./bin/benchmark --impl lockfree --threads 32
```

**Problem:** Low throughput
```bash
# Disable CPU frequency scaling
sudo cpupower frequency-set -g performance

# Pin to physical cores
OMP_PROC_BIND=true ./bin/benchmark --impl lockfree --threads 16
```

### Plotting Issues

```bash
# Install dependencies
pip3 install --user pandas matplotlib seaborn

# Verify CSV format
head -5 results/results_*.csv

# Test plotting
python3 scripts/plot_results.py results/results_20241129_040718.csv
```

---

## Development

### Code Organization

- **Shared definitions:** `skiplist_common.h` (data structures, macros)
- **Marked pointer macros:** `IS_MARKED()`, `GET_UNMARKED()`, `GET_MARKED()`
- **Atomic operations:** C11 `<stdatomic.h>` with sequential consistency
- **Random level generation:** Thread-local seeds with nanosecond precision

### Adding New Implementations

1. Create `skiplist_yourname.c`
2. Implement interface from `skiplist_common.h`:
   ```c
   SkipList* skiplist_create_yourname(void);
   bool skiplist_insert_yourname(SkipList*, int, int);
   bool skiplist_delete_yourname(SkipList*, int);
   bool skiplist_contains_yourname(SkipList*, int);
   void skiplist_destroy_yourname(SkipList*);
   ```
3. Add to `benchmark.c` and `correctness_test.c`
4. Update `Makefile`

### Performance Tuning

**Lock-free parameters** (in `skiplist_lockfree.c`):
```c
#define YIELD_THRESHOLD 3        // Yield after N CAS failures
#define MAX_RETRIES 100          // Abort after N attempts
#define BACKOFF_MAX_SPINS 4096   // Exponential backoff ceiling
```

**Skip list parameters** (in `skiplist_common.h`):
```c
#define MAX_LEVEL 16    // Maximum skip list height
#define P_FACTOR 0.5    // Level promotion probability
```

---

## Technical Details

### Memory Model

- **C11 atomics** with sequential consistency
- **Memory ordering:** Total ordering across all threads
- **No memory reclamation:** Deleted nodes leaked (acceptable for benchmarks)

### Linearization Points

| Operation | Coarse-Grained | Fine-Grained | Lock-Free |
|-----------|----------------|--------------|-----------|
| Insert | Lock acquisition | Level-0 CAS with lock held | Level-0 CAS |
| Delete | Lock acquisition | Mark flag set | Level-0 mark |
| Contains | Lock acquisition | Unmarked node observation | Unmarked node observation |

### Progress Guarantees

| Implementation | Contains | Insert/Delete |
|----------------|----------|---------------|
| Coarse-Grained | Blocking | Blocking |
| Fine-Grained | Wait-free | Lock-free |
| Lock-Free | Wait-free | Lock-free |

**Lock-free definition:** At least one thread makes progress in finite steps (system-wide progress, not per-thread).

---

## References

1. **Pugh, W. (1990).** "Skip lists: a probabilistic alternative to balanced trees." *Communications of the ACM*, 33(6), 668-676.

2. **Harris, T. L. (2001).** "A pragmatic implementation of non-blocking linked-lists." *International Symposium on Distributed Computing* (DISC), 300-314.

3. **Fraser, K. (2004).** "Practical lock freedom." *PhD Thesis*, University of Cambridge.

4. **Michael, M. M. (2004).** "Hazard pointers: Safe memory reclamation for lock-free objects." *IEEE Transactions on Parallel and Distributed Systems*, 15(6), 491-504.

5. **Herlihy, M., & Shavit, N. (2008).** *The Art of Multiprocessor Programming*. Morgan Kaufmann.

6. **Fraser, K., & Harris, T. (2007).** "Concurrent programming without locks." *ACM Transactions on Computer Systems*, 25(2), Article 5.

7. **Linden, J., & Jonsson, B. (2013).** "A skiplist-based concurrent priority queue with minimal memory contention." *International Conference on Principles of Distributed Systems*, 206-220.
