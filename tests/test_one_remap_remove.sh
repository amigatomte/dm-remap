#!/bin/bash
#
# Test: Create device, trigger ONE error/remap, then remove
# This tests if having remaps in the device causes removal crashes
#

set -e

echo "One Remap Remove Test"
echo "====================="
echo ""

# Cleanup any existing setup
sudo dmsetup remove testdev_remap 2>/dev/null || true
sudo dmsetup remove dm-main-errors 2>/dev/null || true
sudo losetup -d /dev/loop18 2>/dev/null || true
sudo losetup -d /dev/loop19 2>/dev/null || true

# Load modules
echo "Loading modules..."
sudo rmmod dm-remap-v4-real 2>/dev/null || true
sudo modprobe dm-mod
sudo insmod src/dm-remap-v4-real.ko real_device_mode=1
echo "✓ Module loaded"
echo ""

# Create test images
echo "Creating test images..."
dd if=/dev/zero of=/tmp/test_main_remap.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare_remap.img bs=1M count=100 2>/dev/null
echo "✓ Images created"
echo ""

# Setup loop devices
echo "Setting up loop devices..."
sudo losetup /dev/loop18 /tmp/test_main_remap.img
sudo losetup /dev/loop19 /tmp/test_spare_remap.img
echo "✓ Loop devices ready"
echo ""

# Create dm-linear with ONE error at sector 1000
echo "Creating dm-linear + dm-error for main device..."
echo "0 1000 linear /dev/loop18 0" > /tmp/dm-table-main.txt
echo "1000 8 error" >> /tmp/dm-table-main.txt
echo "1008 203792 linear /dev/loop18 1008" >> /tmp/dm-table-main.txt
sudo dmsetup create dm-main-errors < /tmp/dm-table-main.txt
echo "✓ Created dm-main-errors with error at sector 1000"
echo ""

# Create dm-remap device
echo "Creating dm-remap device..."
echo "0 204800 dm-remap-v4 /dev/mapper/dm-main-errors /dev/loop19" | sudo dmsetup create testdev_remap
echo "✓ Device created"
echo ""

# Trigger error to create ONE remap
echo "Reading sector 1000 to trigger remap..."
sudo dd if=/dev/mapper/testdev_remap bs=512 skip=1000 count=1 of=/dev/null 2>&1 | grep -E "copied|error" || true
echo "✓ Read attempted"
echo ""

# Wait for remap processing
echo "Waiting 3 seconds for remap processing..."
sleep 3
echo ""

# Check status
echo "Checking device status..."
sudo dmsetup status testdev_remap
echo ""

# Check kernel log for remap
echo "Checking kernel log for remap creation..."
sudo dmesg | grep -i "remap" | tail -5
echo ""

# NOW TRY TO REMOVE - this is where crashes happen
echo "================================================================"
echo "CRITICAL: About to remove device with ONE remap in it"
echo "================================================================"
echo ""

sleep 1

echo "Removing device..."
sudo dmsetup remove testdev_remap
echo "✓ Device removed successfully!"
echo ""

# Cleanup
echo "Cleaning up..."
sudo dmsetup remove dm-main-errors
sudo losetup -d /dev/loop18
sudo losetup -d /dev/loop19
rm -f /tmp/test_main_remap.img /tmp/test_spare_remap.img /tmp/dm-table-main.txt
echo "✓ Cleanup complete"
echo ""

echo "================================================================"
echo "                  TEST PASSED!"
echo "================================================================"
