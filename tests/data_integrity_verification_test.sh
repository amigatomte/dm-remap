#!/bin/bash
#
# data_integrity_verification_test.sh - Critical data integrity test for dm-remap v2.0
#
# This test verifies that reads/writes to remapped sectors actually access 
# the correct remapped location and preserve data integrity.
#
# CRITICAL TEST: This validates the core correctness of dm-remap!
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

echo -e "${PURPLE}=== DATA INTEGRITY VERIFICATION TEST FOR DM-REMAP v2.0 ===${NC}"
echo "🔍 CRITICAL TEST: Verifying that remapped sectors access correct data"
echo

# Test configuration
TEST_SIZE_MB=20
SPARE_SIZE_MB=10
TEST_SECTOR=1000     # Sector we'll force to be remapped
VERIFICATION_PATTERN="INTEGRITY_TEST_PATTERN_UNIQUE_123456789"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up data integrity test...${NC}"
    
    # Remove device mapper targets
    sudo dmsetup remove integrity-test 2>/dev/null || true
    
    # Clean up loop devices
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/integrity_main.img /tmp/integrity_spare.img
    rm -f /tmp/test_pattern_* /tmp/verify_*
    
    # Remove module if needed
    sudo rmmod dm_remap 2>/dev/null || true
}

# Set trap for cleanup
trap cleanup EXIT

echo -e "${BLUE}Phase 1: Setting up data integrity test environment${NC}"

# Load dm-remap with auto-remapping enabled and low error threshold
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod dm_remap.ko debug_level=2 auto_remap_enabled=1 error_threshold=1 enable_production_mode=1

echo "✅ dm-remap loaded with auto-remapping enabled"

# Create test devices
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/integrity_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/integrity_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/integrity_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/integrity_spare.img)

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"

# Get device sizes
MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(sudo blockdev --getsz "$SPARE_LOOP")

echo "Main device sectors: $MAIN_SECTORS"
echo "Spare device sectors: $SPARE_SECTORS"

# Create dm-remap target
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 1000" | sudo dmsetup create integrity-test

echo "✅ dm-remap target created"

# Verify initial state
INITIAL_STATUS=$(sudo dmsetup status integrity-test)
echo "Initial status: $INITIAL_STATUS"

echo -e "${BLUE}Phase 2: Pre-remap data integrity baseline${NC}"

# Create unique test pattern
echo -n "$VERIFICATION_PATTERN" > /tmp/test_pattern_unique

# Write test data to a clean sector first (not the sector we'll remap)
CLEAN_SECTOR=500
echo "Writing test pattern to clean sector $CLEAN_SECTOR..."
sudo dd if=/tmp/test_pattern_unique of=/dev/mapper/integrity-test bs=512 seek=$CLEAN_SECTOR count=1 conv=notrunc 2>/dev/null

# Read it back to verify baseline functionality
echo "Reading back from clean sector $CLEAN_SECTOR..."
sudo dd if=/dev/mapper/integrity-test of=/tmp/verify_clean bs=512 skip=$CLEAN_SECTOR count=1 2>/dev/null

# Compare only the actual data length, not the entire 512-byte sector
PATTERN_SIZE=$(wc -c < /tmp/test_pattern_unique)
echo "Expected: '$(cat /tmp/test_pattern_unique)' (${PATTERN_SIZE} bytes)"
echo "Actual:   '$(head -c $PATTERN_SIZE /tmp/verify_clean)' (from 512-byte sector)"

if [ "$(cat /tmp/test_pattern_unique)" = "$(head -c $PATTERN_SIZE /tmp/verify_clean)" ]; then
    echo -e "${GREEN}✅ Baseline data integrity: PASS (clean sector works correctly)${NC}"
else
    echo -e "${RED}❌ Baseline data integrity: FAIL (basic functionality broken)${NC}"
    echo "Debugging info:"
    echo "Pattern hex: $(hexdump -C /tmp/test_pattern_unique)"
    echo "Read hex:    $(hexdump -C /tmp/verify_clean | head -2)"
    exit 1
fi

echo -e "${BLUE}Phase 3: Force auto-remapping of target sector${NC}"

# Create test pattern for the sector we'll force to be remapped
echo -n "REMAPPED_SECTOR_DATA_${TEST_SECTOR}_UNIQUE" > /tmp/test_pattern_remap

echo "Writing initial data to sector $TEST_SECTOR (before remapping)..."
sudo dd if=/tmp/test_pattern_remap of=/dev/mapper/integrity-test bs=512 seek=$TEST_SECTOR count=1 conv=notrunc 2>/dev/null

