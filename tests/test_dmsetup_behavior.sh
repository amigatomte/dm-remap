#!/bin/bash

# Test dmsetup message behavior with a simple linear target first
echo "=== Testing dmsetup message behavior ==="

# Create a simple linear target to test message behavior
dd if=/dev/zero of=/tmp/test_linear.img bs=1M count=10 2>/dev/null
LOOP=$(losetup -f --show /tmp/test_linear.img)
SECTORS=$(blockdev --getsz $LOOP)

echo "Creating linear target for testing..."
echo "0 $SECTORS linear $LOOP 0" | sudo dmsetup create test-linear

echo "Testing linear target message (should fail gracefully):"
LINEAR_RESULT=$(sudo dmsetup message test-linear 0 test 2>&1 || echo "EXPECTED_FAILURE")
echo "Linear result: '$LINEAR_RESULT'"

# Clean up linear test
sudo dmsetup remove test-linear
losetup -d $LOOP
rm -f /tmp/test_linear.img

echo ""
echo "=== Testing with our dm-remap target ==="

# Create our test target
dd if=/dev/zero of=/tmp/debug_main.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=/tmp/debug_spare.img bs=1M count=10 2>/dev/null

MAIN_LOOP=$(losetup -f --show /tmp/debug_main.img)
SPARE_LOOP=$(losetup -f --show /tmp/debug_spare.img)

# Load our module
cd /home/christian/kernel_dev/dm-remap/src
sudo insmod dm_remap.ko

SECTORS=$(blockdev --getsz $MAIN_LOOP)
echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create debug-test

echo "Testing ping command return code:"
if sudo dmsetup message debug-test 0 ping; then
    echo "Ping command succeeded (return code 0)"
else
    echo "Ping command failed (return code $?)"
fi

echo "Testing metadata_status command return code:"
if sudo dmsetup message debug-test 0 metadata_status; then
    echo "metadata_status command succeeded (return code 0)"
else
    echo "metadata_status command failed (return code $?)"
fi

echo "Testing unknown command return code:"
if sudo dmsetup message debug-test 0 unknown_command; then
    echo "Unknown command succeeded (return code 0)"
else
    echo "Unknown command failed (return code $?)"
fi

# Check kernel logs
echo ""
echo "=== Recent kernel logs ==="
sudo dmesg | tail -5

# Cleanup
sudo dmsetup remove debug-test
sudo losetup -d $MAIN_LOOP $SPARE_LOOP
sudo rmmod dm_remap
rm -f /tmp/debug_main.img /tmp/debug_spare.img
