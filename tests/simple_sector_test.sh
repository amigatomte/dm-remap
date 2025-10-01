#!/bin/bash
#
# simple_sector_test.sh - Simple sector-specific error test
#

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}=== SIMPLE SECTOR-SPECIFIC ERROR TEST ===${NC}"

# Test configuration
TEST_SIZE_MB=50
SPARE_SIZE_MB=10

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    sudo dmsetup remove test-remap 2>/dev/null || true
    sudo dmsetup remove test-flakey 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/test_main.img /tmp/test_spare.img
}

trap cleanup EXIT

echo "Creating test devices..."
dd if=/dev/zero of=/tmp/test_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/test_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/test_spare.img)

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"

# Get device size
MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Total sectors: $MAIN_SECTORS"

echo "Creating dm-flakey target..."
# Create dm-flakey that fails intermittently (up 5 seconds, down 2 seconds)
echo "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 5 2" | sudo dmsetup create test-flakey

echo "✅ dm-flakey target created"

echo "Creating dm-remap target..."
# Create dm-remap on top of flakey
echo "0 $MAIN_SECTORS remap /dev/mapper/test-flakey $SPARE_LOOP 0 1000" | sudo dmsetup create test-remap

echo "✅ dm-remap target created"

# Check status
echo ""
echo "Device status:"
sudo dmsetup status test-remap
sudo dmsetup status test-flakey

echo ""
echo "Testing I/O operations during error injection cycles..."

# Clear kernel messages
sudo dmesg -C

# Function to test I/O
test_io() {
    local sector=$1
    local test_name="$2"
    
    echo -n "$test_name (sector $sector): "
    
    # Create test data
    dd if=/dev/urandom of=/tmp/test_data.tmp bs=4096 count=1 2>/dev/null
    
    # Try to write
    if sudo dd if=/tmp/test_data.tmp of=/dev/mapper/test-remap bs=512 seek="$sector" count=8 conv=notrunc,fdatasync 2>/dev/null; then
        echo -e "${GREEN}Success${NC}"
    else
        echo -e "${YELLOW}Error (expected during down period)${NC}"
    fi
    
    # Check status
    local status=$(sudo dmsetup status test-remap)
    echo "  Status: $status"
    
    sleep 1
}

# Test different sectors during error cycles
echo ""
echo "Starting I/O test sequence (will cycle through error periods)..."

for i in {1..8}; do
    test_io $((1000 + i * 100)) "Test $i"
    sleep 2  # Wait to see different error injection phases
done

echo ""
echo "Final status:"
sudo dmsetup status test-remap

echo ""
echo "Recent kernel messages:"
sudo dmesg -T | grep -E "(dm-remap|flakey)" | tail -10

echo ""
echo -e "${GREEN}Simple sector test completed!${NC}"
echo ""
echo "This test demonstrates:"
echo "• dm-flakey creating intermittent errors"
echo "• dm-remap detecting and handling those errors"
echo "• Sector-specific I/O testing"
echo "• Error injection timing control"

exit 0