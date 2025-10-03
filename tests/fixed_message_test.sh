#!/bin/bash

# Fixed dmsetup message test with proper device setup
echo "=== Fixed dmsetup message test ==="

# Create backing files
truncate -s 1G /tmp/test_main.img
truncate -s 1G /tmp/test_spare.img

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/test_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/test_spare.img)
echo "Using main device: $MAIN_LOOP"
echo "Using spare device: $SPARE_LOOP"

# Load module
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod ../src/dm_remap.ko

# Create target with correct format (like test_recovery_v3.sh)
TARGET_NAME="test-message"
SECTORS=2097152
echo "Creating target: 0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 1000"
if echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 1000" | sudo dmsetup create $TARGET_NAME; then
    echo "✓ Target created successfully"
    
    echo ""
    echo "Testing metadata_status message:"
    
    # Test the message command
    echo "Direct command:"
    sudo dmsetup message $TARGET_NAME 0 metadata_status
    echo "Return code: $?"
    
    echo ""
    echo "Captured output:"
    OUTPUT=$(sudo dmsetup message $TARGET_NAME 0 metadata_status 2>&1)
    echo "Result: '$OUTPUT'"
    echo "Length: ${#OUTPUT}"
    
    echo ""
    echo "Test ping for comparison:"
    PING_OUTPUT=$(sudo dmsetup message $TARGET_NAME 0 ping 2>&1)
    echo "Ping result: '$PING_OUTPUT'"
    
    echo ""
    echo "Recent kernel messages:"
    dmesg | tail -8 | grep "dm-remap"
    
    # Cleanup target
    sudo dmsetup remove $TARGET_NAME
else
    echo "✗ Failed to create target"
    dmesg | tail -5 | grep "dm-remap"
fi

# Cleanup loop devices
sudo losetup -d $MAIN_LOOP
sudo losetup -d $SPARE_LOOP
rm -f /tmp/test_main.img /tmp/test_spare.img /tmp/metadata.json

echo ""
echo "=== Test complete ==="