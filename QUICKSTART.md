# Quick Start Guide

## Getting Started in 5 Minutes

### 1. Build the Project
```bash
cd skiplist-lockfree
make
```

### 2. Run Correctness Tests
```bash
make test
```

Expected output: All tests should pass ✓

### 3. Run a Quick Benchmark
```bash
# Test lock-free implementation with 8 threads
./bin/benchmark --impl lockfree --threads 8 --ops 100000 --workload mixed
```

### 4. Compare All Implementations
```bash
# Quick comparison
for impl in coarse fine lockfree; do
    echo "Testing $impl..."
    ./bin/benchmark --impl $impl --threads 4 --ops 50000 --workload mixed
done
```

## Typical Workflow

### For Development and Testing
```bash
# 1. Make changes to source code
vim src/skiplist_lockfree.c

# 2. Rebuild
make clean && make

# 3. Test correctness
make test

# 4. Quick performance check
./bin/benchmark --impl lockfree --threads 8 --workload mixed
```

### For Report Generation

#### Step 1: Run Experiments (10-30 minutes)
```bash
./scripts/run_experiments.sh
```

This generates: `results/results_TIMESTAMP.csv`

#### Step 2: Generate Plots
```bash
# Install Python dependencies if needed
pip3 install matplotlib pandas seaborn

# Generate all plots
python3 scripts/plot_results.py results/results_TIMESTAMP.csv
```

This creates plots in: `results/plots/`

#### Step 3: Analyze Results
Check the following files:
- `results/plots/scalability.png` - Main scalability results
- `results/plots/speedup.png` - Speedup comparison
- `results/plots/workload_comparison.png` - Workload analysis
- `results/plots/summary_statistics.csv` - Numerical summary

#### Step 4: Write Report
Use `REPORT_TEMPLATE.md` as a guide and include the generated plots.

## Common Commands

### Testing
```bash
# Basic correctness
make test

# With thread sanitizer (detects race conditions)
make sanitize
./bin/correctness_test

# Debug build
make debug
gdb ./bin/benchmark
```

### Benchmarking
```bash
# Insert-only workload
./bin/benchmark --impl lockfree --threads 16 --workload insert

# Read-heavy workload (90% reads)
./bin/benchmark --impl lockfree --threads 8 --workload readonly --initial-size 50000

# High contention test
./bin/benchmark --impl lockfree --threads 32 --key-range 1000

# Export to CSV for analysis
./bin/benchmark --impl lockfree --threads 8 --csv > my_results.csv
```

### Visualization
```bash
# From experiment results
python3 scripts/plot_results.py results/results_*.csv

# From custom benchmark
./bin/benchmark --impl lockfree --threads 4 --csv > test.csv
# Add CSV header manually if needed
python3 scripts/plot_results.py test.csv
```

## Troubleshooting

### Build Errors
```bash
# Check GCC version (need >= 7.0)
gcc --version

# Check OpenMP support
gcc -fopenmp -v

# Clean and rebuild
make clean && make
```

### Runtime Errors
```bash
# Segmentation fault? Try with sanitizer
make sanitize
./bin/correctness_test

# Performance issues? Check CPU affinity
export OMP_PROC_BIND=true
export OMP_NUM_THREADS=8
./bin/benchmark
```

### Plot Generation Issues
```bash
# Install Python dependencies
pip3 install matplotlib pandas seaborn

# Check Python version (need >= 3.6)
python3 --version
```

## Performance Tips

1. **Disable CPU frequency scaling** for consistent results:
   ```bash
   # On Linux (requires root)
   sudo cpupower frequency-set --governor performance
   ```

2. **Set CPU affinity**:
   ```bash
   export OMP_PROC_BIND=true
   export OMP_PLACES=cores
   ```

3. **Disable dynamic thread adjustment**:
   ```bash
   export OMP_DYNAMIC=false
   ```

4. **Run multiple times** and average results to reduce noise.

## Next Steps

1. ✅ Verify everything builds and tests pass
2. ✅ Run quick benchmarks to understand performance characteristics
3. ✅ Run full experiment suite
4. ✅ Generate and analyze plots
5. ✅ Write report using template

## Getting Help

- Check `README.md` for detailed documentation
- Review `REPORT_TEMPLATE.md` for report structure
- Examine test output for correctness issues
- Use `--help` flag on benchmark tool for options

## File Overview

**Source Code:**
- `src/skiplist_common.h` - Shared definitions
- `src/skiplist_lockfree.c` - Main lock-free implementation
- `src/skiplist_coarse.c` - Baseline coarse-grained
- `src/skiplist_fine.c` - Fine-grained locking

**Testing:**
- `tests/correctness_test.c` - Validation suite
- `src/benchmark.c` - Performance testing

**Scripts:**
- `scripts/run_experiments.sh` - Automated benchmarks
- `scripts/plot_results.py` - Visualization

**Documentation:**
- `README.md` - Complete documentation
- `REPORT_TEMPLATE.md` - Report writing guide
- `QUICKSTART.md` - This file