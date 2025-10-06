#!/bin/bash
#
# debug_data_integrity.sh - Debug version to understand the data integrity issue
#

set -e

echo "=== DEBUG DATA INTEGRITY TEST ==="

# Cleanup function
cleanup() {
    echo "Cleaning up debug test..."
    sudo dmsetup remove debug-integrity 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/debug_main.img /tmp/debug_spare.img /tmp/test_write /tmp/test_read
}

trap cleanup EXIT

echo "Phase 1: Setup"
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod dm_remap.ko debug_level=2

# Create small test devices
dd if=/dev/zero of=/tmp/debug_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=/tmp/debug_spare.img bs=1M count=2 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/debug_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/debug_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

# Create dm-remap
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create debug-integrity

echo "Phase 2: Basic write/read test"

# Create test data - simple string
echo -n "TEST_DATA_123" > /tmp/test_write
echo "Test data: $(cat /tmp/test_write)"
echo "Test data size: $(wc -c < /tmp/test_write) bytes"

# Write to sector 100
echo "Writing to sector 100..."
sudo dd if=/tmp/test_write of=/dev/mapper/debug-integrity bs=512 seek=100 count=1 conv=notrunc 2>/dev/null

# Read back from sector 100  
echo "Reading from sector 100..."
sudo dd if=/dev/mapper/debug-integrity of=/tmp/test_read bs=512 skip=100 count=1 2>/dev/null

echo "Written data: '$(cat /tmp/test_write)'"
echo "Read data: '$(head -c 50 /tmp/test_read | tr -d '\0')'"

# Compare
if [ "$(cat /tmp/test_write)" = "$(head -c $(wc -c < /tmp/test_write) /tmp/test_read)" ]; then
    echo "✅ Data integrity: PASS"
else
    echo "❌ Data integrity: FAIL"
    echo "Hex dump of written:"
    hexdump -C /tmp/test_write
    echo "Hex dump of read:"
    hexdump -C /tmp/test_read | head -2
fi

echo "Debug test completed."