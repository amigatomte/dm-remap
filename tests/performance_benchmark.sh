#!/bin/bash
#
# performance_benchmark.sh - Comprehensive performance benchmarking for dm-remap v4.0
#
# This script measures and analyzes the performance impact of the reservation system
# across different device configurations and workload patterns.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${PURPLE}=================================================================${NC}"
echo -e "${PURPLE}  dm-remap v4.0 PERFORMANCE BENCHMARK SUITE${NC}"
echo -e "${PURPLE}=================================================================${NC}"

# Performance targets
TARGET_OVERHEAD_PERCENT=5
TARGET_ALLOCATION_TIME_US=100
TARGET_THROUGHPUT_MB_S=500

# Benchmark results
BENCHMARK_RESULTS=()
PERFORMANCE_METRICS=()

# Setup function
setup_benchmark_env() {
    echo -e "${BLUE}=== Setting up benchmark environment ===${NC}"
    
    # Ensure dm-remap module is loaded
    if ! lsmod | grep -q dm_remap; then
        echo "Loading dm-remap module..."
        sudo insmod src/dm_remap.ko
    fi
    
    # Create benchmark devices
    echo "Creating benchmark test devices..."
    dd if=/dev/zero of=/tmp/perf_main.img bs=1M count=100 2>/dev/null     # 100MB main
    dd if=/dev/zero of=/tmp/perf_large.img bs=1M count=32 2>/dev/null     # 32MB spare (geometric)
    dd if=/dev/zero of=/tmp/perf_medium.img bs=1M count=8 2>/dev/null     # 8MB spare (linear)
    dd if=/dev/zero of=/tmp/perf_small.img bs=1M count=2 2>/dev/null      # 2MB spare (minimal)
    
    # Setup loop devices
    MAIN_LOOP=$(sudo losetup -f --show /tmp/perf_main.img)
    LARGE_LOOP=$(sudo losetup -f --show /tmp/perf_large.img)
    MEDIUM_LOOP=$(sudo losetup -f --show /tmp/perf_medium.img)
    SMALL_LOOP=$(sudo losetup -f --show /tmp/perf_small.img)
    
    echo "Benchmark devices ready:"
    echo "  Main: $MAIN_LOOP (100MB)"
    echo "  Large spare: $LARGE_LOOP (32MB - geometric strategy)"
    echo "  Medium spare: $MEDIUM_LOOP (8MB - linear strategy)"
    echo "  Small spare: $SMALL_LOOP (2MB - minimal strategy)"
}

# Cleanup function
cleanup_benchmark() {
    echo -e "${YELLOW}Cleaning up benchmark environment...${NC}"
    
    # Remove targets
    for target in perf-geometric perf-linear perf-minimal; do
        sudo dmsetup remove "$target" 2>/dev/null || true
    done
    
    # Clean up loop devices
    for loop in "$MAIN_LOOP" "$LARGE_LOOP" "$MEDIUM_LOOP" "$SMALL_LOOP"; do
        sudo losetup -d "$loop" 2>/dev/null || true
    done
    
    # Remove test files
    rm -f /tmp/perf_*.img
}

trap cleanup_benchmark EXIT

