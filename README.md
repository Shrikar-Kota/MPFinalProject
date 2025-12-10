# Lock-Free Skip List: Concurrent Data Structure Implementation

A comprehensive implementation and performance analysis of three concurrent skip list variants with different synchronization strategies, achieving **27× speedup** over coarse-grained locking and **4.3× advantage** under extreme contention.

## Author
Shrikar Reddy Kota | Rohit Kumar Salla  
CS/ECE 5510 - Multiprocessor Programming  
Virginia Tech  
December 2025

---

## Overview

This project implements and evaluates three concurrent skip list variants:

1. **Coarse-Grained Locking** - Single global lock protecting all operations
2. **Fine-Grained Locking** - Per-node locks with optimistic validation and lock-free reads
3. **Lock-Free** - CAS-based synchronization with mark-before-unlink deletion and local recovery optimization

### Key Achievements

- **27× speedup** over coarse-grained at 32 threads
- **4.3× throughput advantage** under extreme contention through novel local recovery optimization
- **Peak performance** of 9.45M ops/sec at 16 threads
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
│   ├── figure4_contention.png   # Shows 4.3× breakthrough
│   └── figure5_comparison.png
├── results/
│   └── results_TIMESTAMP.csv   # Experimental data (42 configurations)
├── Makefile                     # Build system
├── README.md                    # This file
└── REPORT.pdf    # Comprehensive 5-page analysis report
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
./bin/benchmark <impl> <threads> <workload> <ops> <key_range>
```

**Parameters:**
- `impl`: Implementation type (`coarse`, `fine`, `lockfree`)
- `threads`: Number of parallel threads (1-32)
- `workload`: Workload type (`insert`, `readonly`, `mixed`, `delete`)
- `ops`: Total operations to perform (e.g., 8000000)
- `key_range`: Size of key space [0, N) (e.g., 100000)

**Example - Lock-free with 16 threads:**
```bash
./bin/benchmark lockfree 16 mixed 16000000 100000
```

**Example output:**
```
Implementation: lockfree
Threads: 16
Workload: mixed
Operations: 16000000
Key Range: 100000
Time: 1.69 seconds
Throughput: 9.45M ops/sec
Successful: 8592473
Failed: 7407527
```

**Example - Test extreme contention scenario:**
```bash
./bin/benchmark lockfree 16 mixed 16000000 1000
```

### Run Complete Experimental Suite

```bash
./scripts/run_experiments.sh
```

**This runs 42 benchmark configurations:**
- **Experiment 1:** Scalability (6 thread counts × 3 implementations = 18 runs)
  - Threads: 1, 2, 4, 8, 16, 32
  - Workload: Mixed (50% insert, 25% delete, 25% contains)
  - Key range: 100,000
  
- **Experiment 2:** Workload sensitivity (4 workloads × 3 implementations = 12 runs)
  - Fixed: 8 threads, 100,000 key range
  - Workloads: Insert-only, Read-only, Mixed, Delete-heavy
  
- **Experiment 3:** Contention study (4 key ranges × 3 implementations = 12 runs) ⭐
  - Fixed: 16 threads, Mixed workload
  - Key ranges: 1,000 (extreme), 10,000 (high), 100,000 (medium), 1,000,000 (low)
  - This reveals the **4.3× breakthrough result**

**Runtime:** ~30-60 minutes (depending on hardware)

**Output:** `results/results_TIMESTAMP.csv` with complete experimental data

### Generate Publication-Quality Figures

```bash
python3 scripts/plot_results.py results/results_TIMESTAMP.csv
```

**Output:** 5 figures in `figures/` directory:
- `figure1_scalability.png` - Throughput vs thread count 
- `figure2_speedup.png` - Speedup relative to single-threaded baseline
- `figure3_workload.png` - Performance across workload types
- `figure4_contention.png` - **Contention study showing lock-free advantage**
- `figure5_comparison.png` - Peak performance comparison

All figures are 300 DPI, publication-ready with annotations.

---

## Understanding the Results

### Success vs Failed Operations

**Important:** "Failed" operations are **not errors** but correct behavior:

- **Insert:** 
  - Success = new key inserted
  - Failed = duplicate key already exists (correct rejection)
  
- **Delete:** 
  - Success = key found and removed
  - Failed = key not in list (correct behavior)
  
- **Contains:** 
  - Success = key found
  - Failed = key not in list (correct behavior)

**Mixed workload** shows ~50% success rate because:
- Random keys from range [0, 100K)
- Duplicate inserts correctly return false
- Searches for non-existent keys correctly return false
- **All operations (successful + failed) contribute to throughput**

### Why Lock-Free Wins Under Contention

**Traditional lock-free skip lists** restart from head on every CAS failure:
```
1. Start at head
2. Traverse O(log n) levels to find position
3. CAS fails → RESTART FROM HEAD  ← O(n) wasted work!
4. Repeat...
```

**Our local recovery optimization:**
```
1. Start at head
2. Traverse O(log n) levels to find position
3. CAS fails → Check if predecessor still valid
4. If valid: Retry locally (O(1) work)  ← Game changer!
5. If deleted: Restart from head (rare)
```

**Result:** At extreme contention (16 threads, 1000 keys), this optimization provides **4.3× speedup** (12.8M vs 2.94M ops/sec) by converting expensive O(n) restarts into cheap O(1) local retries.

---

## Workload Types

| Workload | Composition | Use Case |
|----------|-------------|----------|
| `insert` | 100% insert | Write-heavy stress test |
| `readonly` | 100% contains | Read-only scalability |
| `mixed` | 50% insert, 25% delete, 25% contains | Realistic concurrent usage |
| `delete` | 100% delete | Requires pre-population |

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

# If tests fail, check implementation correctness
./bin/correctness_test
```

### Benchmark Issues

**Problem:** Segmentation fault
```bash
# Increase stack size (skip lists can be tall)
ulimit -s unlimited
./bin/benchmark lockfree 32 mixed 32000000 100000
```

**Problem:** Low throughput compared to reported results
```bash
# Disable CPU frequency scaling
sudo cpupower frequency-set -g performance

# Pin threads to physical cores
export OMP_PROC_BIND=true
./bin/benchmark lockfree 16 mixed 16000000 100000
```

**Problem:** High variance in results
```bash
# Run multiple times and average
for i in {1..5}; do
    ./bin/benchmark lockfree 16 mixed 16000000 100000
done
```

### Plotting Issues

```bash
# Install dependencies
pip3 install --user pandas matplotlib seaborn

# Verify CSV format
head -5 results/results_*.csv

# Should show:
# impl,threads,workload,ops,key_range,time,throughput,successful,failed
# lockfree,16,mixed,16000000,100000,1.6934,9448433.51,...

# Test plotting
python3 scripts/plot_results.py results/results_*.csv
```

---

## Technical Details

### Memory Model

- **C11 atomics** with sequential consistency
- **Memory ordering:** Total ordering across all threads
- **No memory reclamation:** Deleted nodes leaked (acceptable for finite benchmarks)
  - Production systems would use epoch-based reclamation or hazard pointers
  - For benchmark workloads (finite duration), memory growth is bounded

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
| Fine-Grained | Wait-free | Lock-free (optimistic) |
| Lock-Free | Wait-free | Lock-free (CAS-based) |

**Lock-free definition:** At least one thread makes progress in finite steps (system-wide progress, not per-thread). Each CAS failure indicates another thread's success
