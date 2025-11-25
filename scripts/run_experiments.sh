cd ~/MPFinalProject

cat > scripts/run_experiments_comprehensive.sh << 'EOF'
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
echo "Comprehensive Skip List Benchmark Suite"
echo "========================================="
echo "Operations per thread: ${OPS_PER_THREAD}"
echo "Total configurations: 42"
echo "Estimated time: 2-3 hours"
echo "Started at: $(date)"
echo ""
echo "Results will be saved to: ${RESULTS_FILE}"
echo ""

# Write header once
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
    
    # 10 minute timeout per benchmark
    timeout 600 ./bin/benchmark \
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
    
    if [ $exit_code -eq 124 ]; then
        echo "  ⏱ TIMEOUT after 10 minutes - skipping"
    elif [ $exit_code -ne 0 ]; then
        echo "  ✗ FAILED with exit code $exit_code"
    else
        # Filter out header lines and empty lines
        grep -v "^impl,threads,workload" ${TEMP_FILE} | grep -v "^$" >> ${RESULTS_FILE}
        echo "  ✓ Done in ${duration}s"
    fi
}

echo ""
echo "=== Experiment 1: Scalability Test (18 runs) ==="
total=18
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
        ((current++))
        echo ""
        echo "Progress: [$current/$total]"
        run_benchmark $impl $threads "mixed" $OPS_PER_THREAD $KEY_RANGE 5000
    done
done

echo ""
echo "=== Experiment 2: Workload Comparison (12 runs) ==="
FIXED_THREADS=8
total=12
current=0
for impl in "${IMPLEMENTATIONS[@]}"; do
    for workload in "${WORKLOADS[@]}"; do
        ((current++))
        echo ""
        echo "Progress: [$current/$total]"
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
total=12
current=0

for impl in "${IMPLEMENTATIONS[@]}"; do
    for key_range in "${KEY_RANGES[@]}"; do
        ((current++))
        echo ""
        echo "Progress: [$current/$total]"
        run_benchmark $impl $FIXED_THREADS "mixed" $OPS_PER_THREAD $key_range 5000
    done
done

# Clean up temp file
rm -f ${TEMP_FILE}

echo ""
echo "========================================="
echo "Benchmarks Complete!"
echo "========================================="
echo "Completed at: $(date)"
echo "Results saved to: ${RESULTS_FILE}"
echo ""
echo "Total results collected: $(( $(wc -l < ${RESULTS_FILE}) - 1 ))"
echo ""
echo "Preview of results:"
head -15 ${RESULTS_FILE}
echo "..."
echo ""
echo "To generate plots and analysis:"
echo "  python3 scripts/plot_results.py ${RESULTS_FILE}"
echo ""
EOF

chmod +x scripts/run_experiments_comprehensive.sh

# Run it with logging
echo "Starting comprehensive benchmark suite..."
echo "This will take 2-3 hours. You can:"
echo "  - Leave it running"
echo "  - Monitor in another terminal: tail -f results/results_*.csv"
echo "  - Run in background: nohup ./scripts/run_experiments_comprehensive.sh > benchmark.log 2>&1 &"
echo ""
read -p "Press Enter to start, or Ctrl+C to cancel..."

time ./scripts/run_experiments_comprehensive.sh 2>&1 | tee benchmark_run.log