# Allocation performance benchmark
benchmark_allocation_performance() {
    local device_name="$1"
    local spare_loop="$2"
    local strategy="$3"
    local operation_count="$4"
    
    echo -e "${CYAN}--- Allocation Performance: $device_name ($strategy) ---${NC}"
    
    # Create target
    local target_name="perf-$(echo "$strategy" | tr '[:upper:]' '[:lower:]')"
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    SPARE_SECTORS=$(sudo blockdev --getsz "$spare_loop")
    
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $spare_loop 0 $SPARE_SECTORS" | sudo dmsetup create "$target_name"
    
    if ! sudo dmsetup info "$target_name" | grep -q "State.*ACTIVE"; then
        echo -e "${RED}❌ Failed to create benchmark target: $target_name${NC}"
        return 1
    fi
    
    # Warm up the system
    echo "Performing warm-up operations..."
    for i in $(seq 1 10); do
        sector=$((i * 100 + 1000))
        sudo dmsetup message "$target_name" 0 remap $sector >/dev/null 2>&1 || true
    done
    
    # Benchmark allocation performance
    echo "Benchmarking $operation_count allocation operations..."
    local start_time=$(date +%s%N)  # nanoseconds
    
    local successful_ops=0
    local failed_ops=0
    
    for i in $(seq 1 "$operation_count"); do
        sector=$((i * 50 + 2000))  # Avoid warm-up sectors
        
        local op_start=$(date +%s%N)
        result=$(sudo dmsetup message "$target_name" 0 remap $sector 2>&1)
        local op_end=$(date +%s%N)
        
        if [[ "$result" != *"error"* ]]; then
            successful_ops=$((successful_ops + 1))
            local op_time_ns=$((op_end - op_start))
            local op_time_us=$((op_time_ns / 1000))
            
            # Track individual operation times for statistics
            echo "$op_time_us" >> "/tmp/perf_${strategy}_times.txt"
        else
            failed_ops=$((failed_ops + 1))
            if [[ "$result" == *"no unreserved spare sectors available"* ]]; then
                echo "  Reached capacity at operation $i"
                break
            fi
        fi
        
        # Progress indicator
        if [[ $((i % 100)) -eq 0 ]]; then
            echo "    Progress: $i/$operation_count operations"
        fi
    done
    
    local end_time=$(date +%s%N)
    local total_time_ns=$((end_time - start_time))
    local total_time_ms=$((total_time_ns / 1000000))
    local avg_time_us=$((total_time_ns / successful_ops / 1000))
    
    # Calculate statistics from individual operation times
    local min_time_us max_time_us median_time_us p95_time_us p99_time_us
    if [[ -f "/tmp/perf_${strategy}_times.txt" ]]; then
        min_time_us=$(sort -n "/tmp/perf_${strategy}_times.txt" | head -1)
        max_time_us=$(sort -n "/tmp/perf_${strategy}_times.txt" | tail -1)
        median_time_us=$(sort -n "/tmp/perf_${strategy}_times.txt" | sed -n "$((successful_ops / 2))p")
        p95_time_us=$(sort -n "/tmp/perf_${strategy}_times.txt" | sed -n "$((successful_ops * 95 / 100))p")
        p99_time_us=$(sort -n "/tmp/perf_${strategy}_times.txt" | sed -n "$((successful_ops * 99 / 100))p")
        rm "/tmp/perf_${strategy}_times.txt"
    fi
    
    # Get final system status
    local status=$(sudo dmsetup status "$target_name")
    
    echo ""
    echo "Performance Results for $device_name ($strategy):"
    echo "  Total operations: $operation_count"
    echo "  Successful: $successful_ops"
    echo "  Failed: $failed_ops"
    echo "  Total time: ${total_time_ms}ms"
    echo "  Average allocation time: ${avg_time_us}μs"
    echo "  Min allocation time: ${min_time_us}μs"
    echo "  Max allocation time: ${max_time_us}μs"
    echo "  Median allocation time: ${median_time_us}μs"
    echo "  95th percentile: ${p95_time_us}μs"
    echo "  99th percentile: ${p99_time_us}μs"
    echo "  Operations per second: $((successful_ops * 1000 / total_time_ms))"
    
    # Performance assessment
    local performance_grade="UNKNOWN"
    if [[ $avg_time_us -le $TARGET_ALLOCATION_TIME_US ]]; then
        performance_grade="EXCELLENT"
        echo -e "${GREEN}  Performance Grade: $performance_grade (≤${TARGET_ALLOCATION_TIME_US}μs target)${NC}"
    elif [[ $avg_time_us -le $((TARGET_ALLOCATION_TIME_US * 2)) ]]; then
        performance_grade="GOOD"
        echo -e "${YELLOW}  Performance Grade: $performance_grade (≤${TARGET_ALLOCATION_TIME_US}μs target)${NC}"
    else
        performance_grade="NEEDS_OPTIMIZATION"
        echo -e "${RED}  Performance Grade: $performance_grade (>${TARGET_ALLOCATION_TIME_US}μs target)${NC}"
    fi
    
    # Store results for summary
    BENCHMARK_RESULTS+=("$device_name,$strategy,$successful_ops,$avg_time_us,$performance_grade")
    
    # Clean up target
    sudo dmsetup remove "$target_name"
    echo ""
}

