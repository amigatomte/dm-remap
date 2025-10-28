#!/bin/bash

# Benchmark: dm-remap Remap Operation Latency
# Tests the performance of individual remap operations
# Measures: first access time, cached access time, distribution

set -e

# Configuration
MAIN_DEVICE="/dev/loop20"
SPARE_DEVICE="/dev/loop21"
DM_REMAP_DEVICE="remap-bench"
MAIN_IMAGE="/tmp/remap_main_bench.img"
SPARE_IMAGE="/tmp/remap_spare_bench.img"
DEVICE_SIZE_MB=100
SPARE_SIZE_MB=50

# Results
RESULTS_DIR="/tmp/remap_benchmarks"
mkdir -p "$RESULTS_DIR"

# Cleanup function
cleanup() {
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
    echo "dm-remap Remap Latency Benchmark"
    echo "=========================================="
    echo ""
    
    # Clean up any existing setup
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

# Test: First Access Latency (with error detection)
test_first_access_latency() {
    echo "[Test 1] First Access Latency (with error detection)"
    echo "==========================================="
    echo ""
    
    local test_sector=1000
    local result_file="$RESULTS_DIR/first_access_latency.txt"
    
    echo "Creating remap entry at sector $test_sector..."
    sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$test_sector" 5000
    
    echo "Measuring read latency..."
    
    # Read the remapped sector 10 times and measure latency
    for i in {1..10}; do
        # Use dd with time measurement
        time_output=$( { time sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>&1; } 2>&1 )
        
        # Extract user time and sys time
        echo "$time_output" >> "$result_file"
    done
    
    echo "Results saved to $result_file"
    echo ""
}

# Test: Cached Access Latency (subsequent accesses)
test_cached_access_latency() {
    echo "[Test 2] Cached Access Latency (subsequent accesses)"
    echo "======================================================"
    echo ""
    
    local test_sector=2000
    local result_file="$RESULTS_DIR/cached_access_latency.txt"
    
    echo "Creating remap entry at sector $test_sector..."
    sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$test_sector" 5100
    
    echo "Warming up cache (3 accesses)..."
    for i in {1..3}; do
        sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>/dev/null
    done
    
    echo "Measuring cached latency (10 accesses)..."
    
    # Read the remapped sector 10 times
    for i in {1..10}; do
        time_output=$( { time sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>&1; } 2>&1 )
        echo "$time_output" >> "$result_file"
    done
    
    echo "Results saved to $result_file"
    echo ""
}

# Test: Latency Distribution
test_latency_distribution() {
    echo "[Test 3] Latency Distribution (100 accesses)"
    echo "=============================================="
    echo ""
    
    local test_sector=3000
    local result_file="$RESULTS_DIR/latency_distribution.txt"
    
    echo "Creating remap entry at sector $test_sector..."
    sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$test_sector" 5200
    
    echo "Measuring 100 accesses..."
    
    # Read the remapped sector 100 times
    for i in {1..100}; do
        time_output=$( { time sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>&1; } 2>&1 )
        echo "$time_output" >> "$result_file"
        
        # Progress indicator
        if [ $((i % 20)) -eq 0 ]; then
            echo "  Progress: $i/100"
        fi
    done
    
    echo "Results saved to $result_file"
    echo ""
}

# Test: Multiple Remaps Performance
test_multiple_remaps() {
    echo "[Test 4] Multiple Remaps Performance"
    echo "===================================="
    echo ""
    
    local result_file="$RESULTS_DIR/multiple_remaps.txt"
    local num_remaps=10
    
    echo "Creating $num_remaps remap entries..."
    
    for i in $(seq 0 $((num_remaps - 1))); do
        bad_sector=$((4000 + i * 100))
        spare_sector=$((6000 + i * 100))
        sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$bad_sector" "$spare_sector"
    done
    
    echo "Measuring latency with multiple remaps..."
    
    # Read from each remapped sector
    for i in $(seq 0 $((num_remaps - 1))); do
        bad_sector=$((4000 + i * 100))
        
        time_output=$( { time sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$bad_sector 2>&1; } 2>&1 )
        echo "Sector $bad_sector: $time_output" >> "$result_file"
    done
    
    echo "Results saved to $result_file"
    echo ""
}

# Verify modules are loaded
verify_modules() {
    echo "[Verify] Checking dm-remap modules..."
    
    if ! lsmod | grep -q dm_remap_v4_real; then
        echo "ERROR: dm-remap-v4-real module not loaded!"
        echo "Loading it now..."
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
    
    echo "Starting benchmark tests..."
    echo ""
    
    test_first_access_latency
    test_cached_access_latency
    test_latency_distribution
    test_multiple_remaps
    
    echo "=========================================="
    echo "Benchmark Complete!"
    echo "=========================================="
    echo ""
    echo "Results saved to: $RESULTS_DIR"
    echo ""
    echo "Next steps:"
    echo "1. Review latency measurements"
    echo "2. Analyze distribution statistics"
    echo "3. Compare against baseline"
    echo ""
}

main "$@"
