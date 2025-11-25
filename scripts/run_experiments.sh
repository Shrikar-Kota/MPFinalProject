cd ~/MPFinalProject

cat > scripts/run_experiments_fixed.sh << 'EOF'
#!/bin/bash
OUTPUT_DIR="results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="${OUTPUT_DIR}/results_${TIMESTAMP}.csv"
mkdir -p ${OUTPUT_DIR}

IMPLEMENTATIONS=("coarse" "fine" "lockfree")
THREAD_COUNTS=(1 2 4 8 16 32)
WORKLOADS=("insert" "readonly" "mixed" "delete")
OPS_PER_THREAD=1000000
KEY_RANGE=100000

echo "========================================="
echo "Lock-Free Skip List Benchmark Suite"
echo "========================================="
echo "Results will be saved to: ${RESULTS_FILE}"
echo ""

echo "impl,threads,workload,ops,key_range,time,throughput,successful,failed" > ${RESULTS_FILE}

run_benchmark() {
    local impl=$1
    local threads=$2
    local workload=$3
    local ops=$4
    local key_range=$5
    local initial_size=$6
    
    echo "Running: impl=$impl threads=$threads workload=$workload" >&2
    
    # Run benchmark and capture output
    local result=$(./bin/benchmark \
        --impl $impl \
        --threads $threads \
        --ops $ops \
        --key-range $key_range \
        --workload $workload \
        --initial-size $initial_size \
        --warmup 10000 \
        --csv 2>&1)
    
    # Append result to file
    echo "$result" | grep -v "^$" >> ${RESULTS_FILE}
    
    echo "  ✓ Done" >&2
}

echo ""
echo "=== Experiment 1: Scalability Test ===" >&2
total=18
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
        ((current++))
        echo "[$current/$total]" >&2
        run_benchmark $impl $threads "mixed" $OPS_PER_THREAD $KEY_RANGE 5000
    done
done

echo ""
echo "=== Experiment 2: Workload Comparison ===" >&2
FIXED_THREADS=8
total=12
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for workload in "${WORKLOADS[@]}"; do
        ((current++))
        echo "[$current/$total]" >&2
        if [ "$workload" == "delete" ] || [ "$workload" == "readonly" ]; then
            run_benchmark $impl $FIXED_THREADS $workload $OPS_PER_THREAD $KEY_RANGE 50000
        else
            run_benchmark $impl $FIXED_THREADS $workload $OPS_PER_THREAD $KEY_RANGE 0
        fi
    done
done

echo ""
echo "=== Experiment 3: Contention Study ===" >&2
FIXED_THREADS=16
KEY_RANGES=(1000 10000 100000 1000000)
total=12
current=0

for impl in "${IMPLEMENTATIONS[@]}"; do
    for key_range in "${KEY_RANGES[@]}"; do
        ((current++))
        echo "[$current/$total]" >&2
        run_benchmark $impl $FIXED_THREADS "mixed" $OPS_PER_THREAD $key_range 5000
    done
done

echo ""
echo "=========================================" >&2
echo "Benchmarks Complete!" >&2
echo "=========================================" >&2
echo "Results saved to: ${RESULTS_FILE}" >&2
echo ""
echo "To analyze results, run:" >&2
echo "  python3 scripts/plot_results.py ${RESULTS_FILE}" >&2
echo ""
EOF

chmod +x scripts/run_experiments_fixed.sh

# Run it
./scripts/run_experiments_fixed.sh