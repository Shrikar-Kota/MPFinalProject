#!/bin/bash
OUTPUT_DIR="results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="${OUTPUT_DIR}/results_${TIMESTAMP}.csv"
TEMP_FILE="${OUTPUT_DIR}/temp_results.txt"
mkdir -p ${OUTPUT_DIR}

IMPLEMENTATIONS=("coarse" "fine" "lockfree")
THREAD_COUNTS=(1 2 4 8 16 32)
WORKLOADS=("insert" "readonly" "mixed" "delete")
OPS_PER_THREAD=1000000
KEY_RANGE=100000

echo "========================================="
echo "Comprehensive Benchmark Suite"
echo "========================================="
echo "NO TIMEOUT - Will run until complete"
echo "Started at: $(date)"
echo ""

echo "impl,threads,workload,ops,key_range,time,throughput,successful,failed" > ${RESULTS_FILE}

run_benchmark() {
    local impl=$1
    local threads=$2
    local workload=$3
    local ops=$4
    local key_range=$5
    local initial_size=$6
    
    local start_time=$(date +%s)
    echo "[$(date +%H:%M:%S)] Running: impl=$impl threads=$threads workload=$workload"
    
    # NO TIMEOUT - let it run
    ./bin/benchmark \
        --impl $impl \
        --threads $threads \
        --ops $ops \
        --key-range $key_range \
        --workload $workload \
        --initial-size $initial_size \
        --warmup 10000 \
        --csv > ${TEMP_FILE} 2>&1
    
    local exit_code=$?
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    if [ $exit_code -eq 0 ]; then
        grep -v "^impl,threads,workload" ${TEMP_FILE} | grep -v "^$" >> ${RESULTS_FILE}
        echo "  ✓ Done in ${duration}s"
    else
        echo "  ✗ FAILED after ${duration}s"
    fi
}

echo ""
echo "=== Experiment 1: Scalability (18 runs) ==="
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
        ((current++))
        echo "Progress: [$current/18]"
        run_benchmark $impl $threads "mixed" $OPS_PER_THREAD $KEY_RANGE 5000
    done
done

echo ""
echo "=== Experiment 2: Workload Comparison (12 runs) ==="
FIXED_THREADS=8
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for workload in "${WORKLOADS[@]}"; do
        ((current++))
        echo "Progress: [$current/12]"
        if [ "$workload" == "delete" ] || [ "$workload" == "readonly" ]; then
            run_benchmark $impl $FIXED_THREADS $workload $OPS_PER_THREAD $KEY_RANGE 50000
        else
            run_benchmark $impl $FIXED_THREADS $workload $OPS_PER_THREAD $KEY_RANGE 0
        fi
    done
done

echo ""
echo "=== Experiment 3: Contention Study (12 runs) ==="
FIXED_THREADS=16
KEY_RANGES=(1000 10000 100000 1000000)
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for key_range in "${KEY_RANGES[@]}"; do
        ((current++))
        echo "Progress: [$current/12]"
        run_benchmark $impl $FIXED_THREADS "mixed" $OPS_PER_THREAD $key_range 5000
    done
done

rm -f ${TEMP_FILE}

echo ""
echo "========================================="
echo "Benchmarks Complete!"
echo "========================================="
echo "Completed at: $(date)"
echo "Results: ${RESULTS_FILE}"
echo "Total results: $(( $(wc -l < ${RESULTS_FILE}) - 1 ))"
echo ""
echo "To analyze results:"
echo "  python3 scripts/plot_results.py ${RESULTS_FILE}"
echo ""