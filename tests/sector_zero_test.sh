#!/bin/bash
#
# sector_zero_test.sh - Test sector 0 I/O specifically
#

set -e

echo "=== SECTOR ZERO TEST ==="

# Cleanup function  
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove sector0-test 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/sector0_main.img /tmp/sector0_spare.img /tmp/sector0_data
    sudo rmmod dm_remap 2>/dev/null || true
}

trap cleanup EXIT

echo "Phase 1: Setup"
sudo insmod ../src/dm_remap.ko debug_level=3

# Create test devices
dd if=/dev/zero of=/tmp/sector0_main.img bs=1M count=1 2>/dev/null
dd if=/dev/zero of=/tmp/sector0_spare.img bs=1M count=1 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/sector0_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/sector0_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Create dm-remap target"
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 10" | sudo dmsetup create sector0-test

echo "Phase 3: Write data to sector 0"
echo -n "ZERO" > /tmp/sector0_data

echo "Write to sector 0 directly..."
sudo dd if=/tmp/sector0_data of=$MAIN_LOOP bs=512 seek=0 count=1 conv=notrunc 2>/dev/null

echo "Read from sector 0 directly..."
sudo dd if=$MAIN_LOOP of=/tmp/sector0_data bs=512 skip=0 count=1 2>/dev/null
DIRECT_DATA=$(head -c 4 /tmp/sector0_data | tr -d '\0')
echo "Direct: '$DIRECT_DATA'"

echo "Phase 4: Test through dm-remap"
sudo dmesg -C

echo "Read from sector 0 through dm-remap..."
sudo dd if=/dev/mapper/sector0-test of=/tmp/sector0_data bs=512 skip=0 count=1 2>/dev/null
DMREMAP_DATA=$(head -c 4 /tmp/sector0_data | tr -d '\0')
echo "dm-remap: '$DMREMAP_DATA'"

echo "Phase 5: Debug messages"
sudo dmesg -T | tail -10

echo "Analysis:"
echo "Direct:   '$DIRECT_DATA'"
echo "dm-remap: '$DMREMAP_DATA'"

if [ "$DIRECT_DATA" = "$DMREMAP_DATA" ] && [ ! -z "$DMREMAP_DATA" ]; then
    echo "✅ Sector 0 I/O works"
else
    echo "❌ Sector 0 I/O broken"
fi