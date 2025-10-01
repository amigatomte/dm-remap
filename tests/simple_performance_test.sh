#!/bin/bash
#
# simple_performance_test.sh - Quick performance validation for dm-remap v2.0
#

set -e

echo "=== DM-REMAP v2.0 QUICK PERFORMANCE TEST ==="

# Cleanup function
cleanup() {
    sudo dmsetup remove simple-perf-test 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/simple_perf_main.img /tmp/simple_perf_spare.img /tmp/simple_test_data
}

trap cleanup EXIT

# Create simple test setup
echo "Setting up simple performance test..."

# Create test files
dd if=/dev/zero of=/tmp/simple_perf_main.img bs=1M count=50 2>/dev/null
dd if=/dev/zero of=/tmp/simple_perf_spare.img bs=1M count=5 2>/dev/null
dd if=/dev/urandom of=/tmp/simple_test_data bs=4k count=1 2>/dev/null

# Setup loop devices - use specific loop numbers to avoid conflicts
sudo losetup /dev/loop20 /tmp/simple_perf_main.img
sudo losetup /dev/loop21 /tmp/simple_perf_spare.img

# Get sectors
MAIN_SECTORS=$(sudo blockdev --getsz /dev/loop20)
echo "Main device sectors: $MAIN_SECTORS"

# Create simple device mapping
echo "0 $MAIN_SECTORS remap /dev/loop20 /dev/loop21 0 1000" | sudo dmsetup create simple-perf-test

# Test performance with 10 I/O operations
echo "Testing performance (10 iterations)..."

start_time=$(date +%s%N)
for i in {1..10}; do
    sudo dd if=/tmp/simple_test_data of=/dev/mapper/simple-perf-test bs=4k seek=$((i * 10)) count=1 conv=notrunc 2>/dev/null
done
end_time=$(date +%s%N)

total_time_ns=$((end_time - start_time))
avg_time_ns=$((total_time_ns / 10))

echo "Performance Results:"
echo "  Total time: ${total_time_ns}ns"
echo "  Average per I/O: ${avg_time_ns}ns"

# Check if we meet the 1ms target
if [[ $avg_time_ns -lt 1000000 ]]; then
    echo "✅ Performance target met! (${avg_time_ns}ns < 1ms)"
    echo "🎉 Performance optimization SUCCESS!"
else
    echo "⚠️ Performance needs optimization (${avg_time_ns}ns > 1ms)"
fi

# Check for fast path usage in dmesg
echo "Checking for performance optimization messages..."
sudo dmesg -T | grep -E "(Fast path|Performance)" | tail -5 || echo "No performance messages found in recent dmesg"

echo "Performance test completed!"