#!/bin/bash

# Simple test to investigate dmsetup message output
echo "=== Simple dmsetup message test ==="

# Create backing file
truncate -s 1G /tmp/test_device.img

# Setup loop device
LOOP_DEV=$(sudo losetup -f --show /tmp/test_device.img)
echo "Using loop device: $LOOP_DEV"

# Load module
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod ../src/dm_remap.ko

# Create target
TARGET_NAME="test-message"
echo "0 2097152 dm-remap $LOOP_DEV 0 2097152 1024 /tmp/metadata.json" | sudo dmsetup create $TARGET_NAME

echo ""
echo "Testing metadata_status message:"

# Capture with different methods
echo "Method 1 - direct capture:"
RESULT=$(sudo dmsetup message $TARGET_NAME 0 metadata_status 2>&1)
echo "Result: '$RESULT'"

echo "Method 2 - with tee to see realtime:"
sudo dmsetup message $TARGET_NAME 0 metadata_status | tee /tmp/dmsetup_output.txt
echo "File contents: '$(cat /tmp/dmsetup_output.txt)'"

echo "Method 3 - check return code:"
sudo dmsetup message $TARGET_NAME 0 metadata_status
echo "Return code: $?"

echo ""
echo "Recent kernel messages:"
dmesg | tail -5 | grep "dm-remap"

# Cleanup
sudo dmsetup remove $TARGET_NAME
sudo losetup -d $LOOP_DEV
rm -f /tmp/test_device.img /tmp/dmsetup_output.txt
