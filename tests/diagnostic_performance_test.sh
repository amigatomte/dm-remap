#!/bin/bash
#
# diagnostic_performance_test.sh - Diagnose why optimized allocation isn't working
#

set -e

echo "=== Performance Optimization Diagnostic ==="

# Create simple test device
dd if=/dev/zero of=/tmp/diag_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=/tmp/diag_spare.img bs=1M count=2 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/diag_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/diag_spare.img)

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(sudo blockdev --getsz "$SPARE_LOOP")

echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create diag-test

echo ""
echo "=== Single Operation Timing ==="

# Test individual operations with detailed timing
for i in $(seq 1 5); do
    sector=$((i * 100))
    
    echo "Testing operation $i (sector $sector)..."
    start_time=$(date +%s%N)
    
    result=$(sudo dmsetup message diag-test 0 remap $sector)
    
    end_time=$(date +%s%N)
    op_time_us=$(((end_time - start_time) / 1000))
    
    echo "  Result: $result"
    echo "  Time: ${op_time_us}μs"
    echo ""
done

echo "=== Testing Debug Commands ==="

# Try some debug commands to see what's happening
echo "Testing ping command..."
sudo dmsetup message diag-test 0 ping

echo ""
echo "Final target status:"
sudo dmsetup status diag-test

# Cleanup
sudo dmsetup remove diag-test
sudo losetup -d "$MAIN_LOOP" "$SPARE_LOOP"
rm -f /tmp/diag_main.img /tmp/diag_spare.img

echo "Diagnostic completed!"