#!/bin/bash

# Week 9-10 Hotpath Performance Benchmarking Suite
# Comprehensive performance testing to measure optimization impact

set -e

# Test configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_LOG="${SCRIPT_DIR}/benchmark_hotpath_performance.log"
RESULTS_DIR="${SCRIPT_DIR}/benchmark_results"
LOOP0_SIZE=1G
LOOP1_SIZE=500M

# Performance test parameters
SEQUENTIAL_SIZE=256M
RANDOM_SIZE=128M
BATCH_SIZES=(1 4 8 16 32 64)
BLOCK_SIZES=(512 1K 4K 8K 16K 64K)

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date '+%H:%M:%S')]${NC} $1" | tee -a "$BENCHMARK_LOG"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$BENCHMARK_LOG"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$BENCHMARK_LOG"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$BENCHMARK_LOG"
}

header() {
    echo -e "${CYAN}========================================${NC}" | tee -a "$BENCHMARK_LOG"
    echo -e "${CYAN}$1${NC}" | tee -a "$BENCHMARK_LOG"
    echo -e "${CYAN}========================================${NC}" | tee -a "$BENCHMARK_LOG"
}

# Cleanup function
cleanup() {
    log "Cleaning up benchmark environment..."
    
    # Remove dm-remap device
    dmsetup remove dm-remap-bench 2>/dev/null || true
    
    # Detach loop devices
    losetup -d /dev/loop10 2>/dev/null || true
    losetup -d /dev/loop11 2>/dev/null || true
    
    # Remove temporary files
    rm -f /tmp/main_bench.img /tmp/spare_bench.img
    
    log "Benchmark cleanup completed"
}

# Setup signal handlers
trap cleanup EXIT
trap cleanup INT
trap cleanup TERM

# Initialize benchmark environment
init_benchmark() {
    header "Week 9-10 Hotpath Performance Benchmark Suite"
    
    # Clear previous log
    > "$BENCHMARK_LOG"
    
    # Create results directory
    mkdir -p "$RESULTS_DIR"
    
    # Check if we're running as root
    if [[ $EUID -ne 0 ]]; then
        error "This benchmark must be run as root"
        exit 1
    fi
    
    # Check if dm-remap module is loaded
    if ! lsmod | grep -q dm_remap; then
        log "Loading dm-remap module..."
        cd "${SCRIPT_DIR}/../src"
        if ! insmod dm_remap.ko; then
            error "Failed to load dm-remap module"
            exit 1
        fi
    fi
    
    # Create large test device files for realistic benchmarking
    log "Creating benchmark device files (${LOOP0_SIZE} main, ${LOOP1_SIZE} spare)..."
    dd if=/dev/zero of=/tmp/main_bench.img bs=1M count=${LOOP0_SIZE%G}000 2>/dev/null &
    dd if=/dev/zero of=/tmp/spare_bench.img bs=1M count=${LOOP1_SIZE%M} 2>/dev/null &
    wait
    
    # Setup loop devices with different indices to avoid conflicts
    log "Setting up loop devices..."
    losetup /dev/loop10 /tmp/main_bench.img
    losetup /dev/loop11 /tmp/spare_bench.img
    
    # Create dm-remap device
    log "Creating dm-remap benchmark device..."
    echo "0 $(blockdev --getsz /dev/loop10) remap /dev/loop10 /dev/loop11" | \
        dmsetup create dm-remap-bench
    
    if ! dmsetup info dm-remap-bench >/dev/null 2>&1; then
        error "Failed to create dm-remap benchmark device"
        exit 1
    fi
    
    success "Benchmark environment initialized successfully"
}

# Get hotpath statistics
get_hotpath_stats() {
    local dm_minor=$(dmsetup info dm-remap-bench | grep "Minor:" | awk '{print $2}')
    local stats_file="/sys/block/dm-${dm_minor}/dm-remap/hotpath/hotpath_stats"
    
    if [[ -r "$stats_file" ]]; then
        cat "$stats_file"
    else
        echo "Hotpath statistics not available"
    fi
}

