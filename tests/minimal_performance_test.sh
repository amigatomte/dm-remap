#!/bin/bash
#
# minimal_performance_test.sh - Minimal test to isolate performance bottleneck
#

set -e

echo "=== Minimal Performance Isolation Test ==="

# Create very small test devices for faster testing
dd if=/dev/zero of=/tmp/mini_main.img bs=1K count=100 2>/dev/null    # 100KB
dd if=/dev/zero of=/tmp/mini_spare.img bs=1K count=50 2>/dev/null    # 50KB

MAIN_LOOP=$(sudo losetup -f --show /tmp/mini_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/mini_spare.img)

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(sudo blockdev --getsz "$SPARE_LOOP")

echo "Tiny test devices: $MAIN_LOOP ($MAIN_SECTORS sectors), $SPARE_LOOP ($SPARE_SECTORS sectors)"

# Clear kernel logs
sudo dmesg -C

# Create target
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create mini-test

echo ""
echo "=== Testing 5 Fast Allocations ==="

for i in $(seq 1 5); do
    sector=$((i * 10))
    echo "Allocation $i (sector $sector):"
    
    start_time=$(date +%s%N)
    result=$(sudo dmsetup message mini-test 0 remap $sector)
    end_time=$(date +%s%N)
    
    time_us=$(((end_time - start_time) / 1000))
    echo "  Time: ${time_us}μs"
    echo "  Result: $result"
done

echo ""
echo "=== Kernel Messages ==="
sudo dmesg | grep -E "(dm-remap|allocation|cache)" | tail -15

echo ""
echo "=== Final Status ==="
sudo dmsetup status mini-test

# Cleanup
sudo dmsetup remove mini-test
sudo losetup -d "$MAIN_LOOP" "$SPARE_LOOP"
rm -f /tmp/mini_*.img

echo "Minimal test complete!"