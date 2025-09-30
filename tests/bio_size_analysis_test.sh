#!/bin/bash

# dm-remap v2.0 Bio Size Analysis Test
# Debug test to understand what bio sizes are being processed

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

echo "=== dm-remap v2.0 Bio Size Analysis Test ==="
echo "Analyzing bio sizes and error detection with various I/O patterns"
echo

# Cleanup function
cleanup() {
    echo "Cleaning up bio analysis test..."
    sudo dmsetup remove dm_remap_bio_debug 2>/dev/null || true
    sudo dmsetup remove flakey_debug 2>/dev/null || true
    sudo losetup -d "$LOOP_MAIN" 2>/dev/null || true
    sudo losetup -d "$LOOP_SPARE" 2>/dev/null || true
    sudo rm -f /tmp/bio_debug_main.img /tmp/bio_debug_spare.img
}

trap cleanup EXIT

# Setup
echo "Setting up bio analysis test..."
sudo dd if=/dev/zero of=/tmp/bio_debug_main.img bs=1M count=10 2>/dev/null
sudo dd if=/dev/zero of=/tmp/bio_debug_spare.img bs=1M count=5 2>/dev/null

LOOP_MAIN=$(sudo losetup -f --show /tmp/bio_debug_main.img)
LOOP_SPARE=$(sudo losetup -f --show /tmp/bio_debug_spare.img)

MAIN_SIZE=$(sudo blockdev --getsz $LOOP_MAIN)
SPARE_SIZE=$(sudo blockdev --getsz $LOOP_SPARE)

echo "Main device: $LOOP_MAIN (${MAIN_SIZE} sectors)"
echo "Spare device: $LOOP_SPARE (${SPARE_SIZE} sectors)"

# Create error-injecting device
echo "0 $MAIN_SIZE flakey $LOOP_MAIN 0 1 1 1 error_writes" | sudo dmsetup create flakey_debug

# Create dm-remap
echo "0 $MAIN_SIZE remap /dev/mapper/flakey_debug $LOOP_SPARE 0 $SPARE_SIZE" | sudo dmsetup create dm_remap_bio_debug

echo "✓ Setup complete"
echo

# Clear kernel messages
sudo dmesg -C

echo "=== Testing Different I/O Patterns ==="

# Test 1: Single sector (512 bytes) - should be tracked
echo "Test 1: Single sector writes (512 bytes)..."
for i in {1..3}; do
    echo "  Single sector write $i..."
    sudo dd if=/dev/urandom of=/dev/mapper/dm_remap_bio_debug bs=512 count=1 seek=$((100 + i)) oflag=direct 2>/dev/null || echo "    Expected error"
    sleep 1
done

# Test 2: Multiple single sector writes
echo "Test 2: Multiple single sector writes with specific spacing..."
for sector in 200 201 202; do
    echo "  Writing to sector $sector..."
    sudo dd if=/dev/urandom of=/dev/mapper/dm_remap_bio_debug bs=512 count=1 seek=$sector oflag=direct 2>/dev/null || echo "    Expected error"
    sleep 1
done

# Test 3: Multi-sector writes (currently not tracked)
echo "Test 3: Multi-sector writes (4KB)..."
for i in {1..3}; do
    echo "  Multi-sector write $i (4KB)..."
    sudo dd if=/dev/urandom of=/dev/mapper/dm_remap_bio_debug bs=4096 count=1 seek=$((300 + i)) oflag=direct 2>/dev/null || echo "    Expected error"
    sleep 1
done

echo
echo "=== Status Check ==="
STATUS=$(sudo dmsetup status dm_remap_bio_debug)
echo "Current status: $STATUS"

# Parse statistics
if [[ $STATUS =~ errors=W([0-9]+):R([0-9]+) ]]; then
    WRITE_ERRORS=${BASH_REMATCH[1]}
    READ_ERRORS=${BASH_REMATCH[2]}
    echo "Error counts: Write=$WRITE_ERRORS, Read=$READ_ERRORS"
fi

if [[ $STATUS =~ auto_remaps=([0-9]+) ]]; then
    AUTO_REMAPS=${BASH_REMATCH[1]}
    echo "Auto-remaps: $AUTO_REMAPS"
fi

echo
echo "=== Kernel Messages Analysis ==="
echo "Recent dm-remap kernel messages:"
sudo dmesg | grep -i "dm-remap" | tail -20 || echo "No dm-remap messages found"

echo
echo "Recent error-related messages:"
sudo dmesg | grep -i "error\|flakey" | tail -10 || echo "No error messages found"

echo
echo "=== Analysis Results ==="
if [[ $WRITE_ERRORS -gt 0 ]] || [[ $READ_ERRORS -gt 0 ]]; then
    echo "✅ Error detection is working!"
    echo "   Write errors: $WRITE_ERRORS"
    echo "   Read errors: $READ_ERRORS"
else
    echo "⚠ No errors detected. Possible causes:"
    echo "   1. Bio tracking only handles 512-byte I/Os"
    echo "   2. Error injection timing issues"
    echo "   3. Bio endio callback not being called"
fi

if [[ $AUTO_REMAPS -gt 0 ]]; then
    echo "✅ Auto-remap intelligence is working!"
else
    echo "⚠ No auto-remaps triggered. Check error detection and auto-remap logic."
fi

echo
echo "Recommendations for next iteration:"
echo "  - Enable tracking for larger bio sizes (4KB, 8KB)"
echo "  - Add more debug output in bio endio callback"
echo "  - Test with even more aggressive error injection"