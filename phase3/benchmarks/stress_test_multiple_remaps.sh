#!/bin/bash

# Stress Test: dm-remap Multiple Simultaneous Errors
# Tests system stability under high error load
# Monitors: latency, memory usage, CPU utilization

set -e

# Configuration
MAIN_DEVICE="/dev/loop22"
SPARE_DEVICE="/dev/loop23"
DM_REMAP_DEVICE="remap-stress"
MAIN_IMAGE="/tmp/remap_main_stress.img"
SPARE_IMAGE="/tmp/remap_spare_stress.img"
DEVICE_SIZE_MB=200
SPARE_SIZE_MB=100
NUM_REMAPS=50
TEST_DURATION_SEC=60

# Results
RESULTS_DIR="/tmp/remap_stress_results"
mkdir -p "$RESULTS_DIR"

# Cleanup
cleanup() {
    echo "[Cleanup] Stopping workloads..."
    pkill -f "remap_workload" 2>/dev/null || true
    
    echo "[Cleanup] Removing dm-remap device..."
    sudo dmsetup remove "$DM_REMAP_DEVICE" 2>/dev/null || true
    
    echo "[Cleanup] Detaching loop devices..."
    sudo losetup -d "$MAIN_DEVICE" 2>/dev/null || true
    sudo losetup -d "$SPARE_DEVICE" 2>/dev/null || true
    
    echo "[Cleanup] Removing image files..."
    rm -f "$MAIN_IMAGE" "$SPARE_IMAGE"
}

# Setup
setup() {
    echo "=========================================="
    echo "dm-remap Stress Test Suite"
    echo "=========================================="
    echo ""
    echo "Configuration:"
    echo "  Number of remaps: $NUM_REMAPS"
    echo "  Test duration: $TEST_DURATION_SEC seconds"
    echo ""
    
    cleanup 2>/dev/null || true
    
    echo "[Setup] Creating image files..."
    dd if=/dev/zero of="$MAIN_IMAGE" bs=1M count=$DEVICE_SIZE_MB 2>/dev/null
    dd if=/dev/zero of="$SPARE_IMAGE" bs=1M count=$SPARE_SIZE_MB 2>/dev/null
    
    echo "[Setup] Creating loop devices..."
    MAIN_DEVICE=$(sudo losetup -f --show "$MAIN_IMAGE")
    SPARE_DEVICE=$(sudo losetup -f --show "$SPARE_IMAGE")
    
    echo "  Main: $MAIN_DEVICE"
    echo "  Spare: $SPARE_DEVICE"
    echo ""
    
    echo "[Setup] Creating dm-remap device..."
    echo "0 $(($DEVICE_SIZE_MB * 2048)) dm-remap-v4 $MAIN_DEVICE $SPARE_DEVICE" | \
        sudo dmsetup create "$DM_REMAP_DEVICE"
    
    sleep 1
    echo "  Device: /dev/mapper/$DM_REMAP_DEVICE"
    echo ""
}

# Create multiple remaps
create_remaps() {
    echo "[Stress] Creating $NUM_REMAPS remaps..."
    
    for i in $(seq 1 $NUM_REMAPS); do
        bad_sector=$((i * 100))
        spare_sector=$((10000 + i * 100))
        
        sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$bad_sector" "$spare_sector"
        
        if [ $((i % 10)) -eq 0 ]; then
            echo "  Created $i/$NUM_REMAPS remaps"
        fi
    done
    
    echo "  ✓ All $NUM_REMAPS remaps created"
    echo ""
}

# Workload: Random read from remapped sectors
remap_workload() {
    local duration=$1
    local output_file=$2
    local end_time=$((SECONDS + duration))
    local op_count=0
    
    echo "Starting random read workload (${duration}s)..."
    
    while [ $SECONDS -lt $end_time ]; do
        # Pick random remapped sector
        bad_sector=$((RANDOM % NUM_REMAPS * 100 + 100))
        
        # Read from it
        start_ns=$(date +%s%N)
        sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$bad_sector 2>/dev/null
        end_ns=$(date +%s%N)
        
        latency_us=$(( (end_ns - start_ns) / 1000 ))
        echo "$op_count $latency_us" >> "$output_file"
        
        ((op_count++))
        
        # Progress indicator
        if [ $((op_count % 100)) -eq 0 ]; then
            elapsed=$((SECONDS - (end_time - duration)))
            echo "  Progress: $op_count operations in ${elapsed}s"
        fi
    done
    
    echo "Workload complete: $op_count operations"
}

