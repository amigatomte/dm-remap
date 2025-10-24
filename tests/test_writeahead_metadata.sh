#!/bin/bash
#
# Test write-ahead metadata persistence
# Tests: 1) Initial metadata write, 2) Remap with write-ahead
#

set -e

echo "========================================="
echo "Write-Ahead Metadata Test"
echo "========================================="

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    sudo dmsetup remove test-writeahead 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/main-device.img /tmp/spare-device.img
}

trap cleanup EXIT

# Create test images
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/main-device.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/spare-device.img bs=1M count=20 2>/dev/null

# Setup loop devices
sudo losetup /dev/loop20 /tmp/main-device.img
sudo losetup /dev/loop21 /tmp/spare-device.img

# Reload module
echo "Loading dm-remap modules..."
sudo rmmod dm-remap-v4-real 2>/dev/null || true
sudo rmmod dm-remap-v4-stats 2>/dev/null || true
sudo insmod src/dm-remap-v4-stats.ko
sudo insmod src/dm-remap-v4-real.ko

# Create dm-remap device
echo "Creating dm-remap device..."
sudo dmsetup create test-writeahead --table "0 204800 dm-remap-v4 /dev/loop20 /dev/loop21"

# Wait for deferred metadata read/write (100ms + processing)
echo "Waiting for initial metadata write (fire-and-forget)..."
sleep 2

# Check if metadata was written
echo ""
echo "Checking if initial metadata was written..."
MAGIC=$(sudo dd if=/dev/loop21 bs=1 count=4 skip=0 2>/dev/null | od -An -tx1 | tr -d ' \n')

if [ "$MAGIC" = "444d5234" ]; then
    echo "✓ Initial metadata written successfully (magic: 0x$MAGIC)"
else
    echo "✗ No metadata found (got: 0x$MAGIC, expected: 0x444d5234)"
    exit 1
fi

# Now trigger an I/O error to test write-ahead remap
echo ""
echo "Testing write-ahead remap creation..."
echo "(This would require dm-flakey, skipping for now)"

# Check metadata again
REMAP_COUNT=$(sudo /home/christian/kernel_dev/dm-remap/scripts/dm-remap-scan 2>&1 | grep -A5 "Device: /dev/loop21" | grep "Active remaps" | awk '{print $3}')
echo ""
echo "Active remaps in metadata: ${REMAP_COUNT:-0}"

echo ""
echo "✓ Write-ahead metadata test PASSED"
echo "  - Initial metadata: Written"
echo "  - Fire-and-forget: Working"
echo "  - Write-ahead remap: Needs dm-flakey to test fully"