# Reset hotpath statistics
reset_hotpath_stats() {
    local dm_minor=$(dmsetup info dm-remap-bench | grep "Minor:" | awk '{print $2}')
    local reset_file="/sys/block/dm-${dm_minor}/dm-remap/hotpath/hotpath_reset"
    
    if [[ -w "$reset_file" ]]; then
        echo "1" > "$reset_file" 2>/dev/null
        log "Hotpath statistics reset"
    else
        warning "Cannot reset hotpath statistics"
    fi
}

# Extract performance metrics from stats
extract_metrics() {
    local stats="$1"
    local total_ios=$(echo "$stats" | grep "Total I/Os:" | awk '{print $3}')
    local fastpath_ios=$(echo "$stats" | grep "Fast-path I/Os:" | awk '{print $3}')
    local efficiency=$(echo "$stats" | grep "Fast-path efficiency:" | awk '{print $3}' | tr -d '%')
    local cache_hits=$(echo "$stats" | grep "Cache line hits:" | awk '{print $4}')
    local batch_processed=$(echo "$stats" | grep "Batch processed:" | awk '{print $3}')
    
    echo "total_ios=${total_ios:-0},fastpath_ios=${fastpath_ios:-0},efficiency=${efficiency:-0},cache_hits=${cache_hits:-0},batch_processed=${batch_processed:-0}"
}

# Sequential read benchmark
benchmark_sequential_read() {
    local block_size="$1"
    local test_name="seq_read_${block_size}"
    
    log "Sequential Read Benchmark: ${block_size} block size"
    
    # Reset statistics
    reset_hotpath_stats
    
    # Get initial stats
    local initial_stats=$(get_hotpath_stats)
    
    # Run sequential read test
    local start_time=$(date +%s.%N)
    dd if=/dev/mapper/dm-remap-bench of=/dev/null bs="$block_size" count=$((128*1024*1024 / ${block_size%K} / 1024)) 2>/dev/null
    local end_time=$(date +%s.%N)
    
    # Calculate duration
    local duration=$(echo "$end_time - $start_time" | bc -l)
    local throughput=$(echo "scale=2; $SEQUENTIAL_SIZE / $duration / 1024 / 1024" | bc -l)
    
    # Get final stats
    local final_stats=$(get_hotpath_stats)
    local metrics=$(extract_metrics "$final_stats")
    
    # Save results
    echo "${test_name},${block_size},${duration},${throughput},${metrics}" >> "${RESULTS_DIR}/sequential_read.csv"
    
    log "Sequential Read ${block_size}: ${throughput} MB/s in ${duration}s"
}

# Sequential write benchmark
benchmark_sequential_write() {
    local block_size="$1"
    local test_name="seq_write_${block_size}"
    
    log "Sequential Write Benchmark: ${block_size} block size"
    
    # Reset statistics
    reset_hotpath_stats
    
    # Get initial stats
    local initial_stats=$(get_hotpath_stats)
    
    # Run sequential write test
    local start_time=$(date +%s.%N)
    dd if=/dev/zero of=/dev/mapper/dm-remap-bench bs="$block_size" count=$((64*1024*1024 / ${block_size%K} / 1024)) 2>/dev/null
    local end_time=$(date +%s.%N)
    
    # Ensure data is written
    sync
    
    # Calculate duration
    local duration=$(echo "$end_time - $start_time" | bc -l)
    local throughput=$(echo "scale=2; 64 / $duration" | bc -l)
    
    # Get final stats
    local final_stats=$(get_hotpath_stats)
    local metrics=$(extract_metrics "$final_stats")
    
    # Save results
    echo "${test_name},${block_size},${duration},${throughput},${metrics}" >> "${RESULTS_DIR}/sequential_write.csv"
    
    log "Sequential Write ${block_size}: ${throughput} MB/s in ${duration}s"
}