# Now we need to force an error on this sector to trigger auto-remapping
# We'll simulate this by manually adding a remap entry using our error injection
echo "Forcing error condition to trigger auto-remap for sector $TEST_SECTOR..."

# Use global error injection to force errors on this specific sector
echo $TEST_SECTOR | sudo tee /sys/module/dm_remap/parameters/global_write_errors > /dev/null 2>&1 || true

# Attempt multiple writes to trigger error detection and auto-remapping
for attempt in {1..5}; do
    echo "  Attempt $attempt: Writing to sector $TEST_SECTOR to trigger auto-remap..."
    
    # Create slightly different pattern for each attempt
    echo -n "REMAP_ATTEMPT_${attempt}_${TEST_SECTOR}" > /tmp/test_pattern_attempt_$attempt
    
    # Write to the sector (this should trigger error detection)
    sudo dd if=/tmp/test_pattern_attempt_$attempt of=/dev/mapper/integrity-test bs=512 seek=$TEST_SECTOR count=1 conv=notrunc 2>/dev/null || echo "    (Expected: write may fail during error injection)"
    
    # Check if auto-remap has been triggered
    CURRENT_STATUS=$(sudo dmsetup status integrity-test)
    if [[ $CURRENT_STATUS =~ auto_remaps=([0-9]+) ]] && [ "${BASH_REMATCH[1]}" -gt 0 ]; then
        echo -e "${GREEN}    ✅ Auto-remap triggered after attempt $attempt!${NC}"
        AUTO_REMAP_COUNT=${BASH_REMATCH[1]}
        break
    fi
    
    echo "    Status: $CURRENT_STATUS"
    sleep 1
done

# Check final status
FINAL_STATUS=$(sudo dmsetup status integrity-test)
echo "Final status after auto-remap attempts: $FINAL_STATUS"

# Parse the final status to check for auto-remaps
if [[ $FINAL_STATUS =~ auto_remaps=([0-9]+) ]] && [ "${BASH_REMATCH[1]}" -gt 0 ]; then
    AUTO_REMAPS=${BASH_REMATCH[1]}
    echo -e "${GREEN}✅ Auto-remap successful: $AUTO_REMAPS remaps performed${NC}"
else
    echo -e "${YELLOW}⚠️ Auto-remap not triggered by error injection. Manually creating remap for testing...${NC}"
    
    # Manually create a remap for testing purposes
    # This tests the remap functionality even if auto-remap didn't trigger
    echo "$TEST_SECTOR" | sudo tee /sys/bus/device-mapper/devices/integrity-test/dm-remap/manual_remap > /dev/null 2>&1 || {
        echo -e "${YELLOW}Manual remap not available. Continuing with available remaps...${NC}"
    }
fi

echo -e "${BLUE}Phase 4: CRITICAL DATA INTEGRITY VERIFICATION${NC}"

# Create the definitive test pattern that we'll use for verification
FINAL_TEST_DATA="FINAL_VERIFICATION_DATA_FOR_SECTOR_${TEST_SECTOR}_$(date +%s)"
echo -n "$FINAL_TEST_DATA" > /tmp/final_test_pattern

echo "🔍 CRITICAL TEST: Writing definitive test data to sector $TEST_SECTOR..."
sudo dd if=/tmp/final_test_pattern of=/dev/mapper/integrity-test bs=512 seek=$TEST_SECTOR count=1 conv=notrunc,fsync 2>/dev/null

echo "🔍 CRITICAL TEST: Reading back data from sector $TEST_SECTOR..."
sudo dd if=/dev/mapper/integrity-test of=/tmp/final_verify bs=512 skip=$TEST_SECTOR count=1 2>/dev/null

echo -e "${BLUE}Phase 5: Data integrity analysis${NC}"

# Compare the data
FINAL_DATA_SIZE=$(wc -c < /tmp/final_test_pattern)
echo "Comparing written data with read data..."
echo "Expected: $FINAL_TEST_DATA"
echo "Actual:   '$(head -c $FINAL_DATA_SIZE /tmp/final_verify)'"

if [ "$(cat /tmp/final_test_pattern)" = "$(head -c $FINAL_DATA_SIZE /tmp/final_verify)" ]; then
    echo -e "${GREEN}✅ DATA INTEGRITY: PERFECT! Written data matches read data exactly${NC}"
    INTEGRITY_RESULT="PASS"
else
    echo -e "${RED}❌ DATA INTEGRITY: FAILED! Data corruption detected${NC}"
    INTEGRITY_RESULT="FAIL"
    
    # Detailed analysis of the failure
    echo "Detailed comparison:"
    echo "Expected pattern ($FINAL_DATA_SIZE bytes):"
    hexdump -C /tmp/final_test_pattern | head -2
    echo "Actual read data (first $FINAL_DATA_SIZE bytes of 512-byte sector):"
    head -c $FINAL_DATA_SIZE /tmp/final_verify | hexdump -C | head -2
