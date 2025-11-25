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
    
    echo "Running: impl=$impl threads=$threads workload=$workload"
    
    ./bin/benchmark \
        --impl $impl \
        --threads $threads \
        --ops $ops \
        --key-range $key_range \
        --workload $workload \
        --initial-size $initial_size \
        --warmup 10000 \
        --csv >> ${RESULTS_FILE}
}

echo ""
echo "=== Experiment 1: Scalability Test ==="
for impl in "${IMPLEMENTATIONS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
        run_benchmark $impl $threads "mixed" $OPS_PER_THREAD $KEY_RANGE 5000
    done
done

echo ""
echo "=== Experiment 2: Workload Comparison ==="
FIXED_THREADS=8
for impl in "${IMPLEMENTATIONS[@]}"; do
    for workload in "${WORKLOADS[@]}"; do
        if [ "$workload" == "delete" ] || [ "$workload" == "readonly" ]; then
            run_benchmark $impl $FIXED_THREADS $workload $OPS_PER_THREAD $KEY_RANGE 50000
        else
            run_benchmark $impl $FIXED_THREADS $workload $OPS_PER_THREAD $KEY_RANGE 0
        fi
    done
done

echo ""
echo "=== Experiment 3: Contention Study ==="
FIXED_THREADS=16
KEY_RANGES=(1000 10000 100000 1000000)

for impl in "${IMPLEMENTATIONS[@]}"; do
    for key_range in "${KEY_RANGES[@]}"; do
        run_benchmark $impl $FIXED_THREADS "mixed" $OPS_PER_THREAD $key_range 5000
    done
done

echo ""
echo "========================================="
echo "Benchmarks Complete!"
echo "========================================="
echo "Results saved to: ${RESULTS_FILE}"
echo ""
echo "To analyze results, run:"
echo "  python3 scripts/plot_results.py ${RESULTS_FILE}"
echo ""