# Random I/O benchmark
benchmark_random_io() {
    local block_size="$1"
    local io_type="$2"  # read or write
    local test_name="random_${io_type}_${block_size}"
    
    log "Random ${io_type} Benchmark: ${block_size} block size"
    
    # Reset statistics
    reset_hotpath_stats
    
    # Prepare fio job file
    local fio_job="/tmp/random_${io_type}_${block_size}.fio"
    cat > "$fio_job" << EOF
[random_${io_type}]
filename=/dev/mapper/dm-remap-bench
direct=1
rw=rand${io_type}
bs=${block_size}
size=128M
numjobs=1
runtime=30
time_based=1
ioengine=libaio
iodepth=32
group_reporting=1
EOF

    # Run fio benchmark if available
    if command -v fio >/dev/null 2>&1; then
        local start_time=$(date +%s.%N)
        local fio_output=$(fio "$fio_job" 2>/dev/null)
        local end_time=$(date +%s.%N)
        
        # Parse fio output for IOPS and bandwidth
        local iops=$(echo "$fio_output" | grep "IOPS=" | head -1 | sed 's/.*IOPS=\([0-9.]*\).*/\1/')
        local bw=$(echo "$fio_output" | grep "BW=" | head -1 | sed 's/.*BW=\([0-9.]*\).*/\1/')
        
        # Get final stats
        local final_stats=$(get_hotpath_stats)
        local metrics=$(extract_metrics "$final_stats")
        
        # Save results
        echo "${test_name},${block_size},30,${bw:-0},${iops:-0},${metrics}" >> "${RESULTS_DIR}/random_io.csv"
        
        log "Random ${io_type} ${block_size}: ${iops} IOPS, ${bw} KB/s"
    else
        warning "fio not available, skipping random I/O benchmark"
    fi
    
    rm -f "$fio_job"
}

# Batch processing efficiency test
benchmark_batch_efficiency() {
    local batch_size="$1"
    
    log "Batch Processing Efficiency Test: batch size ${batch_size}"
    
    # Reset statistics
    reset_hotpath_stats
    
    # Simulate batch workload with many small I/Os
    local start_time=$(date +%s.%N)
    for i in $(seq 1 $((batch_size * 50))); do
        dd if=/dev/zero of=/dev/mapper/dm-remap-bench bs=512 count=1 seek=$((i * 8)) 2>/dev/null &
        
        # Control batch size by waiting periodically
        if [[ $((i % batch_size)) -eq 0 ]]; then
            wait
        fi
    done
    wait
    local end_time=$(date +%s.%N)
    
    # Calculate metrics
    local duration=$(echo "$end_time - $start_time" | bc -l)
    local ops_per_sec=$(echo "scale=2; $((batch_size * 50)) / $duration" | bc -l)
    
    # Get final stats
    local final_stats=$(get_hotpath_stats)
    local metrics=$(extract_metrics "$final_stats")
    local batch_processed=$(echo "$metrics" | sed 's/.*batch_processed=\([0-9]*\).*/\1/')
    local batch_efficiency=$(echo "scale=1; $batch_processed * 100 / $((batch_size * 50))" | bc -l)
    
    # Save results
    echo "batch_${batch_size},${batch_size},${duration},${ops_per_sec},${batch_efficiency},${metrics}" >> "${RESULTS_DIR}/batch_efficiency.csv"
    
    log "Batch size ${batch_size}: ${ops_per_sec} ops/s, ${batch_efficiency}% batch efficiency"
}

