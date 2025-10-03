#!/bin/bash

# Test message output capture with dm-remap
# This script investigates why dmsetup message doesn't return our result strings

echo "=== Testing dmsetup message output capture ==="

# Setup
LOOP_DEV1=$(sudo losetup -f)
LOOP_DEV2=$(sudo losetup -f --show /tmp/test_backing_1GB.img)
echo "Using loop device: $LOOP_DEV2"

# Load module
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod ../src/dm_remap.ko

# Create target with metadata
TARGET_NAME="test-message-output"
echo "0 2097152 dm-remap $LOOP_DEV2 0 2097152 1024 /tmp/metadata.json" | sudo dmsetup create $TARGET_NAME

echo ""
echo "=== Testing different output capture methods ==="

echo "1. Standard dmsetup message (what we've been using):"
OUTPUT1=$(sudo dmsetup message $TARGET_NAME 0 metadata_status 2>&1)
echo "Result: '$OUTPUT1'"
echo "Length: ${#OUTPUT1}"

echo ""
echo "2. Capturing stderr separately:"
OUTPUT2=$(sudo dmsetup message $TARGET_NAME 0 metadata_status 2>/tmp/stderr)
echo "stdout: '$OUTPUT2'"
echo "stderr: '$(cat /tmp/stderr)'"

echo ""
echo "3. Testing with -v (verbose) flag:"
OUTPUT3=$(sudo dmsetup -v message $TARGET_NAME 0 metadata_status 2>&1)
echo "Result: '$OUTPUT3'"

echo ""
echo "4. Testing with different command that we know works:"
OUTPUT4=$(sudo dmsetup message $TARGET_NAME 0 ping 2>&1)
echo "Ping result: '$OUTPUT4'"

echo ""
echo "5. Check kernel messages for our debug output:"
echo "Recent kernel messages:"
dmesg | tail -10 | grep "dm-remap"

echo ""
echo "=== Testing with strace to see system calls ==="
echo "Running dmsetup message under strace:"
sudo strace -e trace=write,read,ioctl dmsetup message $TARGET_NAME 0 metadata_status 2>&1 | head -20

# Cleanup
sudo dmsetup remove $TARGET_NAME 2>/dev/null || true
sudo losetup -d $LOOP_DEV2
rm -f /tmp/stderr

echo ""
echo "=== Test complete ==="