#!/bin/bash

# Stress Test: dm-remap Multiple Simultaneous Errors (SIMPLIFIED & TESTED)
# Tests system stability under remap load
# Straightforward implementation without background process complications

# Note: NOT using set -e to prevent exit on device teardown errors

# Configuration
MAIN_DEVICE="/dev/loop22"
SPARE_DEVICE="/dev/loop23"
DM_REMAP_DEVICE="remap-stress"
MAIN_IMAGE="/tmp/remap_main_stress.img"
SPARE_IMAGE="/tmp/remap_spare_stress.img"
DEVICE_SIZE_MB=200
SPARE_SIZE_MB=100
NUM_REMAPS=50
NUM_ACCESSES=100

# Results
RESULTS_DIR="/tmp/remap_stress_results"
mkdir -p "$RESULTS_DIR"

# Cleanup
cleanup() {
    echo ""
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
    echo "  Accesses per remap: $NUM_ACCESSES"
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
        
        sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$bad_sector" "$spare_sector" > /dev/null
        
        if [ $((i % 10)) -eq 0 ]; then
            echo "  Created $i/$NUM_REMAPS remaps"
        fi
    done
    
    echo "  ✓ All $NUM_REMAPS remaps created"
    echo ""
}

# Test 1: Sequential access to all remapped sectors
test_sequential_access() {
    echo "[Test 1] Sequential Access to All Remapped Sectors"
    echo "===================================================="
    echo ""
    
    local latency_file="$RESULTS_DIR/sequential_latency.txt"
    local total_ops=0
    local total_latency=0
    local max_latency=0
    local min_latency=999999
    
    echo "Accessing all $NUM_REMAPS remapped sectors..."
    
    for i in $(seq 1 $NUM_REMAPS); do
        bad_sector=$((i * 100))
        
        # Check if device still exists before accessing
        if [ ! -e /dev/mapper/"$DM_REMAP_DEVICE" ]; then
            echo "  [Warning] Device disappeared at iteration $i"
            break
        fi
        
        start_ns=$(date +%s%N)
        if ! sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$bad_sector 2>/dev/null; then
            echo "  [Warning] Read failed at sector $bad_sector (iteration $i)"
            break
        fi
        end_ns=$(date +%s%N)
        
        latency_us=$(( (end_ns - start_ns) / 1000 ))
        echo "$i $latency_us" >> "$latency_file"
        
        total_latency=$((total_latency + latency_us))
        ((total_ops++))
        
        if [ $latency_us -gt $max_latency ]; then
            max_latency=$latency_us
        fi
        
        if [ $latency_us -lt $min_latency ]; then
            min_latency=$latency_us
        fi
        
        if [ $((i % 10)) -eq 0 ]; then
            echo "  Progress: $i/$NUM_REMAPS"
        fi
    done
    
    echo ""
    echo "Results Analysis:"
    if [ $total_ops -gt 0 ]; then
        avg_latency=$((total_latency / total_ops))
        echo "  Total operations: $total_ops"
        echo "  Average latency: ${avg_latency} μs"
        echo "  Min latency: ${min_latency} μs"
        echo "  Max latency: ${max_latency} μs"
    fi
    
    echo "  Results saved to: $latency_file"
    echo ""
}

# Test 2: Repeated access to same remapped sector
test_repeated_access() {
    echo "[Test 2] Repeated Access (Cache Warming)"
    echo "=========================================="
    echo ""
    
    local test_sector=500  # 5 * 100
    local latency_file="$RESULTS_DIR/repeated_latency.txt"
    local total_ops=0
    local total_latency=0
    local max_latency=0
    local min_latency=999999
    
    echo "Accessing sector $test_sector repeatedly $NUM_ACCESSES times..."
    
    for i in $(seq 1 $NUM_ACCESSES); do
        # Check if device still exists before accessing
        if [ ! -e /dev/mapper/"$DM_REMAP_DEVICE" ]; then
            echo "  [Warning] Device disappeared at iteration $i"
            break
        fi
        
        start_ns=$(date +%s%N)
        if ! sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>/dev/null; then
            echo "  [Warning] Read failed at iteration $i"
            break
        fi
        end_ns=$(date +%s%N)
        
        latency_us=$(( (end_ns - start_ns) / 1000 ))
        echo "$i $latency_us" >> "$latency_file"
        
        total_latency=$((total_latency + latency_us))
        ((total_ops++))
        
        if [ $latency_us -gt $max_latency ]; then
            max_latency=$latency_us
        fi
        
        if [ $latency_us -lt $min_latency ]; then
            min_latency=$latency_us
        fi
        
        if [ $((i % 20)) -eq 0 ]; then
            echo "  Progress: $i/$NUM_ACCESSES"
        fi
    done
    
    echo ""
    echo "Results Analysis:"
    if [ $total_ops -gt 0 ]; then
        avg_latency=$((total_latency / total_ops))
        echo "  Total operations: $total_ops"
        echo "  Average latency: ${avg_latency} μs"
        echo "  Min latency: ${min_latency} μs"
        echo "  Max latency: ${max_latency} μs"
    fi
    
    echo "  Results saved to: $latency_file"
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
    create_remaps
    
    echo "Starting stress tests..."
    echo ""
    
    test_sequential_access
    test_repeated_access
    
    echo "=========================================="
    echo "Stress Tests Complete!"
    echo "=========================================="
    echo ""
    echo "Results saved to: $RESULTS_DIR"
    echo ""
    echo "To analyze results:"
    echo "  cat $RESULTS_DIR/sequential_latency.txt"
    echo "  cat $RESULTS_DIR/repeated_latency.txt"
    echo ""
}

main "$@"
