#!/bin/bash
set -e

# Clean up any existing setup
sudo rmmod dm_remap 2>/dev/null || true

# Create test devices
dd if=/dev/zero of=/tmp/debug_main.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=/tmp/debug_spare.img bs=1M count=10 2>/dev/null

MAIN_LOOP=$(losetup -f --show /tmp/debug_main.img)
SPARE_LOOP=$(losetup -f --show /tmp/debug_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

# Load module
cd /home/christian/kernel_dev/dm-remap/src
sudo insmod dm_remap.ko

# Create target
SECTORS=$(blockdev --getsz $MAIN_LOOP)
echo "Creating target with $SECTORS sectors..."
echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create debug-test

# Test commands
echo "Testing ping command:"
sudo dmsetup message debug-test 0 ping

echo "Testing metadata_status command:"
sudo dmsetup message debug-test 0 metadata_status

echo "Checking kernel logs:"
dmesg | tail -5

# Cleanup
sudo dmsetup remove debug-test
sudo losetup -d $MAIN_LOOP $SPARE_LOOP
sudo rmmod dm_remap
rm -f /tmp/debug_main.img /tmp/debug_spare.img
