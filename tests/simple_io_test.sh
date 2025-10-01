#!/bin/bash
#
# simple_io_test.sh - Simple I/O test to isolate the issue
#

set -e

echo "=== SIMPLE I/O TEST ==="

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove simple-test 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/simple_main.img /tmp/simple_spare.img /tmp/test_data
    sudo rmmod dm_remap 2>/dev/null || true
}

trap cleanup EXIT

echo "Phase 1: Setup"
sudo insmod ../src/dm_remap.ko debug_level=3

# Create a small test device
dd if=/dev/zero of=/tmp/simple_main.img bs=1M count=1 2>/dev/null
MAIN_LOOP=$(sudo losetup -f --show /tmp/simple_main.img)
echo "Main device: $MAIN_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Create dm-remap target (main device only)"
# Create a dummy spare device 
dd if=/dev/zero of=/tmp/simple_spare.img bs=1M count=1 2>/dev/null
SPARE_LOOP=$(sudo losetup -f --show /tmp/simple_spare.img)
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 10" | sudo dmsetup create simple-test

echo "Phase 3: Write test data directly to device"
echo -n "TEST123" > /tmp/test_data
sudo dd if=/tmp/test_data of=$MAIN_LOOP bs=512 seek=1 count=1 conv=notrunc 2>/dev/null

echo "Phase 4: Read directly from device"
sudo dd if=$MAIN_LOOP of=/tmp/test_data bs=512 skip=1 count=1 2>/dev/null
DIRECT_READ=$(head -c 7 /tmp/test_data)
echo "Direct read: '$DIRECT_READ'"

echo "Phase 5: Read through dm-remap"
sudo dmesg -C  # Clear messages
sudo dd if=/dev/mapper/simple-test of=/tmp/test_data bs=512 skip=1 count=1 2>/dev/null
DMREMAP_READ=$(head -c 7 /tmp/test_data)
echo "dm-remap read: '$DMREMAP_READ'"

echo "Phase 6: Kernel messages"
sudo dmesg -T | tail -5

echo "Results:"
echo "Direct:   '$DIRECT_READ'"
echo "dm-remap: '$DMREMAP_READ'"

if [ "$DIRECT_READ" = "$DMREMAP_READ" ] && [ ! -z "$DMREMAP_READ" ]; then
    echo "✅ Basic I/O forwarding works"
else
    echo "❌ Basic I/O forwarding is broken"
fi