# Monitor system resources
monitor_system() {
    local duration=$1
    local output_file=$2
    local end_time=$((SECONDS + duration))
    
    echo "Starting system monitoring (${duration}s)..."
    
    while [ $SECONDS -lt $end_time ]; do
        timestamp=$(date +%s)
        cpu_usage=$(top -bn1 | grep "Cpu(s)" | sed "s/.*, *\([0-9.]*\)%* id.*/\1/" | awk '{print 100 - $1}')
        mem_usage=$(free | grep Mem | awk '{print ($3/$2) * 100}')
        
        echo "$timestamp $cpu_usage $mem_usage" >> "$output_file"
        
        sleep 1
    done
}

# Test 1: Sustained load with multiple remaps
test_sustained_load() {
    echo "[Test 1] Sustained Load with Multiple Remaps"
    echo "============================================"
    echo ""
    
    local latency_file="$RESULTS_DIR/sustained_latency.txt"
    local system_file="$RESULTS_DIR/sustained_system.txt"
    
    create_remaps
    
    # Start monitoring in background
    monitor_system $TEST_DURATION_SEC "$system_file" &
    local monitor_pid=$!
    
    # Run workload
    remap_workload $TEST_DURATION_SEC "$latency_file"
    
    wait $monitor_pid
    
    # Analyze results
    echo ""
    echo "Results Analysis:"
    if [ -f "$latency_file" ]; then
        avg_latency=$(awk '{sum+=$2; count++} END {print sum/count}' "$latency_file")
        max_latency=$(awk '{print $2}' "$latency_file" | sort -n | tail -1)
        min_latency=$(awk '{print $2}' "$latency_file" | sort -n | head -1)
        
        echo "  Average latency: ${avg_latency} μs"
        echo "  Min latency: ${min_latency} μs"
        echo "  Max latency: ${max_latency} μs"
    fi
    
    if [ -f "$system_file" ]; then
        avg_cpu=$(awk '{sum+=$2; count++} END {print sum/count}' "$system_file")
        avg_mem=$(awk '{sum+=$3; count++} END {print sum/count}' "$system_file")
        
        echo "  Average CPU: ${avg_cpu}%"
        echo "  Average memory: ${avg_mem}%"
    fi
    
    echo ""
    echo "Results saved to:"
    echo "  $latency_file"
    echo "  $system_file"
    echo ""
}

# Test 2: Sparse load verification
test_sparse_load() {
    echo "[Test 2] Sparse Load (Reduced Remap Count)"
    echo "=========================================="
    echo ""
    
    local num_sparse=$((NUM_REMAPS / 5))
    local latency_file="$RESULTS_DIR/sparse_latency.txt"
    
    echo "Creating $num_sparse remaps for sparse test..."
    
    for i in $(seq 1 $num_sparse); do
        bad_sector=$((30000 + i * 500))
        spare_sector=$((40000 + i * 500))
        sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$bad_sector" "$spare_sector"
    done
    
    echo "Running sparse load test..."
    remap_workload $((TEST_DURATION_SEC / 2)) "$latency_file"
    
    echo ""
    echo "Results saved to: $latency_file"
    echo ""
}

# Verify modules
verify_modules() {
    echo "[Verify] Checking dm-remap modules..."
    
    if ! lsmod | grep -q dm_remap_v4_real; then
        echo "Loading modules..."
        sudo modprobe dm-bufio
        sudo insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-stats.ko
        sudo insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-real.ko
        sleep 1
    fi
    
    echo "  ✓ Modules loaded"
    echo ""
}

# Main
main() {
    trap cleanup EXIT
    
    verify_modules
    setup
    
    echo "Starting stress tests..."
    echo ""
    
    test_sustained_load
    test_sparse_load
    
    echo "=========================================="
    echo "Stress Tests Complete!"
    echo "=========================================="
    echo ""
    echo "Results saved to: $RESULTS_DIR"
    echo ""
}

main "$@"
