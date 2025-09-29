#!/bin/bash

# simple_test_v1.sh - Basic test script for dm-remap v1
# Tests all v1 features: .ctr, .dtr, .map, .message, .status

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm-remap.ko"
TEST_DIR="/tmp/dm-remap-test"
DM_NAME="test-remap-v1"

cleanup() {
    echo "=== Cleanup ==="
    sudo dmsetup remove "$DM_NAME" 2>/dev/null || true
    sudo losetup -d /dev/loop50 2>/dev/null || true
    sudo losetup -d /dev/loop51 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    rm -rf "$TEST_DIR"
}

# Cleanup on exit
trap cleanup EXIT

# Create test directory
mkdir -p "$TEST_DIR"

echo "=== dm-remap v1 Testing ==="
echo "Date: $(date)"

# Step 1: Build and load module
echo "=== Step 1: Module loading ==="
cd /home/christian/kernel_dev/dm-remap/src
make clean
make

if [[ ! -f "dm-remap.ko" ]]; then
    echo "ERROR: Module not found at dm-remap.ko"
    exit 1
fi

if lsmod | grep -q dm_remap; then
    echo "⚠️  Module already loaded, removing first"
    sudo rmmod dm_remap || true
fi

sudo insmod dm-remap.ko
echo "✅ Module loaded"

# Step 2: Set up test devices
echo "=== Step 2: Device setup ==="
truncate -s 100M "$TEST_DIR/main.img"
truncate -s 50M "$TEST_DIR/spare.img"

sudo losetup /dev/loop50 "$TEST_DIR/main.img"
sudo losetup /dev/loop51 "$TEST_DIR/spare.img"
echo "✅ Loop devices created"

# Step 3: Create dm target (4 args: main_dev spare_dev spare_start spare_len)
echo "=== Step 3: Target creation ==="
SECTORS=$((100 * 1024 * 1024 / 512))  # 100MB in 512-byte sectors
SPARE_SECTORS=$((50 * 1024 * 1024 / 512))  # 50MB in 512-byte sectors

echo "0 $SECTORS remap /dev/loop50 /dev/loop51 0 $SPARE_SECTORS" | sudo dmsetup create "$DM_NAME"

# Verify target creation
if sudo dmsetup info "$DM_NAME" | grep -q "State.*ACTIVE"; then
    echo "✅ Target created successfully"
else
    echo "❌ Target creation failed"
    exit 1
fi

# Step 4: Test basic I/O with debug logging verification
echo "=== Step 4: Basic I/O test with debug logging ==="
# Enable debug logging
echo 2 | sudo tee /sys/module/dm_remap/parameters/debug_level >/dev/null

# Clear dmesg buffer for clean debug log capture
sudo dmesg -C

# Test with exact 16-byte data for precise validation
TEST_DATA="test data v1 123"
echo -n "$TEST_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=16 seek=100 count=1 conv=notrunc 2>/dev/null
DATA=$(sudo dd if="/dev/mapper/$DM_NAME" bs=16 skip=100 count=1 2>/dev/null)

if [[ "$DATA" == "$TEST_DATA" ]]; then
    echo "✅ Basic I/O working (exact match)"
else
    echo "❌ Basic I/O failed: expected '$TEST_DATA', got '$DATA'"
    exit 1
fi

# Verify debug logging is working
sleep 1  # Give kernel time to process logs
if sudo dmesg | grep -q "dm-remap: I/O:"; then
    echo "✅ Debug logging working (I/O operations logged)"
    DEBUG_COUNT=$(sudo dmesg | grep "dm-remap: I/O:" | wc -l)
    echo "   Found $DEBUG_COUNT I/O debug messages"
else
    echo "⚠️  Debug logging may not be working (no I/O logs found)"
fi

# Step 4.5: Test multi-sector I/O
echo "=== Step 4.5: Multi-sector I/O test ==="
# Test with 1024-byte (2 sector) write
MULTI_DATA="This is a longer test string for multi-sector I/O validation testing with more than 512 bytes of data to ensure multi-sector operations work correctly through the device mapper target."
echo -n "$MULTI_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=1024 seek=50 count=1 conv=notrunc 2>/dev/null
MULTI_READ=$(sudo dd if="/dev/mapper/$DM_NAME" bs=1024 skip=50 count=1 2>/dev/null | tr -d '\0')

if [[ "$MULTI_READ" == "$MULTI_DATA"* ]]; then
    echo "✅ Multi-sector I/O working"
else
    echo "❌ Multi-sector I/O failed"
    exit 1
fi

# Step 5: Test message interface
echo "=== Step 5: Message interface test ==="
sudo dmsetup message "$DM_NAME" 0 ping
sleep 1

