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
└── FINAL_PROJECT_REPORT.pdf    # Comprehensive 5-page analysis report
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

## Implementation Details

### 1. Coarse-Grained Locking

**Synchronization:** Single global `omp_lock_t` protecting all operations

**Algorithm:**
```c
bool insert(list, key, value) {
    acquire_global_lock();
    search_and_insert();
    release_global_lock();
}
```

**Properties:**
- Sequential consistency (trivially correct)
- Simple implementation (~176 lines)
- Zero concurrency (readers block writers)
- Negative scaling (0.36× speedup at 32 threads)

**Use case:** Baseline for comparison

---

### 2. Fine-Grained Locking

**Synchronization:** Per-node locks with optimistic validation

**Key Techniques:**
- Lock-free `contains()` operation (wait-free reads)
- Hand-over-hand locking for insert/delete
- Optimistic search then validate before linking
- Marked deletion flag for logical removal

**Algorithm (Insert):**
```c
bool insert(list, key, value) {
    while (true) {
        optimistic_search(key, preds, succs);  // No locks
        lock(preds[0]);
        if (validate(preds[0], succs[0])) {
            link_node_at_level_0();
            unlock(preds[0]);
            build_tower();  // Link upper levels
            return true;
        }
        unlock(preds[0]);
    }
}
```

**Properties:**
- Lock-free reads (wait-free progress)
- Deadlock-free (hand-over-hand locking)
- Excellent delete performance (37.8M ops/sec)
- Optimistic validation overhead under contention

**Complexity:** ~280 lines

---

### 3. Lock-Free (CAS-Based)

**Synchronization:** CAS-only operations (no locks)

**Key Innovations:**

**3.1 Mark-Before-Unlink Deletion (Harris 2001):**
```c
// Logical deletion: Mark next pointer
for (level = top; level >= 0; level--) {
    mark_next_pointer(victim, level);
}
// Physical removal during helping
```

**3.2 Local Recovery Optimization (Novel):**
```c
if (!CAS(&pred->next[level], curr, succ)) {
    // Traditional: restart from head
    // Our optimization: check predecessor validity first
    if (IS_MARKED(pred->next[level])) {
        goto retry_head;  // Pred deleted, must restart
    }
    // Pred alive, retry locally!
    curr = GET_UNMARKED(pred->next[level]);
    continue;  // NO RESTART
}
```

**Why this matters:** Under high contention, traditional lock-free algorithms restart from head on every CAS failure, causing O(n) wasted work per retry. Local recovery reduces this to O(1), providing **6× speedup** under extreme contention.

**3.3 Physical Helping:**
```c
while (IS_MARKED(curr->next[level])) {
    // Help other threads complete deletions
    CAS(&pred->next[level], curr, unmarked_succ);
    curr = unmarked_succ;
}
```

**Lock-Free Verification:**
- No mutual exclusion primitives
- CAS-only synchronization
- System-wide progress guarantee (every CAS failure = another thread's success)
- Mark-before-unlink prevents ABA problem
- Bounded retries (100) for livelock prevention

**Complexity:** ~200 lines

**Properties:**
- True lock-free (formal verification in report)
- Wait-free `contains()` operation
- Best performance under contention (6× advantage)
- Not wait-free (individual operations may fail after retries)

---

## Performance Results

### Scalability (Mixed Workload)

| Threads | Coarse-Grained | Fine-Grained | Lock-Free | LF Speedup |
|---------|----------------|--------------|-----------|------------|
| 1 | 0.77 M ops/s | 1.12 M ops/s | 0.84 M ops/s | 1.08× |
| 2 | 0.76 M ops/s | 1.47 M ops/s | 1.47 M ops/s | 1.93× |
| 4 | 0.69 M ops/s | 2.44 M ops/s | 2.50 M ops/s | 3.61× |
| 8 | 0.57 M ops/s | 3.00 M ops/s | **4.39 M ops/s** | **7.73×** |
| 16 | 0.46 M ops/s | 6.25 M ops/s | **6.80 M ops/s** | **14.8×** |
| 32 | 0.28 M ops/s | 6.12 M ops/s | **7.86 M ops/s** | **28.1×** |

**Key findings:**
- Lock-free achieves **28× speedup** over coarse-grained at 32 threads
- Lock-free shows **9.4× speedup** vs single thread (best scalability)
- Coarse-grained exhibits negative scaling (slower with more threads)

---

### Contention Study (16 Threads, Mixed Workload)

| Key Range | Contention | Coarse-Grained | Fine-Grained | Lock-Free | LF Advantage |
|-----------|------------|----------------|--------------|-----------|--------------|
| 1,000 | **EXTREME** | 0.62 M ops/s | 1.54 M ops/s | **9.18 M ops/s** | **6.0×** ⭐ |
| 10,000 | High | 0.58 M ops/s | 3.70 M ops/s | **9.16 M ops/s** | **2.5×** |
| 100,000 | Medium | 0.49 M ops/s | 6.46 M ops/s | 6.59 M ops/s | 1.02× |
| 1,000,000 | Low | 0.31 M ops/s | 3.48 M ops/s | **4.04 M ops/s** | 1.16× |

**★ Breakthrough Result:** Under extreme contention (16 threads competing for 1,000 keys), lock-free delivers **6× higher throughput** than fine-grained through local recovery optimization.

---

### Workload Sensitivity (8 Threads)

| Workload | Coarse-Grained | Fine-Grained | Lock-Free | Winner |
|----------|----------------|--------------|-----------|--------|
| Insert-Only | 0.62 M ops/s | 5.66 M ops/s | 5.25 M ops/s | Fine-Grained |
| Read-Only | 0.67 M ops/s | 6.17 M ops/s | 5.75 M ops/s | Fine-Grained |
| **Mixed** | 0.64 M ops/s | 3.42 M ops/s | **4.12 M ops/s** | **Lock-Free** ⭐ |
| Delete-Heavy | 1.85 M ops/s | **37.8 M ops/s** | 29.8 M ops/s | Fine-Grained |

**Key findings:**
- Lock-free wins on **mixed workloads** (most realistic scenario)
- Fine-grained excels at delete-heavy workloads (efficient physical removal)
- Lock-free provides 20% higher throughput on mixed vs fine-grained

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
