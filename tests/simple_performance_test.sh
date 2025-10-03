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

# Convert to milliseconds for better readability
avg_time_ms=$((avg_time_ns / 1000000))
if [[ $avg_time_ms -eq 0 ]]; then
    avg_time_ms_display="<1ms"
else
    avg_time_ms_display="${avg_time_ms}ms"
fi

echo "  Average per I/O: ${avg_time_ms_display}"

# Check for performance optimizations
fast_path_count=$(sudo dmesg | grep -c "Fast path:" 2>/dev/null || echo "0")
enhanced_io_count=$(sudo dmesg | grep -c "Enhanced I/O:" 2>/dev/null || echo "0")

echo ""
echo "Performance Analysis:"
if [[ $fast_path_count -gt 0 ]]; then
    echo "✅ Fast path optimization: ACTIVE ($fast_path_count fast path operations detected)"
else
    echo "⚠️ Fast path optimization: NOT DETECTED"
fi

if [[ $enhanced_io_count -gt 0 ]]; then
    echo "✅ Enhanced I/O processing: ACTIVE ($enhanced_io_count enhanced operations detected)"
else
    echo "⚠️ Enhanced I/O processing: NOT DETECTED"
fi

# Overall performance assessment
total_optimizations=$((fast_path_count + enhanced_io_count))
if [[ $total_optimizations -gt 5 ]]; then
    echo "✅ PERFORMANCE STATUS: OPTIMIZED"
    echo "   dm-remap performance features are working correctly"
elif [[ $total_optimizations -gt 0 ]]; then
    echo "⚠️ PERFORMANCE STATUS: PARTIALLY OPTIMIZED"
    echo "   Some performance features detected ($total_optimizations optimizations)"
else
    echo "❌ PERFORMANCE STATUS: UNOPTIMIZED"
    echo "   Performance optimizations not detected in kernel logs"
fi

echo ""
echo "Note: Test includes filesystem and dd command overhead."
echo "Pure device mapper latency is typically much lower."

echo "Performance test completed!"