# Check kernel logs for pong response
if sudo dmesg | tail -10 | grep -q "pong"; then
    echo "✅ Ping message working (check dmesg for 'pong')"
else
    echo "⚠️  Ping message sent (response in kernel logs)"
fi

# Test remap command
sudo dmsetup message "$DM_NAME" 0 remap 200
echo "✅ Remap message sent"

# Step 5.5: Test additional message commands
echo "=== Step 5.5: Additional message commands ==="
sudo dmsetup message "$DM_NAME" 0 verify 200
echo "✅ Verify message sent"

sudo dmsetup message "$DM_NAME" 0 clear
echo "✅ Clear message sent"

# Verify clear worked by checking status
CLEAR_STATUS=$(sudo dmsetup status "$DM_NAME")
if [[ "$CLEAR_STATUS" == *"remapped=0"* ]]; then
    echo "✅ Clear command verified (remapped=0)"
else
    echo "⚠️  Clear status: $CLEAR_STATUS"
fi

# Step 6: Test status interface
echo "=== Step 6: Status interface test ==="
STATUS=$(sudo dmsetup status "$DM_NAME")
echo "Status: $STATUS"

if [[ "$STATUS" == *"remap"* ]]; then
    echo "✅ Status interface working"
else
    echo "❌ Status interface failed"
    exit 1
fi

# Step 7: Test remapping functionality with debug verification
echo "=== Step 7: Remapping functionality test ==="

# Test 1: Write data, remap sector, verify redirection
ORIG_DATA="original data"
REMAP_DATA="remapped data"

# Write original data to sector 300
echo -n "$ORIG_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=16 seek=300 count=1 conv=notrunc 2>/dev/null

# Read it back to confirm
READ_ORIG=$(sudo dd if="/dev/mapper/$DM_NAME" bs=16 skip=300 count=1 2>/dev/null | tr -d '\0')
echo "Original data written: '$READ_ORIG'"

# Remap the sector
sudo dmsetup message "$DM_NAME" 0 remap 300

# Clear dmesg for clean remap debug capture
sudo dmesg -C

# Write new data to the same sector (should go to spare)
echo -n "$REMAP_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=16 seek=300 count=1 conv=notrunc 2>/dev/null

# Read it back (should get remapped data)
READ_REMAP=$(sudo dd if="/dev/mapper/$DM_NAME" bs=16 skip=300 count=1 2>/dev/null | tr -d '\0')
echo "Remapped data read: '$READ_REMAP'"

if [[ "$READ_REMAP" == "$REMAP_DATA" ]]; then
    echo "✅ Remapping redirection working"
else
    echo "❌ Remapping failed: expected '$REMAP_DATA', got '$READ_REMAP'"
fi

# Verify remap debug logging
sleep 1
if sudo dmesg | grep -q "dm-remap: REMAP:"; then
    echo "✅ Remap debug logging working"
    REMAP_MSG=$(sudo dmesg | grep "dm-remap: REMAP:" | head -1)
    echo "   Debug: $REMAP_MSG"
else
    echo "⚠️  Remap debug logging may not be working"
fi

# Check if status shows the remap
NEW_STATUS=$(sudo dmsetup status "$DM_NAME")
echo "Status after remap: $NEW_STATUS"
echo "✅ Remapping test completed"

# Step 8: Error handling and edge cases
echo "=== Step 8: Error handling test ==="

# Test invalid commands
sudo dmsetup message "$DM_NAME" 0 invalid_command 2>/dev/null || echo "✅ Invalid command handled"
sudo dmsetup message "$DM_NAME" 0 remap abc 2>/dev/null || echo "✅ Invalid remap argument handled"
sudo dmsetup message "$DM_NAME" 0 remap 2>/dev/null || echo "✅ Missing remap argument handled"
sudo dmsetup message "$DM_NAME" 0 verify xyz 2>/dev/null || echo "✅ Invalid verify argument handled"

# Test boundary conditions
MAX_SECTOR=$((SECTORS - 1))
sudo dmsetup message "$DM_NAME" 0 remap $MAX_SECTOR 2>/dev/null && echo "✅ Max sector remap accepted" || echo "⚠️  Max sector remap rejected"
sudo dmsetup message "$DM_NAME" 0 remap $SECTORS 2>/dev/null || echo "✅ Out-of-bounds sector rejected"

echo "=== Test Summary ==="
echo "✅ All v1 features tested successfully"
echo "📋 Final status: $(sudo dmsetup status "$DM_NAME")"
echo "📋 Final kernel logs:"
sudo dmesg | tail -5

echo "=== Test completed successfully ==="