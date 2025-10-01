#!/bin/bash
#
# Bio Endio Error Detection Validation Test
#
# This test specifically validates the bio endio callback pipeline:
# 1. dm-flakey injects I/O errors
# 2. dmr_bio_endio() detects the errors 
# 3. Error statistics are updated
# 4. Auto-remap logic is triggered when thresholds are met
# 5. Automatic remapping occurs in background worker
#
# Focus: End-to-end validation of the v2.0 error detection and auto-remap pipeline
#

set -e

# Test configuration
TEST_NAME="Bio Endio Error Detection Pipeline"
TEST_DIR="/tmp/dm_remap_endio_test"
LOOP_SIZE_MB=100
TEST_FILE_SIZE_MB=10
ERROR_INJECT_INTERVAL=5  # Inject errors every 5 seconds
SUCCESS_INTERVAL=10      # Allow success every 10 seconds

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== ${TEST_NAME} ===${NC}"
echo "Validating bio endio error detection and auto-remap pipeline"
echo

# Function to cleanup resources
cleanup() {
    echo -e "${YELLOW}Cleaning up test resources...${NC}"
    
    # Unmount filesystem if mounted
    if mountpoint -q "${TEST_DIR}/mount" 2>/dev/null; then
        sudo umount "${TEST_DIR}/mount" || true
    fi
    
    # Remove dm-remap device
    if dmsetup info dm-remap-test >/dev/null 2>&1; then
        sudo dmsetup remove dm-remap-test || true
    fi
    
    # Remove dm-flakey device  
    if dmsetup info flakey-test >/dev/null 2>&1; then
        sudo dmsetup remove flakey-test || true
    fi
    
    # Detach loop devices
    for loop in $(losetup -a | grep "${TEST_DIR}" | cut -d: -f1); do
        sudo losetup -d "$loop" || true
    done
    
    # Clean up test directory
    rm -rf "$TEST_DIR"
    
    echo -e "${GREEN}Cleanup completed${NC}"
}

# Set up cleanup trap
trap cleanup EXIT

# Create test environment
echo -e "${BLUE}Setting up test environment...${NC}"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

# Create loop device files
echo "Creating loop device backing files..."
dd if=/dev/zero of=main_device.img bs=1M count=$LOOP_SIZE_MB status=progress
dd if=/dev/zero of=spare_device.img bs=1M count=10 status=progress

# Set up loop devices
echo "Setting up loop devices..."
MAIN_LOOP=$(sudo losetup -f --show main_device.img)
SPARE_LOOP=$(sudo losetup -f --show spare_device.img) 

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"

# Load required modules
echo "Loading kernel modules..."
sudo modprobe dm-flakey || true
sudo insmod /home/christian/kernel_dev/dm-remap/src/dm_remap.ko

# Set debug level for detailed bio tracking output
echo "Setting debug level to 3 for comprehensive bio tracking..."
echo 3 | sudo tee /sys/module/dm_remap/parameters/debug_level >/dev/null

# Configure error threshold for faster auto-remap triggering
echo "Configuring low error threshold for testing..."
echo 2 | sudo tee /sys/module/dm_remap/parameters/error_threshold >/dev/null

# Set up dm-flakey device for controlled error injection
echo -e "${BLUE}Setting up dm-flakey for error injection...${NC}"
MAIN_SECTORS=$(blockdev --getsz "$MAIN_LOOP")
echo "Main device sectors: $MAIN_SECTORS"

# Create flakey device: up_interval down_interval device_path start_sector
sudo dmsetup create flakey-test --table "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 $SUCCESS_INTERVAL $ERROR_INJECT_INTERVAL"

# Create dm-remap device using the flakey device as source
echo -e "${BLUE}Creating dm-remap device with error injection...${NC}"
SPARE_SECTORS=$(blockdev --getsz "$SPARE_LOOP")
echo "Spare device sectors: $SPARE_SECTORS"

sudo dmsetup create dm-remap-test --table "0 $MAIN_SECTORS remap /dev/mapper/flakey-test 0 $SPARE_LOOP 0 $SPARE_SECTORS"

# Wait for device to be ready
sleep 2

# Verify device creation
if [[ ! -b /dev/mapper/dm-remap-test ]]; then
    echo -e "${RED}ERROR: dm-remap device not created${NC}"
    exit 1
fi

echo -e "${GREEN}dm-remap device created successfully${NC}"

# Function to monitor error statistics
monitor_errors() {
    local duration=$1
    local start_time=$(date +%s)
    local end_time=$((start_time + duration))
    
    echo -e "${BLUE}Monitoring error detection for ${duration} seconds...${NC}"
    
    while [[ $(date +%s) -lt $end_time ]]; do
        # Check kernel messages for bio endio callbacks
        echo "=== Bio Endio Activity $(date) ==="
        dmesg | tail -20 | grep -E "(Bio tracking|I/O error|Auto-remap|dmr_bio_endio)" || echo "No bio endio activity found"
        
        # Check dm-remap statistics
        echo "=== dm-remap Statistics ==="
        if [[ -d /sys/module/dm_remap/parameters ]]; then
            echo "Error threshold: $(cat /sys/module/dm_remap/parameters/error_threshold 2>/dev/null || echo 'N/A')"
            echo "Write errors: $(cat /sys/module/dm_remap/parameters/write_errors 2>/dev/null || echo 'N/A')"
            echo "Read errors: $(cat /sys/module/dm_remap/parameters/read_errors 2>/dev/null || echo 'N/A')"
            echo "Auto remaps: $(cat /sys/module/dm_remap/parameters/auto_remaps 2>/dev/null || echo 'N/A')"
        fi
        
        # Check device mapper status
        echo "=== Device Mapper Status ==="
        sudo dmsetup status dm-remap-test || echo "Status unavailable"
        
        echo
        sleep 3
    done
}

