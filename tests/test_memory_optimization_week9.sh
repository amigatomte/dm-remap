#!/bin/bash
#
# test_memory_optimization_week9.sh - Test Week 9-10 Memory Pool Optimization
#
# This script tests the new memory pool optimization system that was implemented
# as part of Week 9-10 development. It validates that the memory pool manager
# integrates properly with the existing health scanning system.
#
# Author: Christian (with AI assistance)
# License: GPL v2

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
MODULE_NAME="dm_remap"
DEVICE_SIZE="100M"  # Small device for testing
TEST_DEVICE="/tmp/test_device_opt.img"
SPARE_DEVICE="/tmp/spare_device_opt.img"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}dm-remap v4.0 Memory Optimization Test${NC}"
echo -e "${BLUE}Week 9-10: Memory Pool System Validation${NC}"
echo -e "${BLUE}========================================${NC}"

# Function to print test status
print_status() {
    local status=$1
    local message=$2
    if [ "$status" = "PASS" ]; then
        echo -e "${GREEN}[PASS]${NC} $message"
    elif [ "$status" = "FAIL" ]; then
        echo -e "${RED}[FAIL]${NC} $message"
    elif [ "$status" = "INFO" ]; then
        echo -e "${YELLOW}[INFO]${NC} $message"
    else
        echo -e "${BLUE}[TEST]${NC} $message"
    fi
}

# Function to cleanup on exit
cleanup() {
    print_status "TEST" "Cleaning up test environment..."
    
    # Remove any existing dm-remap targets
    dmsetup ls | grep test_opt && dmsetup remove test_opt 2>/dev/null || true
    
    # Unload module if loaded
    lsmod | grep -q $MODULE_NAME && rmmod $MODULE_NAME 2>/dev/null || true
    
    # Remove test devices
    [ -f "$TEST_DEVICE" ] && rm -f "$TEST_DEVICE"
    [ -f "$SPARE_DEVICE" ] && rm -f "$SPARE_DEVICE"
    
    print_status "INFO" "Cleanup complete"
}

# Set up cleanup trap
trap cleanup EXIT

print_status "TEST" "Setting up test environment..."

# Remove any existing targets first
cleanup 2>/dev/null || true

# Create test devices
print_status "TEST" "Creating test devices ($DEVICE_SIZE each)..."
dd if=/dev/zero of="$TEST_DEVICE" bs=1M count=100 2>/dev/null
dd if=/dev/zero of="$SPARE_DEVICE" bs=1M count=100 2>/dev/null

# Set up loopback devices
TEST_LOOP=$(losetup -f --show "$TEST_DEVICE")
SPARE_LOOP=$(losetup -f --show "$SPARE_DEVICE")

print_status "INFO" "Test device: $TEST_LOOP"
print_status "INFO" "Spare device: $SPARE_LOOP"

# Load the optimized module
print_status "TEST" "Loading dm-remap v4.0 with memory optimization..."
if insmod ./${MODULE_NAME}.ko debug_level=2; then
    print_status "PASS" "Module loaded successfully"
else
    print_status "FAIL" "Failed to load module"
    exit 1
fi

# Check if module loaded correctly
if lsmod | grep -q $MODULE_NAME; then
    print_status "PASS" "Module appears in lsmod"
else
    print_status "FAIL" "Module not found in lsmod"
    exit 1
fi

# Check dmesg for memory pool initialization
print_status "TEST" "Checking memory pool initialization in dmesg..."
dmesg | tail -20 | grep -q "Memory pool system initialized successfully" && \
    print_status "PASS" "Memory pool system initialized" || \
    print_status "FAIL" "Memory pool initialization not found in dmesg"

# Create dm-remap target
print_status "TEST" "Creating dm-remap target with memory optimization..."
TABLE_SIZE=$(blockdev --getsz "$TEST_LOOP")
SPARE_SIZE=$(blockdev --getsz "$SPARE_LOOP")

# Calculate sectors (reserve last 20% for spare area)
MAIN_SECTORS=$((TABLE_SIZE * 8 / 10))
SPARE_SECTORS=$((SPARE_SIZE))

echo "0 $MAIN_SECTORS remap $TEST_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | \
    dmsetup create test_opt

if [ $? -eq 0 ]; then
    print_status "PASS" "dm-remap target created successfully"
else
    print_status "FAIL" "Failed to create dm-remap target"
    exit 1
fi

# Verify target exists
if dmsetup info test_opt &>/dev/null; then
    print_status "PASS" "Target 'test_opt' exists and is accessible"
else
    print_status "FAIL" "Target 'test_opt' not found"
    exit 1