# Cache alignment effectiveness test
benchmark_cache_alignment() {
    log "Cache Alignment Effectiveness Test"
    
    # Reset statistics
    reset_hotpath_stats
    
    # Test various access patterns to stress cache alignment
    local patterns=("sequential" "stride_64" "stride_128" "random")
    
    for pattern in "${patterns[@]}"; do
        log "Testing cache alignment with ${pattern} access pattern"
        
        case "$pattern" in
            "sequential")
                dd if=/dev/mapper/dm-remap-bench of=/dev/null bs=64 count=100000 2>/dev/null
                ;;
            "stride_64")
                for i in $(seq 0 64 6400); do
                    dd if=/dev/mapper/dm-remap-bench of=/dev/null bs=64 count=1 skip=$i 2>/dev/null
                done
                ;;
            "stride_128")
                for i in $(seq 0 128 12800); do
                    dd if=/dev/mapper/dm-remap-bench of=/dev/null bs=64 count=1 skip=$i 2>/dev/null
                done
                ;;
            "random")
                for i in $(seq 1 1000); do
                    local offset=$((RANDOM % 10000))
                    dd if=/dev/mapper/dm-remap-bench of=/dev/null bs=64 count=1 skip=$offset 2>/dev/null
                done
                ;;
        esac
        
        # Get stats for this pattern
        local pattern_stats=$(get_hotpath_stats)
        local cache_hits=$(echo "$pattern_stats" | grep "Cache line hits:" | awk '{print $4}')
        local total_ios=$(echo "$pattern_stats" | grep "Total I/Os:" | awk '{print $3}')
        local cache_hit_rate=$(echo "scale=1; $cache_hits * 100 / $total_ios" | bc -l 2>/dev/null || echo "0")
        
        log "Cache alignment ${pattern}: ${cache_hit_rate}% hit rate"
        echo "${pattern},${cache_hits:-0},${total_ios:-0},${cache_hit_rate}" >> "${RESULTS_DIR}/cache_alignment.csv"
        
        # Reset for next pattern
        reset_hotpath_stats
    done
}

# Generate performance report
generate_report() {
    local report_file="${RESULTS_DIR}/performance_report.txt"
    
    header "Performance Benchmark Report"
    
    {
        echo "Week 9-10 Hotpath Optimization Performance Report"
        echo "Generated: $(date)"
        echo "=============================================="
        echo
        
        echo "SEQUENTIAL READ PERFORMANCE:"
        if [[ -f "${RESULTS_DIR}/sequential_read.csv" ]]; then
            echo "Block Size | Duration(s) | Throughput(MB/s) | Fast-path Efficiency(%)"
            echo "-----------|-------------|------------------|------------------------"
            while IFS=',' read -r test block duration throughput metrics; do
                local efficiency=$(echo "$metrics" | sed 's/.*efficiency=\([0-9]*\).*/\1/')
                printf "%-10s | %-11s | %-16s | %-22s\n" "$block" "$duration" "$throughput" "${efficiency}%"
            done < "${RESULTS_DIR}/sequential_read.csv"
        else
            echo "No sequential read data available"
        fi
        echo
        
        echo "SEQUENTIAL WRITE PERFORMANCE:"
        if [[ -f "${RESULTS_DIR}/sequential_write.csv" ]]; then
            echo "Block Size | Duration(s) | Throughput(MB/s) | Fast-path Efficiency(%)"
            echo "-----------|-------------|------------------|------------------------"
            while IFS=',' read -r test block duration throughput metrics; do
                local efficiency=$(echo "$metrics" | sed 's/.*efficiency=\([0-9]*\).*/\1/')
                printf "%-10s | %-11s | %-16s | %-22s\n" "$block" "$duration" "$throughput" "${efficiency}%"
            done < "${RESULTS_DIR}/sequential_write.csv"
        else
            echo "No sequential write data available"
        fi
        echo
        
        echo "BATCH PROCESSING EFFICIENCY:"
        if [[ -f "${RESULTS_DIR}/batch_efficiency.csv" ]]; then
            echo "Batch Size | Operations/sec | Batch Efficiency(%)"
            echo "-----------|----------------|--------------------"
            while IFS=',' read -r test batch_size duration ops_per_sec batch_eff metrics; do
                printf "%-10s | %-14s | %-18s\n" "$batch_size" "$ops_per_sec" "${batch_eff}%"
            done < "${RESULTS_DIR}/batch_efficiency.csv"
        else
            echo "No batch efficiency data available"
        fi
        echo
        
        echo "CACHE ALIGNMENT EFFECTIVENESS:"
        if [[ -f "${RESULTS_DIR}/cache_alignment.csv" ]]; then
            echo "Access Pattern | Cache Hit Rate(%)"
            echo "---------------|------------------"
            while IFS=',' read -r pattern cache_hits total_ios hit_rate; do
                printf "%-14s | %-16s\n" "$pattern" "${hit_rate}%"
            done < "${RESULTS_DIR}/cache_alignment.csv"
        else
            echo "No cache alignment data available"
        fi
        
    } | tee "$report_file"
    
    success "Performance report generated: $report_file"
}

