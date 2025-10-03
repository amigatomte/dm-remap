#!/bin/bash
set -e

# Clean up any existing setup
sudo rmmod dm_remap 2>/dev/null || true

# Create test devices
dd if=/dev/zero of=/tmp/debug_main.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=/tmp/debug_spare.img bs=1M count=10 2>/dev/null

MAIN_LOOP=$(losetup -f --show /tmp/debug_main.img)
SPARE_LOOP=$(losetup -f --show /tmp/debug_spare.img)

# Load module and create target
cd /home/christian/kernel_dev/dm-remap/src
sudo insmod dm_remap.ko
SECTORS=$(blockdev --getsz $MAIN_LOOP)
echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create debug-test

echo "=== Testing ping command ==="
PING_RESULT=$(sudo dmsetup message debug-test 0 ping 2>/dev/null || echo "no_output")
echo "Ping result: '$PING_RESULT'"

echo "=== Testing metadata_status command ==="
STATUS_RESULT=$(sudo dmsetup message debug-test 0 metadata_status 2>/dev/null || echo "no_output")
echo "Status result: '$STATUS_RESULT'"

echo "=== Testing with stderr capture ==="
PING_RESULT2=$(sudo dmsetup message debug-test 0 ping 2>&1 || echo "failed")
echo "Ping result with stderr: '$PING_RESULT2'"

STATUS_RESULT2=$(sudo dmsetup message debug-test 0 metadata_status 2>&1 || echo "failed")
echo "Status result with stderr: '$STATUS_RESULT2'"

# Cleanup
sudo dmsetup remove debug-test
sudo losetup -d $MAIN_LOOP $SPARE_LOOP
sudo rmmod dm_remap
rm -f /tmp/debug_main.img /tmp/debug_spare.img
