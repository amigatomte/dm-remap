#!/bin/bash
#
# Simplified test to debug the freeze issue
# This will monitor dmesg in real-time
#

set -e

echo "Freeze Debug Test"
echo "================="
echo ""

# Start dmesg monitoring in background
echo "Starting dmesg monitor..."
sudo dmesg -C  # Clear previous logs
sudo dmesg -w > /tmp/dm_freeze_debug.log 2>&1 &
DMESG_PID=$!
echo "dmesg monitor PID: $DMESG_PID"
sleep 1

# Cleanup function
cleanup() {
    echo "Cleanup: killing dmesg monitor"
    sudo kill $DMESG_PID 2>/dev/null || true
    sudo dmsetup remove testdev_remap 2>/dev/null || true
    sudo dmsetup remove dm-main-errors 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/test_main_remap.img /tmp/test_spare_remap.img /tmp/dm-table-main.txt
}

trap cleanup EXIT

# Cleanup any existing setup
sudo dmsetup remove testdev_remap 2>/dev/null || true
sudo dmsetup remove dm-main-errors 2>/dev/null || true
sudo losetup -d /dev/loop20 2>/dev/null || true
sudo losetup -d /dev/loop21 2>/dev/null || true

# Load module
echo "Loading module..."
sudo rmmod dm-remap-v4-real 2>/dev/null || true
sudo modprobe dm-mod
sudo insmod src/dm-remap-v4-real.ko real_device_mode=1
echo "✓ Module loaded"
sleep 1

# Create test images
echo "Creating test images..."
dd if=/dev/zero of=/tmp/test_main_remap.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare_remap.img bs=1M count=100 2>/dev/null
echo "✓ Images created"

# Setup loop devices
echo "Setting up loop devices..."
sudo losetup /dev/loop20 /tmp/test_main_remap.img
sudo losetup /dev/loop21 /tmp/test_spare_remap.img
echo "✓ Loop devices ready"

# Create dm-linear with ONE error at sector 1000
echo "Creating dm-error device..."
echo "0 1000 linear /dev/loop20 0" > /tmp/dm-table-main.txt
echo "1000 8 error" >> /tmp/dm-table-main.txt
echo "1008 203792 linear /dev/loop20 1008" >> /tmp/dm-table-main.txt
sudo dmsetup create dm-main-errors < /tmp/dm-table-main.txt
echo "✓ Error device created"

# Create dm-remap device
echo "Creating dm-remap device..."
echo "0 204800 dm-remap-v4 /dev/mapper/dm-main-errors /dev/loop21" | sudo dmsetup create testdev_remap
echo "✓ Device created"
sleep 1

# Trigger error to create ONE remap
echo "Reading sector 1000 to trigger ONE remap..."
sudo dd if=/dev/mapper/testdev_remap bs=512 skip=1000 count=1 of=/dev/null 2>&1 | grep -E "copied|error" || true
echo "✓ Read attempted"

# Wait for remap processing
echo "Waiting 3 seconds for remap..."
sleep 3

# Check status
echo "Device status:"
sudo dmsetup status testdev_remap

echo ""
echo "================================================================"
echo "ABOUT TO REMOVE DEVICE - WATCH FOR PRESUSPEND MESSAGES"
echo "================================================================"
echo ""
echo "Log file: /tmp/dm_freeze_debug.log"
echo ""

# Give user chance to see
sleep 2

echo "Attempting removal..."
timeout 10 sudo dmsetup remove testdev_remap || {
    echo ""
    echo "ERROR: dmsetup remove timed out or failed!"
    echo "Showing last 50 lines of kernel log:"
    tail -50 /tmp/dm_freeze_debug.log
    exit 1
}

echo ""
echo "✓ DEVICE REMOVED SUCCESSFULLY!"
echo ""
echo "Last 50 lines of kernel log:"
tail -50 /tmp/dm_freeze_debug.log
