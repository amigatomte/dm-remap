#!/bin/bash
#
# debug_io_forwarding.sh - Debug basic I/O forwarding in dm-remap
#

set -e

echo "=== DEBUG I/O FORWARDING ==="

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove debug-io 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/debug_io_main.img /tmp/debug_io_spare.img
    rm -f /tmp/test_write_io /tmp/test_read_io
    sudo rmmod dm_remap 2>/dev/null || true
}

trap cleanup EXIT

echo "Phase 1: Setup minimal test"
sudo insmod ../src/dm_remap.ko debug_level=3  # Maximum debug

# Create tiny test devices
dd if=/dev/zero of=/tmp/debug_io_main.img bs=1M count=2 2>/dev/null
dd if=/dev/zero of=/tmp/debug_io_spare.img bs=1M count=1 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/debug_io_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/debug_io_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Create dm-remap target"
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 10" | sudo dmsetup create debug-io

echo "Phase 3: Test direct device access"
echo -n "DIRECT_TEST_DATA" > /tmp/test_write_io

echo "Writing directly to main device at sector 10..."
sudo dd if=/tmp/test_write_io of=$MAIN_LOOP bs=512 seek=10 count=1 conv=notrunc 2>/dev/null

echo "Reading directly from main device at sector 10..."
sudo dd if=$MAIN_LOOP of=/tmp/test_read_io bs=512 skip=10 count=1 2>/dev/null

DIRECT_DATA=$(head -c 16 /tmp/test_read_io | tr -d '\0')
echo "Direct read result: '$DIRECT_DATA'"

echo "Phase 4: Test dm-remap forwarding"
echo "Clear kernel messages for clean debug output..."
sudo dmesg -C

echo "Reading through dm-remap at sector 10..."
sudo dd if=/dev/mapper/debug-io of=/tmp/test_read_io bs=512 skip=10 count=1 2>/dev/null

DMREMAP_DATA=$(sudo hexdump -C /dev/mapper/debug-io | head -1 | cut -d'|' -f2 | cut -d'.' -f1)
echo "dm-remap read result: '$DMREMAP_DATA'"

echo "Phase 5: Check kernel debug messages"
echo "Recent kernel messages:"
sudo dmesg -T | tail -10

echo "Phase 6: Analysis"
echo "Direct device: '$DIRECT_DATA'"
echo "dm-remap:      '$DMREMAP_DATA'"

if [ "$DIRECT_DATA" = "$DMREMAP_DATA" ] && [ ! -z "$DMREMAP_DATA" ]; then
    echo "✅ I/O forwarding works correctly"
else
    echo "❌ I/O forwarding is broken"
    echo "  Expected: '$DIRECT_DATA'"
    echo "  Got:      '$DMREMAP_DATA'"
fi

echo "Debug test completed."