#!/bin/bash
#
# Simple test: Create device and immediately remove it
# No I/O, no remaps, just create and destroy
#

set -e

echo "Simple Create/Remove Test"
echo "=========================="
echo ""

# Load modules
echo "Loading modules..."
sudo modprobe dm-mod
sudo insmod src/dm-remap-v4-stats.ko 2>/dev/null || echo "Stats module already loaded or not needed"
sudo insmod src/dm-remap-v4-real.ko
echo "✓ Modules loaded"
echo ""

# Create test images
echo "Creating test images..."
dd if=/dev/zero of=/tmp/test_main_simple.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare_simple.img bs=1M count=100 2>/dev/null
echo "✓ Images created"
echo ""

# Setup loop devices
echo "Setting up loop devices..."
sudo losetup -d /dev/loop20 2>/dev/null || true
sudo losetup -d /dev/loop21 2>/dev/null || true
sudo losetup /dev/loop20 /tmp/test_main_simple.img
sudo losetup /dev/loop21 /tmp/test_spare_simple.img
echo "✓ Loop devices ready"
echo ""

# Create dm-remap device
echo "Creating dm-remap device..."
echo "0 204800 dm-remap-v4 /dev/loop20 /dev/loop21" | sudo dmsetup create testdev_simple
echo "✓ Device created"
echo ""

# Check status
echo "Checking device status..."
sudo dmsetup status testdev_simple
echo ""

# Create a manual remap using dmsetup message
echo "Creating a manual remap entry..."
sudo dmsetup message testdev_simple 0 add_remap 1000 1
echo "✓ Remap created: sector 1000 -> spare sector 1"
echo ""

# Check status again
echo "Checking device status after remap..."
sudo dmsetup status testdev_simple
echo ""

# Small delay
echo "Waiting 2 seconds..."
sleep 2
echo ""

# Remove device WITHOUT suspending first
echo "Removing device (no suspend)..."
sudo dmsetup remove testdev_simple
echo "✓ Device removed successfully!"
echo ""

# Cleanup
echo "Cleaning up..."
sudo losetup -d /dev/loop20
sudo losetup -d /dev/loop21
rm -f /tmp/test_main_simple.img /tmp/test_spare_simple.img
echo "✓ Cleanup complete"
echo ""

echo "================================================================"
echo "                  TEST PASSED!"
echo "================================================================"