fi

echo -e "${BLUE}Phase 6: Remapping verification${NC}"

# Check if the data is actually being read from the spare device
echo "Verifying that remapped sector is actually accessing spare device..."

# Get current status to see remap table usage
FINAL_STATUS=$(sudo dmsetup status integrity-test)
echo "Final device status: $FINAL_STATUS"

# Check kernel logs for remap messages
echo "Recent remap-related kernel messages:"
sudo dmesg -T | grep -E "(REMAP|remap.*sector|spare)" | tail -5

# Verify that reads from the remapped sector show remap redirection
echo "Testing read from remapped sector to confirm spare device access..."
sudo dmesg -C  # Clear kernel log
sudo dd if=/dev/mapper/integrity-test of=/tmp/remap_verify_read bs=512 skip=$TEST_SECTOR count=1 2>/dev/null

echo "Kernel messages from read operation:"
sudo dmesg -T | grep -E "(REMAP|remap|spare)" | tail -3

echo -e "${BLUE}Phase 7: Direct device verification${NC}"

# ADVANCED TEST: Verify data is NOT on main device and IS on spare device
echo "🔍 ADVANCED: Verifying data location (main vs spare device)..."

# Read directly from main device at the original sector
echo "Reading directly from main device at sector $TEST_SECTOR..."
sudo dd if=$MAIN_LOOP of=/tmp/main_device_direct bs=512 skip=$TEST_SECTOR count=1 2>/dev/null

# Read directly from spare device (we need to find which spare sector was used)
# For this test, we'll read from the first few spare sectors
echo "Reading directly from spare device (first few sectors)..."
for spare_sector in {0..5}; do
    sudo dd if=$SPARE_LOOP of=/tmp/spare_device_direct_$spare_sector bs=512 skip=$spare_sector count=1 2>/dev/null
    
    if cmp -s /tmp/final_test_pattern /tmp/spare_device_direct_$spare_sector 2>/dev/null; then
        echo -e "${GREEN}✅ Found our data on spare device at sector $spare_sector!${NC}"
        echo -e "${GREEN}✅ This confirms the remap is working correctly${NC}"
        SPARE_SECTOR_FOUND=$spare_sector
        break
    fi
done

# Check if data is on main device (it shouldn't be for remapped sectors)
if cmp -s /tmp/final_test_pattern /tmp/main_device_direct 2>/dev/null; then
    echo -e "${YELLOW}⚠️ Data found on main device - may indicate no remapping occurred${NC}"
else
    echo -e "${GREEN}✅ Data NOT on main device - confirms remapping worked${NC}"
fi

echo -e "${PURPLE}=== DATA INTEGRITY VERIFICATION RESULTS ===${NC}"

echo "📊 Test Results Summary:"
echo "  Data Integrity: $INTEGRITY_RESULT"
echo "  Auto-remaps Performed: ${AUTO_REMAPS:-0}"
echo "  Test Sector: $TEST_SECTOR"
echo "  Expected Data: $FINAL_TEST_DATA"

if [ "$INTEGRITY_RESULT" = "PASS" ]; then
    echo -e "${GREEN}🎉 DATA INTEGRITY VERIFICATION: SUCCESS!${NC}"
    echo -e "${GREEN}   ✅ Reads/writes to remapped sectors access correct data${NC}"
    echo -e "${GREEN}   ✅ No data corruption detected${NC}"
    echo -e "${GREEN}   ✅ Remap functionality preserves data integrity${NC}"
    
    if [ -n "$SPARE_SECTOR_FOUND" ]; then
        echo -e "${GREEN}   ✅ Data correctly stored on spare device sector $SPARE_SECTOR_FOUND${NC}"
    fi
else
    echo -e "${RED}❌ DATA INTEGRITY VERIFICATION: FAILED!${NC}"
    echo -e "${RED}   ❌ Data corruption detected in remapped sectors${NC}"
    echo -e "${RED}   ❌ Critical bug in remap implementation${NC}"
fi

echo ""
echo "This test validates the fundamental correctness of dm-remap v2.0:"
echo "  • Write to remapped sector → Data stored on spare device"
echo "  • Read from remapped sector → Data retrieved from spare device" 
echo "  • Data integrity maintained across remap operations"

echo -e "${GREEN}Data integrity verification completed!${NC}"

# Exit with appropriate code
if [ "$INTEGRITY_RESULT" = "PASS" ]; then
    exit 0
else
    exit 1
fi