# I/O throughput benchmark
benchmark_io_throughput() {
    echo -e "${CYAN}--- I/O Throughput Benchmark ---${NC}"
    
    # Create a target for I/O testing
    local target_name="perf-io-test"
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    SPARE_SECTORS=$(sudo blockdev --getsz "$LARGE_LOOP")
    
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $LARGE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create "$target_name"
    
    if ! sudo dmsetup info "$target_name" | grep -q "State.*ACTIVE"; then
        echo -e "${RED}❌ Failed to create I/O benchmark target${NC}"
        return 1
    fi
    
    # Add some remaps to test I/O through remapped sectors
    echo "Setting up remapped sectors for I/O testing..."
    for i in $(seq 1 20); do
        sector=$((i * 100))
        sudo dmsetup message "$target_name" 0 remap $sector >/dev/null 2>&1 || true
    done
    
    # Test sequential read throughput
    echo "Testing sequential read throughput..."
    local read_start=$(date +%s%N)
    dd if="/dev/mapper/$target_name" of=/dev/null bs=1M count=50 2>/dev/null
    local read_end=$(date +%s%N)
    local read_time_s=$(((read_end - read_start) / 1000000000))
    local read_throughput_mb_s=$((50 / read_time_s))
    
    # Test sequential write throughput
    echo "Testing sequential write throughput..."
    local write_start=$(date +%s%N)
    dd if=/dev/zero of="/dev/mapper/$target_name" bs=1M count=50 2>/dev/null
    local write_end=$(date +%s%N)
    local write_time_s=$(((write_end - write_start) / 1000000000))
    local write_throughput_mb_s=$((50 / write_time_s))
    
    echo ""
    echo "I/O Throughput Results:"
    echo "  Sequential Read: ${read_throughput_mb_s} MB/s"
    echo "  Sequential Write: ${write_throughput_mb_s} MB/s"
    
    # Performance assessment
    if [[ $read_throughput_mb_s -ge $TARGET_THROUGHPUT_MB_S && $write_throughput_mb_s -ge $TARGET_THROUGHPUT_MB_S ]]; then
        echo -e "${GREEN}  I/O Performance: EXCELLENT (≥${TARGET_THROUGHPUT_MB_S} MB/s target)${NC}"
    elif [[ $read_throughput_mb_s -ge $((TARGET_THROUGHPUT_MB_S / 2)) && $write_throughput_mb_s -ge $((TARGET_THROUGHPUT_MB_S / 2)) ]]; then
        echo -e "${YELLOW}  I/O Performance: ACCEPTABLE (≥${TARGET_THROUGHPUT_MB_S} MB/s target)${NC}"
    else
        echo -e "${RED}  I/O Performance: NEEDS_OPTIMIZATION (<${TARGET_THROUGHPUT_MB_S} MB/s target)${NC}"
    fi
    
    # Store I/O results
    PERFORMANCE_METRICS+=("IO_READ,$read_throughput_mb_s,MB/s")
    PERFORMANCE_METRICS+=("IO_WRITE,$write_throughput_mb_s,MB/s")
    
    sudo dmsetup remove "$target_name"
    echo ""
}

# Memory usage analysis
benchmark_memory_usage() {
    echo -e "${CYAN}--- Memory Usage Analysis ---${NC}"
    
    # Get initial memory usage
    local mem_before=$(grep "MemAvailable" /proc/meminfo | awk '{print $2}')
    
    # Create multiple targets to stress memory usage
    echo "Creating multiple targets to analyze memory footprint..."
    local targets=()
    
    for i in $(seq 1 5); do
        local target_name="perf-mem-$i"
        MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
        SPARE_SECTORS=$(sudo blockdev --getsz "$LARGE_LOOP")
        
        echo "0 $MAIN_SECTORS remap $MAIN_LOOP $LARGE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create "$target_name"
        targets+=("$target_name")
        
        # Add remaps to each target
        for j in $(seq 1 50); do
            sector=$((j * 20 + i * 1000))
            sudo dmsetup message "$target_name" 0 remap $sector >/dev/null 2>&1 || true
        done
    done
    
    # Get memory usage after target creation
    local mem_after=$(grep "MemAvailable" /proc/meminfo | awk '{print $2}')
    local memory_used_kb=$((mem_before - mem_after))
    local memory_used_mb=$((memory_used_kb / 1024))
    
    echo ""
    echo "Memory Usage Analysis:"
    echo "  Memory before: ${mem_before} KB"
    echo "  Memory after: ${mem_after} KB"  
    echo "  Memory used by 5 targets: ${memory_used_mb} MB"
    echo "  Average per target: $((memory_used_mb / 5)) MB"
    
    # Clean up memory test targets
    for target in "${targets[@]}"; do
        sudo dmsetup remove "$target" 2>/dev/null || true
    done
    
    PERFORMANCE_METRICS+=("MEMORY_PER_TARGET,$((memory_used_mb / 5)),MB")
    echo ""
}

