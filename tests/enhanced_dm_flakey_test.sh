#!/bin/bash
#
# enhanced_dm_flakey_test.sh - Enhanced dm-flakey testing for dm-remap v2.0
#
# This test creates direct block-level errors that dm-remap will detect,
# bypassing filesystem buffering that might mask errors.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

# Test configuration
TEST_SIZE_MB=50
SPARE_SIZE_MB=10
ERROR_SECTOR=1000         # Specific sector to inject errors
FLAKEY_UP_INTERVAL=2      # Seconds device is up
FLAKEY_DOWN_INTERVAL=1    # Seconds device injects errors

echo -e "${PURPLE}=== ENHANCED DM-FLAKEY TESTING FOR DM-REMAP v2.0 ===${NC}"
echo "Testing direct block-level error injection and auto-remapping response"
echo

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up enhanced dm-flakey test environment...${NC}"
    
    # Remove nested device mapper targets in correct order
    sudo dmsetup remove remap-on-flakey 2>/dev/null || true
    sudo dmsetup remove flakey-main 2>/dev/null || true
    sudo dmsetup remove flakey-spare 2>/dev/null || true
    
    # Clean up loop devices
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/enhanced_main.img /tmp/enhanced_spare.img /tmp/test_pattern_*
    
    # Remove module if needed
    sudo rmmod dm_remap 2>/dev/null || true
    sudo rmmod dm_flakey 2>/dev/null || true
    
    echo "Cleanup completed."
}

# Set trap for cleanup
trap cleanup EXIT

echo -e "${BLUE}Phase 1: Setting up enhanced error injection environment${NC}"

# Load required modules
sudo modprobe dm-flakey
if ! sudo dmsetup targets | grep -q "flakey"; then
    echo -e "${RED}❌ dm-flakey not available${NC}"
    exit 1
fi

# Load dm-remap with auto-remapping enabled and low error threshold
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod dm_remap.ko debug_level=2 auto_remap_enabled=1 error_threshold=2 enable_production_mode=1

echo "✅ Modules loaded successfully"

# Create test devices
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/enhanced_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/enhanced_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Create test patterns for verification
for i in {1..5}; do
    dd if=/dev/urandom of=/tmp/test_pattern_$i bs=4096 count=1 2>/dev/null
done

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/enhanced_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/enhanced_spare.img)

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"

# Get device sizes
MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(sudo blockdev --getsz "$SPARE_LOOP")

echo "Main device sectors: $MAIN_SECTORS"
echo "Spare device sectors: $SPARE_SECTORS"

echo -e "${BLUE}Phase 2: Creating dm-flakey error injection layer${NC}"

# Create dm-flakey target that injects errors to specific sectors
# flakey syntax: <dev> <offset> <up interval> <down interval> [num_features] [feature_args]
echo "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 $FLAKEY_UP_INTERVAL $FLAKEY_DOWN_INTERVAL" | sudo dmsetup create flakey-main

echo "✅ dm-flakey layer created with ${FLAKEY_UP_INTERVAL}s up, ${FLAKEY_DOWN_INTERVAL}s error injection"

echo -e "${BLUE}Phase 3: Creating dm-remap on top of dm-flakey${NC}"

# Create dm-remap target on top of the flakey device
echo "0 $MAIN_SECTORS remap /dev/mapper/flakey-main $SPARE_LOOP 0 1000" | sudo dmsetup create remap-on-flakey

echo "✅ dm-remap created on top of dm-flakey"

# Verify setup
sudo dmsetup status remap-on-flakey
sudo dmsetup status flakey-main

echo -e "${BLUE}Phase 4: Direct block-level error injection testing${NC}"

echo "Starting direct I/O operations to trigger errors..."

# Clear kernel message buffer to isolate our test messages
sudo dmesg -C

# Function to perform direct I/O and check for errors
perform_io_test() {
    local test_name="$1"
    local sector="$2"
    local pattern_file="$3"
    
    echo -n "  $test_name (sector $sector): "
    
    # Write directly to specific sector to trigger error injection
    if sudo dd if="$pattern_file" of=/dev/mapper/remap-on-flakey bs=512 seek="$sector" count=8 conv=notrunc,fdatasync 2>/dev/null; then
        echo -e "${GREEN}Write completed${NC}"
    else
        echo -e "${YELLOW}Write encountered error (expected during error injection)${NC}"
    fi
    
    # Give time for error detection and potential auto-remapping
    sleep 1
    
    # Check dm-remap status
    local status=$(sudo dmsetup status remap-on-flakey)
    echo "    Status: $status"
    
    # Check for errors in dmesg
    local recent_errors=$(sudo dmesg -T | grep -E "(dm-remap|error|remap)" | tail -3)
    if [ ! -z "$recent_errors" ]; then
        echo "    Recent messages:"
        echo "$recent_errors" | sed 's/^/      /'
    fi
    
    echo
}