# Function to generate I/O that will trigger errors
generate_error_triggering_io() {
    echo -e "${BLUE}Generating I/O to trigger error detection...${NC}"
    
    # Direct I/O to bypass caches and force actual device I/O
    echo "Performing direct I/O writes to trigger errors..."
    
    # Write test data to specific sectors that should trigger errors
    for i in {1..10}; do
        echo "I/O round $i: Writing 4KB at offset $((i * 4096))"
        
        # Use dd with direct I/O to force immediate device access
        echo "Test data block $i" | sudo dd of=/dev/mapper/dm-remap-test bs=4096 seek=$i count=1 conv=notrunc iflag=fullblock oflag=direct,sync 2>/dev/null || {
            echo "Write $i encountered error (expected during down interval)"
        }
        
        # Brief pause between I/Os
        sleep 1
    done
    
    # Also try some read operations
    echo "Performing direct I/O reads to trigger errors..."
    for i in {1..5}; do
        echo "Read round $i: Reading 4KB at offset $((i * 4096))"
        
        sudo dd if=/dev/mapper/dm-remap-test bs=4096 skip=$i count=1 iflag=direct 2>/dev/null >/dev/null || {
            echo "Read $i encountered error (expected during down interval)"
        }
        
        sleep 1
    done
}

# Start background monitoring
echo -e "${BLUE}Starting error detection validation...${NC}"

# Clear dmesg to focus on new messages
sudo dmesg -c >/dev/null

# Start I/O generation and monitoring in parallel
{
    sleep 2  # Let monitoring start first
    generate_error_triggering_io
} &

# Monitor for errors and bio endio activity
monitor_errors 30

# Wait for background I/O to complete
wait

echo -e "${BLUE}Analyzing results...${NC}"

# Analyze the results
echo "=== Final Error Detection Analysis ==="

# Look for bio endio callbacks in kernel messages
echo "1. Bio Endio Callback Detection:"
bio_endio_count=$(dmesg | grep -c "dmr_bio_endio\|I/O error.*sector" || echo "0")
echo "   Bio endio callbacks detected: $bio_endio_count"

# Look for error statistics updates
echo "2. Error Statistics Updates:"
error_updates=$(dmesg | grep -c "Sector.*health update\|error count" || echo "0") 
echo "   Sector health updates: $error_updates"

# Look for auto-remap trigger events
echo "3. Auto-Remap Trigger Events:"
auto_remap_triggers=$(dmesg | grep -c "Auto-remap triggered\|should_auto_remap" || echo "0")
echo "   Auto-remap triggers: $auto_remap_triggers"

# Look for actual remapping operations
echo "4. Remapping Operations:"
actual_remaps=$(dmesg | grep -c "Successfully auto-remapped\|Auto-remapped sector" || echo "0")
echo "   Actual remappings: $actual_remaps"

# Check final statistics
echo "5. Final Statistics:"
if [[ -d /sys/module/dm_remap/parameters ]]; then
    final_write_errors=$(cat /sys/module/dm_remap/parameters/write_errors 2>/dev/null || echo "N/A")
    final_read_errors=$(cat /sys/module/dm_remap/parameters/read_errors 2>/dev/null || echo "N/A")
    final_auto_remaps=$(cat /sys/module/dm_remap/parameters/auto_remaps 2>/dev/null || echo "N/A")
    
    echo "   Final write errors: $final_write_errors"
    echo "   Final read errors: $final_read_errors"  
    echo "   Final auto remaps: $final_auto_remaps"
fi

# Validation results
echo -e "${BLUE}=== VALIDATION RESULTS ===${NC}"

validation_passed=true

if [[ $bio_endio_count -gt 0 ]]; then
    echo -e "${GREEN}✓ Bio endio callbacks are working${NC}"
else
    echo -e "${RED}✗ No bio endio callbacks detected${NC}"
    validation_passed=false
fi

if [[ $error_updates -gt 0 ]]; then
    echo -e "${GREEN}✓ Error statistics updates are working${NC}"
else
    echo -e "${RED}✗ No error statistics updates detected${NC}"
    validation_passed=false
fi

if [[ $final_write_errors != "N/A" && $final_write_errors -gt 0 ]] || [[ $final_read_errors != "N/A" && $final_read_errors -gt 0 ]]; then
    echo -e "${GREEN}✓ Error counting is working${NC}"
else
    echo -e "${YELLOW}⚠ Error counting may not be working properly${NC}"
fi

if [[ $auto_remap_triggers -gt 0 ]]; then
    echo -e "${GREEN}✓ Auto-remap logic is being triggered${NC}" 
else
    echo -e "${YELLOW}⚠ Auto-remap triggers not detected (may need more errors)${NC}"
fi

if [[ $actual_remaps -gt 0 ]]; then
    echo -e "${GREEN}✓ Actual remapping operations are working${NC}"
else
    echo -e "${YELLOW}⚠ No actual remaps completed (may need more sustained errors)${NC}"
fi

# Show relevant kernel messages
echo -e "${BLUE}=== RELEVANT KERNEL MESSAGES ===${NC}"
dmesg | grep -E "(dmr_|dm_remap|Bio tracking|I/O error|Auto-remap)" | tail -20

if $validation_passed; then
    echo -e "${GREEN}=== BIO ENDIO ERROR DETECTION VALIDATION: PASSED ===${NC}"
    echo "The bio endio callback pipeline is working correctly!"
else
    echo -e "${RED}=== BIO ENDIO ERROR DETECTION VALIDATION: ISSUES DETECTED ===${NC}"
    echo "Some components of the bio endio pipeline may need attention."
fi

echo
echo -e "${BLUE}Test completed. Check kernel messages above for detailed bio endio activity.${NC}"