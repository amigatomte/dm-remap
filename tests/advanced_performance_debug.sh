#!/bin/bash
#
# advanced_performance_debug.sh - Advanced debugging for cache initialization issues
#

set -e

echo "=== Advanced Performance Debugging Session ==="
echo "Objective: Identify and fix cache initialization failure"
echo ""

# Enable detailed kernel logging
echo "Enabling detailed kernel debugging..."
echo 8 | sudo tee /proc/sys/kernel/printk > /dev/null

# Create test environment with detailed monitoring
echo "Creating monitored test environment..."
dd if=/dev/zero of=/tmp/debug_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=/tmp/debug_spare.img bs=1M count=2 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/debug_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/debug_spare.img)

echo "Test devices: $MAIN_LOOP (main), $SPARE_LOOP (spare)"

# Clear kernel message buffer to see only new messages
sudo dmesg -C

echo ""
echo "=== Creating Target with Debug Monitoring ==="

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(sudo blockdev --getsz "$SPARE_LOOP")

echo "Creating dm-remap target..."
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create debug-target

echo ""
echo "=== Kernel Messages from Target Creation ==="
sudo dmesg | grep -E "(dm-remap|allocation|cache|error|failed)" | tail -20

echo ""
echo "=== Testing Single Allocation with Timing ==="

# Test one allocation with precise timing
sector=1000
start_time=$(date +%s%N)
result=$(sudo dmsetup message debug-target 0 remap $sector 2>&1)
end_time=$(date +%s%N)

allocation_time_us=$(((end_time - start_time) / 1000))

echo "Allocation result: $result"
echo "Allocation time: ${allocation_time_us}μs"

echo ""
echo "=== Kernel Messages from Allocation ==="
sudo dmesg | grep -E "(dm-remap|allocation|cache)" | tail -10

echo ""
echo "=== System Memory Analysis ==="
echo "Available memory:"
free -h
echo ""
echo "Kernel memory info:"
cat /proc/meminfo | grep -E "(MemFree|MemAvailable|Slab)"

echo ""
echo "=== Module Symbol Analysis ==="
echo "Checking if optimized allocation symbols are available..."
if nm /home/christian/kernel_dev/dm-remap/src/dm_remap.ko | grep -q "dmr_allocate_spare_sector_optimized"; then
    echo "✅ Optimized allocation symbol found"
else
    echo "❌ Optimized allocation symbol NOT found"
fi

if nm /home/christian/kernel_dev/dm-remap/src/dm_remap.ko | grep -q "dmr_init_allocation_cache"; then
    echo "✅ Cache initialization symbol found"
else
    echo "❌ Cache initialization symbol NOT found"
fi

# Check if target is actually using optimization
echo ""
echo "=== Performance Analysis ==="
if [[ $allocation_time_us -lt 1000 ]]; then
    echo "🚀 OPTIMIZATION WORKING: Allocation time is excellent"
elif [[ $allocation_time_us -lt 10000 ]]; then
    echo "⚡ PARTIAL OPTIMIZATION: Some improvement visible"
else
    echo "❌ NO OPTIMIZATION: Still using slow path (~20ms suggests cache failure)"
fi

echo ""
echo "=== Detailed Target Status ==="
sudo dmsetup status debug-target
sudo dmsetup table debug-target

# Cleanup
echo ""
echo "=== Cleanup ==="
sudo dmsetup remove debug-target
sudo losetup -d "$MAIN_LOOP" "$SPARE_LOOP"
rm -f /tmp/debug_*.img

echo ""
echo "=== Debug Session Complete ==="
echo "Review the kernel messages above to identify cache initialization issues."