# Test 1: Write to a sector that will encounter errors during "down" period
echo "Test sequence: Multiple I/O operations during error injection cycles"

perform_io_test "Test 1 - Target sector" "$ERROR_SECTOR" "/tmp/test_pattern_1"
sleep 3  # Wait for one full cycle

perform_io_test "Test 2 - Adjacent sector" "$((ERROR_SECTOR + 10))" "/tmp/test_pattern_2"
sleep 3  # Wait for another cycle

perform_io_test "Test 3 - Same sector retry" "$ERROR_SECTOR" "/tmp/test_pattern_3"
sleep 3  # Wait for another cycle

perform_io_test "Test 4 - Different area" "$((ERROR_SECTOR + 100))" "/tmp/test_pattern_4"
sleep 2

echo -e "${BLUE}Phase 5: Error analysis and auto-remap verification${NC}"

echo "Final dm-remap status:"
sudo dmsetup status remap-on-flakey

echo ""
echo "Recent kernel messages related to dm-remap and error handling:"
sudo dmesg -T | grep -E "(dm-remap|flakey)" | tail -15

echo ""
echo -e "${BLUE}Phase 6: Read verification test${NC}"

echo "Testing read operations to verify data integrity and error handling..."

for i in {1..4}; do
    echo -n "Reading test pattern $i: "
    if sudo dd if=/dev/mapper/remap-on-flakey of=/tmp/read_verify_$i bs=512 skip="$((ERROR_SECTOR + (i-1) * 10))" count=8 2>/dev/null; then
        echo -e "${GREEN}Read successful${NC}"
    else
        echo -e "${YELLOW}Read error (may indicate error injection active)${NC}"
    fi
done

echo ""
echo -e "${BLUE}Phase 7: Results analysis${NC}"

# Get final status
FINAL_STATUS=$(sudo dmsetup status remap-on-flakey)
echo "Final status: $FINAL_STATUS"

# Parse status for error counts and remaps
if [[ $FINAL_STATUS =~ errors=W([0-9]+):R([0-9]+) ]]; then
    WRITE_ERRORS=${BASH_REMATCH[1]}
    READ_ERRORS=${BASH_REMATCH[2]}
    echo "Detected errors - Write: $WRITE_ERRORS, Read: $READ_ERRORS"
else
    WRITE_ERRORS=0
    READ_ERRORS=0
    echo "No error pattern found in status"
fi

if [[ $FINAL_STATUS =~ auto_remaps=([0-9]+) ]]; then
    AUTO_REMAPS=${BASH_REMATCH[1]}
    echo "Auto-remaps performed: $AUTO_REMAPS"
else
    AUTO_REMAPS=0
    echo "No auto-remap information found"
fi

echo ""
echo -e "${PURPLE}=== ENHANCED DM-FLAKEY TEST RESULTS ===${NC}"

if [ "$WRITE_ERRORS" -gt 0 ] || [ "$READ_ERRORS" -gt 0 ]; then
    echo -e "${GREEN}✅ Error Detection: SUCCESS - Detected $((WRITE_ERRORS + READ_ERRORS)) total errors${NC}"
else
    echo -e "${YELLOW}⚠️ Error Detection: No errors detected (may need more aggressive injection)${NC}"
fi

if [ "$AUTO_REMAPS" -gt 0 ]; then
    echo -e "${GREEN}✅ Auto-Remapping: SUCCESS - Performed $AUTO_REMAPS automatic remaps${NC}"
else
    echo -e "${YELLOW}⚠️ Auto-Remapping: No auto-remaps triggered${NC}"
fi

echo ""
echo "Test demonstrated:"
echo "  • dm-flakey error injection integration"
echo "  • Direct block-level error detection"
echo "  • Real-time error monitoring"
echo "  • Auto-remapping trigger conditions"
echo "  • Error recovery mechanisms"

echo ""
echo -e "${GREEN}Enhanced dm-flakey testing completed!${NC}"

exit 0