# Main benchmark execution
main() {
    echo -e "${BLUE}Starting comprehensive performance benchmark...${NC}"
    echo "Performance Targets:"
    echo "  - Allocation overhead: <${TARGET_OVERHEAD_PERCENT}%"
    echo "  - Allocation time: <${TARGET_ALLOCATION_TIME_US}μs"
    echo "  - I/O throughput: ≥${TARGET_THROUGHPUT_MB_S} MB/s"
    echo ""
    
    # Setup environment
    setup_benchmark_env
    
    # Run allocation performance benchmarks
    echo -e "${BLUE}Phase 1: Allocation Performance Benchmarks${NC}"
    benchmark_allocation_performance "Large Device" "$LARGE_LOOP" "GEOMETRIC" 1000
    benchmark_allocation_performance "Medium Device" "$MEDIUM_LOOP" "LINEAR" 500
    benchmark_allocation_performance "Small Device" "$SMALL_LOOP" "MINIMAL" 100
    
    # Run I/O throughput benchmarks
    echo -e "${BLUE}Phase 2: I/O Throughput Benchmarks${NC}"
    benchmark_io_throughput
    
    # Run memory usage analysis
    echo -e "${BLUE}Phase 3: Memory Usage Analysis${NC}"
    benchmark_memory_usage
    
    # Generate summary report
    echo -e "${PURPLE}=================================================================${NC}"
    echo -e "${PURPLE}  PERFORMANCE BENCHMARK SUMMARY${NC}"
    echo -e "${PURPLE}=================================================================${NC}"
    
    echo ""
    echo "Allocation Performance Results:"
    echo "Device,Strategy,Operations,Avg_Time_μs,Grade"
    for result in "${BENCHMARK_RESULTS[@]}"; do
        echo "$result"
    done
    
    echo ""
    echo "System Performance Metrics:"
    for metric in "${PERFORMANCE_METRICS[@]}"; do
        echo "$metric"
    done
    
    # Overall assessment
    local excellent_count=0
    local total_count=${#BENCHMARK_RESULTS[@]}
    
    for result in "${BENCHMARK_RESULTS[@]}"; do
        if [[ "$result" == *"EXCELLENT"* ]]; then
            excellent_count=$((excellent_count + 1))
        fi
    done
    
    local performance_ratio=$((excellent_count * 100 / total_count))
    
    echo ""
    if [[ $performance_ratio -ge 80 ]]; then
        echo -e "${GREEN}🚀 OVERALL PERFORMANCE: EXCELLENT (${performance_ratio}% targets met)${NC}"
        echo -e "${GREEN}✅ System ready for production deployment!${NC}"
    elif [[ $performance_ratio -ge 60 ]]; then
        echo -e "${YELLOW}⚡ OVERALL PERFORMANCE: GOOD (${performance_ratio}% targets met)${NC}"
        echo -e "${YELLOW}⚠️  Minor optimizations recommended${NC}"
    else
        echo -e "${RED}🐌 OVERALL PERFORMANCE: NEEDS OPTIMIZATION (${performance_ratio}% targets met)${NC}"
        echo -e "${RED}❌ Significant optimizations required${NC}"
    fi
    
    echo ""
    echo -e "${BLUE}📊 Benchmark completed! Check results above for optimization opportunities.${NC}"
}

# Execute main function
main "$@"