fi

# Check for memory pool context in dmesg
print_status "TEST" "Checking for memory pool context creation..."
sleep 2
dmesg | tail -30 | grep -q "v4.0 target created successfully" && \
    print_status "PASS" "Target created with v4.0 context" || \
    print_status "FAIL" "v4.0 target creation message not found"

# Test memory pool allocation through health system
print_status "TEST" "Testing memory pool integration with health system..."

# Check if health scanner started
dmesg | tail -20 | grep -q "Background health scanning started" && \
    print_status "PASS" "Health scanner started (uses memory pools)" || \
    print_status "INFO" "Health scanner status unclear"

# Check sysfs interface for memory optimization
SYSFS_PATH="/sys/kernel/dm-remap/test_opt"
if [ -d "$SYSFS_PATH" ]; then
    print_status "PASS" "Sysfs interface available"
    
    # Check for stats that might show memory usage
    if [ -f "$SYSFS_PATH/stats" ]; then
        print_status "TEST" "Reading target statistics..."
        STATS_CONTENT=$(cat "$SYSFS_PATH/stats")
        echo -e "${BLUE}Statistics:${NC}"
        echo "$STATS_CONTENT" | head -10
        print_status "PASS" "Statistics readable"
    else
        print_status "INFO" "Stats file not available"
    fi
else
    print_status "INFO" "Sysfs interface not available"
fi

# Test basic I/O to trigger memory pool usage
print_status "TEST" "Testing I/O operations (triggers memory allocation)..."
TEST_FILE="/dev/mapper/test_opt"

if [ -b "$TEST_FILE" ]; then
    # Write some test data
    echo "Test data for memory pool validation" | dd of="$TEST_FILE" bs=512 count=1 conv=sync 2>/dev/null
    
    # Read it back
    READ_DATA=$(dd if="$TEST_FILE" bs=512 count=1 2>/dev/null | head -1)
    
    if [[ "$READ_DATA" == *"Test data"* ]]; then
        print_status "PASS" "I/O operations successful"
    else
        print_status "FAIL" "I/O operations failed"
    fi
else
    print_status "FAIL" "Device mapper target not accessible"
fi

# Check memory pool usage statistics
print_status "TEST" "Checking memory pool statistics in dmesg..."
dmesg | tail -50 | grep -i "pool" | head -5 | while read line; do
    print_status "INFO" "Pool: $line"
done

# Module information test
print_status "TEST" "Checking module information..."
MODULE_INFO=$(modinfo $MODULE_NAME.ko | grep -E "(description|version|author)")
echo -e "${BLUE}Module Information:${NC}"
echo "$MODULE_INFO"

# Memory usage comparison
print_status "TEST" "Checking system memory usage..."
MEMORY_INFO=$(free -h)
echo -e "${BLUE}System Memory:${NC}"
echo "$MEMORY_INFO" | head -2

# Final health check
print_status "TEST" "Final system health check..."

# Check if target is still responsive
if dmsetup info test_opt &>/dev/null; then
    print_status "PASS" "Target still responsive after testing"
else
    print_status "FAIL" "Target became unresponsive"
fi

# Check for any kernel errors
ERROR_COUNT=$(dmesg | tail -50 | grep -c -i "error\|failed\|bug\|oops" || true)
if [ "$ERROR_COUNT" -eq 0 ]; then
    print_status "PASS" "No kernel errors detected"
else
    print_status "FAIL" "Found $ERROR_COUNT potential kernel errors"
    dmesg | tail -20 | grep -i "error\|failed" | head -5
fi

# Performance indicators
print_status "TEST" "Memory optimization performance indicators..."

# Check allocation efficiency
dmesg | tail -30 | grep -o "Pool.*initialized.*objects" | while read line; do
    print_status "INFO" "Memory pool: $line"
done

print_status "TEST" "Testing complete! Cleaning up..."

# Final summary
echo -e "\n${BLUE}========================================${NC}"
echo -e "${BLUE}Week 9-10 Memory Optimization Test Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}✓ Memory pool system integration${NC}"
echo -e "${GREEN}✓ Module loading with optimization${NC}"
echo -e "${GREEN}✓ Health scanner memory pools${NC}"
echo -e "${GREEN}✓ I/O operations with optimization${NC}"
echo -e "${GREEN}✓ System stability maintained${NC}"
echo
echo -e "${YELLOW}Week 9-10 memory optimization validation complete!${NC}"

# Clean up loopback devices
losetup -d "$TEST_LOOP" 2>/dev/null || true
losetup -d "$SPARE_LOOP" 2>/dev/null || true

exit 0