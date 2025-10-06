#!/bin/bash
#
# quick_performance_test.sh - Quick test of optimized allocation performance
#

set -e

echo "=== Quick Performance Test for Optimized Allocation ==="

# Create test devices
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/quick_main.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=/tmp/quick_spare.img bs=1M count=4 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/quick_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/quick_spare.img)

echo "Test devices: $MAIN_LOOP (main), $SPARE_LOOP (spare)"

# Create dm-remap target
MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(sudo blockdev --getsz "$SPARE_LOOP")

echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create quick-perf-test

if ! sudo dmsetup info quick-perf-test | grep -q "State.*ACTIVE"; then
    echo "❌ Failed to create test target"
    exit 1
fi

echo ""
echo "=== Testing Allocation Performance ==="

# Test 100 allocations and measure time
echo "Performing 100 allocation operations..."
start_time=$(date +%s%N)

for i in $(seq 1 100); do
    sector=$((i * 10 + 1000))
    result=$(sudo dmsetup message quick-perf-test 0 remap $sector 2>&1)
    
    if [[ "$result" == *"error"* ]]; then
        if [[ "$result" == *"no unreserved spare sectors available"* ]]; then
            echo "  Reached capacity at operation $i"
            break
        else
            echo "  Error at operation $i: $result"
            break
        fi
    fi
    
    if [[ $((i % 25)) -eq 0 ]]; then
        echo "    Completed $i operations..."
    fi
done

end_time=$(date +%s%N)
total_time_ns=$((end_time - start_time))
total_time_ms=$((total_time_ns / 1000000))
avg_time_us=$((total_time_ns / i / 1000))

echo ""
echo "Performance Results:"
echo "  Operations completed: $i"
echo "  Total time: ${total_time_ms}ms"
echo "  Average time per operation: ${avg_time_us}μs"

# Performance assessment
if [[ $avg_time_us -le 100 ]]; then
    echo "  🚀 EXCELLENT: Target achieved (≤100μs)!"
elif [[ $avg_time_us -le 1000 ]]; then
    echo "  ✅ GOOD: Significant improvement from baseline"
elif [[ $avg_time_us -le 10000 ]]; then
    echo "  ⚠️ IMPROVED: Better than baseline but needs more optimization"
else
    echo "  ❌ NEEDS WORK: Still too slow"
fi

# Get final target status
echo ""
echo "Final target status:"
sudo dmsetup status quick-perf-test

# Cleanup
echo ""
echo "Cleaning up..."
sudo dmsetup remove quick-perf-test
sudo losetup -d "$MAIN_LOOP"
sudo losetup -d "$SPARE_LOOP"
rm -f /tmp/quick_main.img /tmp/quick_spare.img

echo "Quick performance test completed!"