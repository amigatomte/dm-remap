#!/bin/bash
#
# Bio Error Status Validation Test  
#
# This test specifically focuses on validating that bio->bi_status errors
# are properly detected and handled in the dmr_bio_endio() callback.
#
# Key validation points:
# 1. bio->bi_status error codes are correctly identified  
# 2. Error statistics are properly updated
# 3. Sector health tracking responds to errors
# 4. Debug output shows error detection flow
#

set -e

# Test configuration
TEST_NAME="Bio Error Status Validation"
TEST_DIR="/tmp/dm_remap_bio_status_test"
LOOP_SIZE_MB=50

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== ${TEST_NAME} ===${NC}"
echo "Validating bio->bi_status error handling in dmr_bio_endio()"
echo

# Function to cleanup resources
cleanup() {
    echo -e "${YELLOW}Cleaning up test resources...${NC}"
    
    # Remove dm devices
    sudo dmsetup remove dm-remap-bio-test 2>/dev/null || true
    sudo dmsetup remove flakey-bio-test 2>/dev/null || true
    
    # Detach loop devices
    for loop in $(losetup -a | grep "${TEST_DIR}" | cut -d: -f1); do
        sudo losetup -d "$loop" 2>/dev/null || true
    done
    
    # Clean up test directory
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

# Create test environment
echo -e "${BLUE}Setting up focused bio error test...${NC}"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

# Create smaller loop device for focused testing
dd if=/dev/zero of=bio_test.img bs=1M count=$LOOP_SIZE_MB
dd if=/dev/zero of=spare_test.img bs=1M count=5

# Set up loop devices
MAIN_LOOP=$(sudo losetup -f --show bio_test.img)
SPARE_LOOP=$(sudo losetup -f --show spare_test.img) 

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"

# Ensure modules are loaded
sudo modprobe dm-flakey || true
sudo insmod /home/christian/kernel_dev/dm-remap/src/dm_remap.ko 2>/dev/null || true

# Set debug level to maximum
echo 3 | sudo tee /sys/module/dm_remap/parameters/debug_level >/dev/null
echo 1 | sudo tee /sys/module/dm_remap/parameters/error_threshold >/dev/null

# Get device parameters
MAIN_SECTORS=$(blockdev --getsz "$MAIN_LOOP")
SPARE_SECTORS=$(blockdev --getsz "$SPARE_LOOP")

echo "Main sectors: $MAIN_SECTORS, Spare sectors: $SPARE_SECTORS"

# Set up dm-flakey with very short intervals for immediate error testing
echo -e "${BLUE}Creating flakey device with immediate error injection...${NC}"
# up_interval=1, down_interval=3 means: 1 sec success, 3 sec errors
sudo dmsetup create flakey-bio-test --table "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 1 3"

# Create dm-remap device
echo -e "${BLUE}Creating dm-remap device for bio error testing...${NC}"
sudo dmsetup create dm-remap-bio-test --table "0 $MAIN_SECTORS remap /dev/mapper/flakey-bio-test $SPARE_LOOP 0 $SPARE_SECTORS"

# Wait for device setup
sleep 2

# Clear kernel log to focus on new messages
sudo dmesg -c >/dev/null

echo -e "${BLUE}Starting bio error status validation...${NC}"

# Function to perform targeted I/O and check error handling
test_bio_error_detection() {
    local test_round=$1
    
    echo "=== Bio Error Test Round $test_round ==="
    
    # Capture initial statistics
    local initial_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    local initial_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    
    echo "Initial errors - Write: $initial_write_errors, Read: $initial_read_errors"
    
    # Perform I/O that should trigger errors (during down interval)
    echo "Triggering I/O during error injection period..."
    
    # Wait for down interval to start (after 1 second up interval)
    sleep 2
    
    # Perform write operation that should fail
    echo "Performing write operation during error period..."
    echo "Error test data $test_round" | sudo dd of=/dev/mapper/dm-remap-bio-test bs=512 seek=$test_round count=1 conv=notrunc oflag=sync 2>&1 | grep -v "records" || {
        echo "Write operation failed as expected during error injection"
    }
    
    # Brief pause
    sleep 1
    
    # Perform read operation that should fail  
    echo "Performing read operation during error period..."
    sudo dd if=/dev/mapper/dm-remap-bio-test bs=512 skip=$test_round count=1 2>&1 >/dev/null | grep -v "records" || {
        echo "Read operation failed as expected during error injection"
    }
    
    # Wait a moment for bio endio callbacks to process
    sleep 2
    
    # Check if errors were detected
    local final_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    local final_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    
    echo "Final errors - Write: $final_write_errors, Read: $final_read_errors"
    
    # Check for bio endio activity in kernel log
    echo "Bio endio callback activity:"
    dmesg | tail -10 | grep -E "(dmr_bio_endio|I/O error|Bio tracking|error.*sector)" || echo "No bio endio activity detected"
    
    # Calculate error detection success
    local write_detected=$((final_write_errors - initial_write_errors))
    local read_detected=$((final_read_errors - initial_read_errors))
    
    echo "Error detection results: +$write_detected write errors, +$read_detected read errors"
    
    return $((write_detected + read_detected))
}

# Run multiple test rounds to validate consistent error detection
echo -e "${BLUE}Running bio error detection test rounds...${NC}"

total_errors_detected=0

for round in {1..3}; do
    echo
    test_bio_error_detection $round
    errors_this_round=$?
    total_errors_detected=$((total_errors_detected + errors_this_round))
    
    # Wait between rounds to let flakey device cycle
    echo "Waiting for next test round..."
    sleep 5
done

echo
echo -e "${BLUE}=== BIO ERROR STATUS VALIDATION RESULTS ===${NC}"

# Final analysis
echo "1. Total errors detected across all rounds: $total_errors_detected"

# Check bio tracking messages
bio_tracking_messages=$(dmesg | grep -c "Bio tracking enabled\|Tracking bio:" || echo "0")
echo "2. Bio tracking setup messages: $bio_tracking_messages"

# Check bio endio callback messages  
bio_endio_messages=$(dmesg | grep -c "dmr_bio_endio\|I/O error.*sector" || echo "0")
echo "3. Bio endio callback messages: $bio_endio_messages"

# Check sector health updates
health_updates=$(dmesg | grep -c "health update\|error count" || echo "0")
echo "4. Sector health update messages: $health_updates"

# Final validation
echo
if [[ $total_errors_detected -gt 0 ]]; then
    echo -e "${GREEN}✓ Bio error status detection is WORKING${NC}"
    echo "  Errors are being detected and statistics updated"
else
    echo -e "${RED}✗ Bio error status detection is NOT WORKING${NC}"
    echo "  No errors detected despite dm-flakey error injection"
fi

if [[ $bio_tracking_messages -gt 0 ]]; then
    echo -e "${GREEN}✓ Bio tracking setup is WORKING${NC}"
    echo "  Bio contexts are being created and tracked"
else
    echo -e "${RED}✗ Bio tracking setup is NOT WORKING${NC}"
    echo "  Bio contexts are not being set up properly"
fi

if [[ $bio_endio_messages -gt 0 ]]; then
    echo -e "${GREEN}✓ Bio endio callbacks are WORKING${NC}"
    echo "  dmr_bio_endio() function is being called"
else
    echo -e "${RED}✗ Bio endio callbacks are NOT WORKING${NC}"
    echo "  dmr_bio_endio() function is not being called"
fi

if [[ $health_updates -gt 0 ]]; then
    echo -e "${GREEN}✓ Sector health tracking is WORKING${NC}"
    echo "  Error statistics are being updated"
else
    echo -e "${YELLOW}⚠ Sector health tracking may need attention${NC}"
    echo "  Limited health update activity detected"
fi

# Show detailed kernel messages for analysis
echo
echo -e "${BLUE}=== DETAILED KERNEL MESSAGES ===${NC}"
dmesg | grep -E "(dm_remap|dmr_|Bio|I/O error|health|Auto-remap)" | tail -30

# Overall assessment
echo
if [[ $total_errors_detected -gt 0 && $bio_endio_messages -gt 0 ]]; then
    echo -e "${GREEN}=== BIO ERROR STATUS VALIDATION: PASSED ===${NC}"
    echo "Bio error detection pipeline is functioning correctly!"
else
    echo -e "${RED}=== BIO ERROR STATUS VALIDATION: FAILED ===${NC}"
    echo "Bio error detection pipeline needs debugging!"
fi

echo
echo -e "${BLUE}Bio error status validation completed.${NC}"