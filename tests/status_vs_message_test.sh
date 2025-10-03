#!/bin/bash

# Test dmsetup status vs dmsetup message behavior
echo "=== Testing dmsetup status vs message behavior ==="

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

# Create target
TARGET_NAME="test-status"
SECTORS=2097152
echo "Creating target: 0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 1000"
if echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 1000" | sudo dmsetup create $TARGET_NAME; then
    echo "✓ Target created successfully"
    
    echo ""
    echo "=== Testing dmsetup status (should return data) ==="
    echo "dmsetup status $TARGET_NAME:"
    STATUS_OUTPUT=$(sudo dmsetup status $TARGET_NAME 2>&1)
    echo "Result: '$STATUS_OUTPUT'"
    echo "Length: ${#STATUS_OUTPUT}"
    
    if [[ "$STATUS_OUTPUT" =~ "remap" ]]; then
        echo "✓ Status contains target type"
    fi
    
    if [[ "$STATUS_OUTPUT" =~ "v2.0" ]]; then
        echo "✓ Status contains version info"
    fi
    
    echo ""
    echo "=== Testing dmsetup message (sends commands, no output expected) ==="
    echo "dmsetup message $TARGET_NAME 0 metadata_status:"
    MESSAGE_OUTPUT=$(sudo dmsetup message $TARGET_NAME 0 metadata_status 2>&1)
    echo "Result: '$MESSAGE_OUTPUT'"
    echo "Length: ${#MESSAGE_OUTPUT}"
    
    if [ ${#MESSAGE_OUTPUT} -eq 0 ]; then
        echo "✓ Message returns empty output (as expected)"
    else
        echo "? Message returned unexpected output"
    fi
    
    echo ""
    echo "=== Checking kernel logs for message processing ==="
    dmesg | tail -5 | grep "dm-remap"
    
    # Cleanup target
    sudo dmsetup remove $TARGET_NAME
else
    echo "✗ Failed to create target"
fi

# Cleanup loop devices
sudo losetup -d $MAIN_LOOP
sudo losetup -d $SPARE_LOOP
rm -f /tmp/test_main.img /tmp/test_spare.img

echo ""
echo "=== Analysis complete ==="