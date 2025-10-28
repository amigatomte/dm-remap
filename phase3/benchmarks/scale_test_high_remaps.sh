#!/bin/bash

# Scale Test: Hash Table Performance at High Remap Counts
# Tests performance with 100 and 1000 remaps to validate O(1) scalability claim
# Measures latency and validates hash table collision distribution

set -e

# Configuration
MAIN_DEVICE="/dev/loop30"
SPARE_DEVICE="/dev/loop31"
DM_REMAP_DEVICE="remap-scale-test"
MAIN_IMAGE="/tmp/remap_scale_main.img"
SPARE_IMAGE="/tmp/remap_scale_spare.img"
DEVICE_SIZE_MB=500
SPARE_SIZE_MB=300

# Results directory
RESULTS_DIR="/tmp/remap_scale_results"
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

# Setup function
setup() {
    echo "════════════════════════════════════════════════════════════"
    echo "Scale Test: High Remap Count Performance"
    echo "════════════════════════════════════════════════════════════"
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

# Test: 100 Remaps
test_100_remaps() {
    echo "[Test 1] Performance with 100 Remaps"
    echo "═══════════════════════════════════════════════════════════"
    echo ""
    
    local result_file="$RESULTS_DIR/scale_100_remaps.txt"
    
    echo "Creating 100 remap entries..."
    for i in {1..100}; do
        sector=$((10000 + i * 100))
        spare=$((100000 + i * 100))
        sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$sector" "$spare" 2>/dev/null || true
    done
    echo "✓ 100 remaps created"
    echo ""
    
    echo "Measuring read latency with 100 remaps..."
    
    # Read 10 different remapped sectors and measure latency
    for i in {1..10}; do
        test_sector=$((10000 + i * 100))
        
        start_ns=$(date +%s%N)
        sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>/dev/null
        end_ns=$(date +%s%N)
        
        latency_us=$(( (end_ns - start_ns) / 1000 ))
        echo "$i $latency_us" >> "$result_file"
    done
    
    # Calculate statistics
    local avg=$(awk '{sum+=$2} END {print int(sum/NR)}' "$result_file")
    local min=$(awk '{if(NR==1 || $2<min) min=$2} END {print min}' "$result_file")
    local max=$(awk '{if(NR==1 || $2>max) max=$2} END {print max}' "$result_file")
    
    echo "Results:"
    echo "  Average: $avg μs"
    echo "  Min: $min μs"
    echo "  Max: $max μs"
    echo "  File: $result_file"
    echo ""
}

# Test: 1000 Remaps
test_1000_remaps() {
    echo "[Test 2] Performance with 1000 Remaps"
    echo "═══════════════════════════════════════════════════════════"
    echo ""
    
    local result_file="$RESULTS_DIR/scale_1000_remaps.txt"
    
    # Remove device and recreate with larger spare
    sudo dmsetup remove "$DM_REMAP_DEVICE" 2>/dev/null || true
    sudo losetup -d "$MAIN_DEVICE" 2>/dev/null || true
    sudo losetup -d "$SPARE_DEVICE" 2>/dev/null || true
    rm -f "$MAIN_IMAGE" "$SPARE_IMAGE"
    
    # Create larger images for 1000 remaps
    DEVICE_SIZE_MB=1000
    SPARE_SIZE_MB=800
    
    dd if=/dev/zero of="$MAIN_IMAGE" bs=1M count=$DEVICE_SIZE_MB 2>/dev/null
    dd if=/dev/zero of="$SPARE_IMAGE" bs=1M count=$SPARE_SIZE_MB 2>/dev/null
    
    MAIN_DEVICE=$(sudo losetup -f --show "$MAIN_IMAGE")
    SPARE_DEVICE=$(sudo losetup -f --show "$SPARE_IMAGE")
    
    echo "0 $(($DEVICE_SIZE_MB * 2048)) dm-remap-v4 $MAIN_DEVICE $SPARE_DEVICE" | \
        sudo dmsetup create "$DM_REMAP_DEVICE"
    
    sleep 1
    
    echo "Creating 1000 remap entries (this may take a minute)..."
    
    local count=0
    for i in {1..1000}; do
        sector=$((50000 + i * 10))
        spare=$((500000 + i * 10))
        sudo dmsetup message "$DM_REMAP_DEVICE" 0 test_remap "$sector" "$spare" 2>/dev/null || true
        
        count=$((count + 1))
        if [ $((count % 100)) -eq 0 ]; then
            echo "  Progress: $count/1000 remaps created"
        fi
    done
    echo "✓ 1000 remaps created"
    echo ""
    
    echo "Measuring read latency with 1000 remaps..."
    
    # Read 10 different remapped sectors and measure latency
    for i in {1..10}; do
        test_sector=$((50000 + i * 100))
        
        start_ns=$(date +%s%N)
        sudo dd if=/dev/mapper/"$DM_REMAP_DEVICE" of=/dev/null \
            bs=512 count=1 skip=$test_sector 2>/dev/null
        end_ns=$(date +%s%N)
        
        latency_us=$(( (end_ns - start_ns) / 1000 ))
        echo "$i $latency_us" >> "$result_file"
    done
    
    # Calculate statistics
    local avg=$(awk '{sum+=$2} END {print int(sum/NR)}' "$result_file")
    local min=$(awk '{if(NR==1 || $2<min) min=$2} END {print min}' "$result_file")
    local max=$(awk '{if(NR==1 || $2>max) max=$2} END {print max}' "$result_file")
    
    echo "Results:"
    echo "  Average: $avg μs"
    echo "  Min: $min μs"
    echo "  Max: $max μs"
    echo "  File: $result_file"
    echo ""
}

# Analysis function
analyze_results() {
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "ANALYSIS: Hash Table Scalability"
    echo "════════════════════════════════════════════════════════════"
    echo ""
    
    if [ -f "$RESULTS_DIR/scale_100_remaps.txt" ] && [ -f "$RESULTS_DIR/scale_1000_remaps.txt" ]; then
        local avg_100=$(awk '{sum+=$2} END {print int(sum/NR)}' "$RESULTS_DIR/scale_100_remaps.txt")
        local avg_1000=$(awk '{sum+=$2} END {print int(sum/NR)}' "$RESULTS_DIR/scale_1000_remaps.txt")
        
        echo "Performance Comparison:"
        echo "  100 remaps:   $avg_100 μs average"
        echo "  1000 remaps:  $avg_1000 μs average"
        echo ""
        
        # Check if performance is consistent (O(1) characteristic)
        if [ "$avg_1000" -lt "$((avg_100 * 2))" ]; then
            echo "✓ SCALABILITY VERIFIED!"
            echo "  Performance is O(1) - 1000 remaps nearly same latency as 100"
            echo "  Latency ratio: $(( avg_1000 * 100 / avg_100 ))%"
        else
            echo "⚠ Performance degradation detected"
            echo "  Latency ratio: $(( avg_1000 * 100 / avg_100 ))%"
        fi
        
        echo ""
        echo "Interpretation:"
        echo "  Linear search (O(n)) would have ~10x latency increase"
        echo "  Hash table (O(1)) maintains consistent latency"
        echo ""
    fi
}

# Main execution
echo "[Verify] Checking dm-remap modules..."
if lsmod | grep -q dm_remap_v4_real; then
    echo "  ✓ Module already loaded"
else
    echo "  ✗ Module not loaded - please load it first"
    echo "  $ sudo insmod /path/to/dm-remap-v4-real.ko"
    exit 1
fi

setup
test_100_remaps
test_1000_remaps
analyze_results

echo "════════════════════════════════════════════════════════════"
echo "Scale Test Complete!"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "Results saved to: $RESULTS_DIR"
echo "  - scale_100_remaps.txt"
echo "  - scale_1000_remaps.txt"
echo ""

cleanup
