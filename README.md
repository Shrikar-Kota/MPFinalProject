# Concurrent Skip List Implementation

A comprehensive implementation and performance analysis of three concurrent skip list variants with different synchronization strategies.

## Author
[Your Name]  
Parallel Programming Course  
November 2024

---

## Overview

This project implements three concurrent skip list variants:
1. **Coarse-grained locking** - Single global lock
2. **Fine-grained locking** - Lock-free reads with epoch-based reclamation
3. **Lock-free** - Optimized lock-free contains operations

Key findings:
- Lock-free reads achieve **12.7× higher throughput** than coarse-grained for read-only workloads
- Write serialization limits scalability in mixed workloads
- Pragmatic hybrid approaches provide excellent real-world performance

---

## Project Structure

```
MPFinalProject/
├── src/
│   ├── skiplist_coarse.c       # Coarse-grained implementation
│   ├── skiplist_fine.c         # Fine-grained with epoch reclamation
│   ├── skiplist_lockfree.c     # Lock-free optimized
│   ├── skiplist_common.h       # Shared definitions
│   ├── skiplist_utils.c        # Utility functions
│   └── benchmark.c             # Performance benchmarking
├── tests/
│   └── correctness_test.c      # Correctness validation
├── scripts/
│   ├── run_experiments.sh      # Automated experiment runner
│   └── plot_results.py         # Results visualization
├── results/
│   ├── results_*.csv           # Experimental data
│   └── plots/                  # Generated visualizations
├── Makefile                    # Build system
├── README.md                   # This file
└── SKIP_LIST_REPORT.pdf        # Comprehensive analysis report
```

---

## Quick Start

### Prerequisites
- GCC compiler with OpenMP support
- Python 3 with pandas and matplotlib (for plotting)
- Make build system
- Linux/Unix environment

### Build
```bash
make clean && make
```

### Run Tests
```bash
make test
```

All 12 correctness tests should pass:
- Coarse-grained: 4/4 ✓
- Fine-grained: 4/4 ✓
- Lock-free: 4/4 ✓

---

## Usage

### Run Single Benchmark
```bash
./bin/benchmark --impl [coarse|fine|lockfree] \
                --threads <num_threads> \
                --ops <operations> \
                --workload [insert|readonly|mixed|delete]
```

**Example:**
```bash
# Test lock-free with 8 threads, mixed workload
./bin/benchmark --impl lockfree --threads 8 --ops 100000 --workload mixed
```

### Run Full Experiments
```bash
./scripts/run_experiments.sh
```

This runs:
- **Experiment 1:** Scalability test (1-32 threads)
- **Experiment 2:** Workload comparison (insert, readonly, mixed, delete)
- **Experiment 3:** Contention study (varying key ranges)

**Expected runtime:** ~30-45 minutes

**Output:** Results saved to `results/results_TIMESTAMP.csv`

### Generate Plots
```bash
python3 scripts/plot_results.py results/results_*.csv
```

**Output:** Plots saved to `results/plots/`
- `scalability.png` - Throughput vs thread count
- `speedup.png` - Speedup analysis
- `workload_comparison.png` - Performance across workloads
- `contention.png` - Contention effects

---

## Benchmark Parameters

### Default Configuration
- **Operations per thread:** 1,000,000
- **Key range:** 100,000
- **Warmup operations:** 10,000
- **Thread counts:** 1, 2, 4, 8, 16, 32

### Workload Types
- **insert:** 100% insert operations
- **readonly:** 100% contains (search) operations
- **mixed:** 50% insert, 25% delete, 25% contains
- **delete:** 100% delete operations (requires pre-population)

### Custom Parameters
```bash
./bin/benchmark --impl lockfree \
                --threads 16 \
                --ops 500000 \
                --key-range 50000 \
                --workload mixed \
                --initial-size 10000 \
                --warmup 5000 \
                --csv
```

---

## Implementation Details

### Coarse-Grained Locking
- **Strategy:** Single global lock protects all operations
- **Complexity:** ~176 lines
- **Pros:** Simple, provably correct
- **Cons:** Limited parallelism

### Fine-Grained Locking
- **Strategy:** Lock for writes, lock-free reads with epoch-based reclamation
- **Complexity:** ~280 lines
- **Pros:** Excellent read scalability, production-quality
- **Cons:** More complex implementation

**Key Features:**
- Epoch-based memory reclamation (Fraser 2004)
- Lock-free contains() operation
- Optimized search caching predecessors

### Lock-Free
- **Strategy:** Pragmatic hybrid - locks for writes, lock-free reads
- **Complexity:** ~150 lines
- **Pros:** Best read performance, simple and reliable
- **Cons:** Writes still serialized

**Key Features:**
- Wait-free contains operation
- Logical deletion with marked flag
- Full skip list structure (all levels)

---

## Performance Results

### Key Findings (8 threads)

**Read-Only Workload:**
| Implementation | Throughput |
|----------------|------------|
| Coarse-grained | 1.49 M ops/s |
| Fine-grained | 6.74 M ops/s |
| **Lock-free** | **18.92 M ops/s** |

**Lock-free is 12.7× faster for reads!**

**Mixed Workload:**
| Implementation | Throughput |
|----------------|------------|
| Coarse-grained | 0.85 M ops/s |
| Fine-grained | 1.23 M ops/s |
| Lock-free | 1.23 M ops/s |

All implementations show limited scalability for write-heavy workloads due to lock contention.

See `SKIP_LIST_REPORT.pdf` for comprehensive analysis.

---

## Troubleshooting

### Build Errors
```bash
# Clean and rebuild
make clean && make

# Check GCC version (need OpenMP support)
gcc --version

# Verify OpenMP
gcc -fopenmp test.c -o test
```

### Test Failures
```bash
# Run tests multiple times to check for race conditions
for i in {1..10}; do make test || break; done

# Run with specific implementation
./bin/correctness_test coarse
./bin/correctness_test fine
./bin/correctness_test lockfree
```

### Plotting Issues
```bash
# Install required Python packages
pip3 install pandas matplotlib seaborn

# Check if results exist
ls -lh results/*.csv

# Run plotting manually
python3 scripts/plot_results.py results/results_TIMESTAMP.csv
```

---

## Development

### Adding New Tests
Edit `tests/correctness_test.c` and add to the test suite.

### Modifying Benchmark
Edit `src/benchmark.c` to change workload generation or metrics.

### Code Style
- Use consistent indentation (4 spaces)
- Document complex algorithms
- Follow existing naming conventions

---

## References

1. Pugh, W. (1990). "Skip lists: a probabilistic alternative to balanced trees." *Communications of the ACM*

2. Harris, T. L. (2001). "A pragmatic implementation of non-blocking linked-lists." *DISC*

3. Fraser, K. (2004). "Practical lock freedom." PhD Thesis, University of Cambridge

4. Herlihy, M., & Shavit, N. (2008). *The Art of Multiprocessor Programming*. Morgan Kaufmann

---

## License

Academic project for educational purposes.

---

## Contact

[Your Name]  
[Your Email]  
[Course Information]

---

## Acknowledgments

- Course instructor and TAs
- VT ARC Computing Cluster
- OpenMP development team
- Referenced research papers and implementations