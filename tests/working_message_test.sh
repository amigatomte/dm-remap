#!/bin/bash

# Working test to investigate dmsetup message output
echo "=== Working dmsetup message test ==="

# Create backing file
truncate -s 1G /tmp/test_device.img

# Setup loop device
LOOP_DEV=$(sudo losetup -f --show /tmp/test_device.img)
echo "Using loop device: $LOOP_DEV"

# Load module (already loaded but ensure it's there)
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod ../src/dm_remap.ko

# Create target with CORRECT target name "remap" not "dm-remap"
TARGET_NAME="test-message"
echo "0 2097152 remap $LOOP_DEV 0 2097152 1024 /tmp/metadata.json" | sudo dmsetup create $TARGET_NAME

echo ""
echo "Testing metadata_status message:"

# Test the message command
echo "Direct command test:"
sudo dmsetup message $TARGET_NAME 0 metadata_status
echo "Return code: $?"

echo ""
echo "Capture output test:"
OUTPUT=$(sudo dmsetup message $TARGET_NAME 0 metadata_status 2>&1)
echo "Captured output: '$OUTPUT'"
echo "Output length: ${#OUTPUT}"

echo ""
echo "Test ping command for comparison:"
sudo dmsetup message $TARGET_NAME 0 ping
PING_OUTPUT=$(sudo dmsetup message $TARGET_NAME 0 ping 2>&1)
echo "Ping output: '$PING_OUTPUT'"

echo ""
echo "Recent kernel messages:"
dmesg | tail -10 | grep "dm-remap"

# Cleanup
sudo dmsetup remove $TARGET_NAME
sudo losetup -d $LOOP_DEV
rm -f /tmp/test_device.img /tmp/metadata.json
