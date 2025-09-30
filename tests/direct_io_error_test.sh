#!/bin/bash

# dm-remap v2.0 Direct I/O Error Injection Test
# Tests auto-remap intelligence with direct block device I/O to bypass filesystem buffering

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

echo "=== dm-remap v2.0 Direct I/O Error Injection Test ==="
echo "Testing auto-remap intelligence with direct block I/O and aggressive error injection"
echo

# Cleanup function
cleanup() {
    echo "Cleaning up direct I/O error test environment..."
    sudo dmsetup remove dm_remap_direct_test 2>/dev/null || true
    sudo dmsetup remove flakey_aggressive 2>/dev/null || true
    sudo losetup -d "$LOOP_MAIN" 2>/dev/null || true
    sudo losetup -d "$LOOP_SPARE" 2>/dev/null || true
    sudo rm -f /tmp/direct_test_main.img /tmp/direct_test_spare.img
}

trap cleanup EXIT

# Setup
echo "Setting up direct I/O error injection test..."
sudo dd if=/dev/zero of=/tmp/direct_test_main.img bs=1M count=20 2>/dev/null
sudo dd if=/dev/zero of=/tmp/direct_test_spare.img bs=1M count=10 2>/dev/null

LOOP_MAIN=$(sudo losetup -f --show /tmp/direct_test_main.img)
LOOP_SPARE=$(sudo losetup -f --show /tmp/direct_test_spare.img)

MAIN_SIZE=$(sudo blockdev --getsz $LOOP_MAIN)
SPARE_SIZE=$(sudo blockdev --getsz $LOOP_SPARE)

echo "Main device: $LOOP_MAIN (${MAIN_SIZE} sectors)"
echo "Spare device: $LOOP_SPARE (${SPARE_SIZE} sectors)"

# Create aggressive dm-flakey device - errors 50% of the time
echo "Creating aggressive error injection layer (50% error rate)..."
echo "0 $MAIN_SIZE flakey $LOOP_MAIN 0 1 1 1 error_writes" | sudo dmsetup create flakey_aggressive

# Create dm-remap on top
echo "0 $MAIN_SIZE remap /dev/mapper/flakey_aggressive $LOOP_SPARE 0 $SPARE_SIZE" | sudo dmsetup create dm_remap_direct_test

echo "✓ Setup complete"
echo

# Test with direct I/O (bypassing filesystem)
echo "=== Direct Block I/O Error Injection Test ==="
echo "Performing raw block device I/O with error injection..."

INITIAL_STATUS=$(sudo dmsetup status dm_remap_direct_test)
echo "Initial status: $INITIAL_STATUS"

# Perform direct writes that should trigger errors
echo "Attempting direct writes to sectors likely to encounter errors..."

for sector in 100 200 300 500 1000 1500 2000; do
    echo "Testing sector $sector..."
    
    # Direct write with potential for error
    if sudo dd if=/dev/urandom of=/dev/mapper/dm_remap_direct_test bs=512 seek=$sector count=8 oflag=direct 2>/dev/null; then
        echo "  Write succeeded (no error or auto-remapped)"
    else
        echo "  Write failed (expected with error injection)"
    fi
    
    # Check status after each write
    STATUS=$(sudo dmsetup status dm_remap_direct_test)
    if [[ $STATUS =~ errors=W([0-9]+):R([0-9]+) ]]; then
        ERRORS_W=${BASH_REMATCH[1]}
        ERRORS_R=${BASH_REMATCH[2]}
        if [ "$ERRORS_W" -gt 0 ] || [ "$ERRORS_R" -gt 0 ]; then
            echo "  🎯 Error detected! Write=$ERRORS_W, Read=$ERRORS_R"
        fi
    fi
    
    if [[ $STATUS =~ auto_remaps=([0-9]+) ]]; then
        AUTO_REMAPS=${BASH_REMATCH[1]}
        if [ "$AUTO_REMAPS" -gt 0 ]; then
            echo "  🚀 Auto-remap triggered! Count: $AUTO_REMAPS"
        fi
    fi
    
    sleep 1
done

# Final status check
FINAL_STATUS=$(sudo dmsetup status dm_remap_direct_test)
echo
echo "Final status: $FINAL_STATUS"

# Check kernel messages for dm-remap activity
echo
echo "=== Checking kernel messages for dm-remap activity ==="
sudo dmesg | grep -i "dm-remap\|flakey" | tail -10 || echo "No recent dm-remap messages"

echo
echo "=== Direct I/O Error Injection Test Complete ==="
echo "If no errors were triggered, this suggests:"
echo "1. dm-flakey error injection needs different configuration"
echo "2. Auto-remap intelligence may need bio-level error detection"
echo "3. Error handling occurs at a different layer than expected"