# Main benchmark execution
run_all_benchmarks() {
    init_benchmark
    
    # Create CSV headers
    echo "test_name,block_size,duration,throughput,total_ios,fastpath_ios,efficiency,cache_hits,batch_processed" > "${RESULTS_DIR}/sequential_read.csv"
    echo "test_name,block_size,duration,throughput,total_ios,fastpath_ios,efficiency,cache_hits,batch_processed" > "${RESULTS_DIR}/sequential_write.csv"
    echo "test_name,block_size,runtime,bandwidth,iops,total_ios,fastpath_ios,efficiency,cache_hits,batch_processed" > "${RESULTS_DIR}/random_io.csv"
    echo "test_name,batch_size,duration,ops_per_sec,batch_efficiency,total_ios,fastpath_ios,efficiency,cache_hits,batch_processed" > "${RESULTS_DIR}/batch_efficiency.csv"
    echo "access_pattern,cache_hits,total_ios,hit_rate" > "${RESULTS_DIR}/cache_alignment.csv"
    
    # Sequential read benchmarks
    header "Sequential Read Performance Tests"
    for block_size in "${BLOCK_SIZES[@]}"; do
        benchmark_sequential_read "$block_size"
    done
    
    # Sequential write benchmarks  
    header "Sequential Write Performance Tests"
    for block_size in "${BLOCK_SIZES[@]}"; do
        benchmark_sequential_write "$block_size"
    done
    
    # Random I/O benchmarks (if fio is available)
    if command -v fio >/dev/null 2>&1; then
        header "Random I/O Performance Tests"
        for block_size in 4K 8K 16K; do
            benchmark_random_io "$block_size" "read"
            benchmark_random_io "$block_size" "write"
        done
    else
        warning "fio not installed, skipping random I/O benchmarks"
        log "Install fio for comprehensive random I/O testing: apt-get install fio"
    fi
    
    # Batch efficiency tests
    header "Batch Processing Efficiency Tests"
    for batch_size in "${BATCH_SIZES[@]}"; do
        benchmark_batch_efficiency "$batch_size"
    done
    
    # Cache alignment tests
    header "Cache Alignment Effectiveness Tests"
    benchmark_cache_alignment
    
    # Generate final report
    generate_report
    
    success "All benchmarks completed successfully!"
    log "Results saved in: ${RESULTS_DIR}/"
    log "Performance report: ${RESULTS_DIR}/performance_report.txt"
}

# Execute benchmarks
main() {
    run_all_benchmarks
}

# Check for required tools
check_dependencies() {
    local missing_tools=()
    
    if ! command -v bc >/dev/null 2>&1; then
        missing_tools+=("bc")
    fi
    
    if ! command -v dmsetup >/dev/null 2>&1; then
        missing_tools+=("dmsetup")
    fi
    
    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        error "Missing required tools: ${missing_tools[*]}"
        log "Install missing tools: apt-get install ${missing_tools[*]}"
        exit 1
    fi
}

# Run dependency check and main function
check_dependencies
main "$@"