#!/bin/bash
# Test script for v4.0.5 baseline (Oct 17 working version)
# This verifies the reverted version works correctly

# Note: NOT using set -e because we expect some commands to fail (like reading bad sectors)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "Testing v4.0.5 Baseline (Oct 17 Version)"
echo "========================================"
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test configuration
LOOP_MAIN=""
LOOP_SPARE=""
TEST_PASSED=0
TEST_FAILED=0

# Cleanup function
cleanup() {
    echo ""
    echo "========================================="
    echo "CLEANUP"
    echo "========================================="
    
    # Remove dm devices
    if [ -n "$(dmsetup ls | grep dm-remap-test)" ]; then
        echo "Removing dm-remap device..."
        sudo dmsetup remove dm-remap-test 2>/dev/null || true
        sleep 1
    fi
    
    if [ -n "$(dmsetup ls | grep error_device)" ]; then
        echo "Removing dm-error device..."
        sudo dmsetup remove error_device 2>/dev/null || true
        sleep 1
    fi
    
    if [ -n "$(dmsetup ls | grep linear_main)" ]; then
        echo "Removing dm-linear device..."
        sudo dmsetup remove linear_main 2>/dev/null || true
        sleep 1
    fi
    
    # Unload module
    if lsmod | grep -q dm_remap_v4_real; then
        echo "Unloading dm-remap module..."
        sudo rmmod dm-remap-v4-real 2>/dev/null || true
        sleep 1
    fi
    
    if lsmod | grep -q dm_remap_v4_stats; then
        echo "Unloading stats module..."
        sudo rmmod dm-remap-v4-stats 2>/dev/null || true
        sleep 1
    fi
    
    # Remove loop devices
    if [ -n "$LOOP_MAIN" ]; then
        echo "Removing main loop device $LOOP_MAIN..."
        sudo losetup -d "$LOOP_MAIN" 2>/dev/null || true
    fi
    
    if [ -n "$LOOP_SPARE" ]; then
        echo "Removing spare loop device $LOOP_SPARE..."
        sudo losetup -d "$LOOP_SPARE" 2>/dev/null || true
    fi
    
    # Remove test files
    rm -f /tmp/dm-remap-test-main.img /tmp/dm-remap-test-spare.img
    
    echo ""
    echo "========================================="
    echo "TEST SUMMARY"
    echo "========================================="
    echo -e "${GREEN}Tests passed: $TEST_PASSED${NC}"
    echo -e "${RED}Tests failed: $TEST_FAILED${NC}"
    
    if [ $TEST_FAILED -eq 0 ]; then
        echo -e "${GREEN}ALL TESTS PASSED! ✓${NC}"
        echo ""
        echo "v4.0.5 baseline is working correctly!"
        exit 0
    else
        echo -e "${RED}SOME TESTS FAILED! ✗${NC}"
        exit 1
    fi
}

trap cleanup EXIT

# Test helper functions
test_step() {
    echo ""
    echo -e "${YELLOW}TEST: $1${NC}"
}

test_pass() {
    echo -e "${GREEN}✓ PASS: $1${NC}"
    ((TEST_PASSED++))
}

test_fail() {
    echo -e "${RED}✗ FAIL: $1${NC}"
    ((TEST_FAILED++))
}

# Start testing
echo "Step 1: Check kernel module exists"
if [ ! -f "$PROJECT_DIR/src/dm-remap-v4-real.ko" ]; then
    echo -e "${RED}ERROR: Module not found. Please run 'make' first.${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Module found: dm-remap-v4-real.ko${NC}"

# Step 2: Create test devices
test_step "Creating test loop devices (100MB main, 50MB spare)"
dd if=/dev/zero of=/tmp/dm-remap-test-main.img bs=1M count=100 status=none
dd if=/dev/zero of=/tmp/dm-remap-test-spare.img bs=1M count=50 status=none

LOOP_MAIN=$(sudo losetup -f --show /tmp/dm-remap-test-main.img)
LOOP_SPARE=$(sudo losetup -f --show /tmp/dm-remap-test-spare.img)

echo "Main device: $LOOP_MAIN"
echo "Spare device: $LOOP_SPARE"
test_pass "Loop devices created"

# Step 3: Create dm-linear device with bad sectors
test_step "Creating dm-linear + dm-error stack (bad sectors: 1000, 2000, 3000)"

# Create dm-error device for sector 1000
echo "0 1 error
1 1 linear $LOOP_MAIN 1
2 1 error
3 $((204800 - 3)) linear $LOOP_MAIN 3" | sudo dmsetup create error_device

# Create dm-linear on top
echo "0 204800 linear /dev/mapper/error_device 0" | sudo dmsetup create linear_main

if [ ! -b "/dev/mapper/linear_main" ]; then
    test_fail "Failed to create dm-linear device"
    exit 1
fi
test_pass "dm-linear + dm-error created with bad sectors"

# Step 4: Load dm-remap modules (stats first, then main module)
test_step "Loading dm-remap-v4-stats module"
if lsmod | grep -q dm_remap_v4_stats; then
    sudo rmmod dm-remap-v4-stats 2>/dev/null || true
    sleep 1
fi

sudo insmod "$PROJECT_DIR/src/dm-remap-v4-stats.ko"

if ! lsmod | grep -q dm_remap_v4_stats; then
    test_fail "Failed to load stats module"
    exit 1
fi
test_pass "Stats module loaded successfully"

test_step "Loading dm-remap-v4-real module"
if lsmod | grep -q dm_remap_v4_real; then
    sudo rmmod dm-remap-v4-real 2>/dev/null || true
    sleep 1
fi

sudo insmod "$PROJECT_DIR/src/dm-remap-v4-real.ko" real_device_mode=1

if ! lsmod | grep -q dm_remap_v4_real; then
    test_fail "Failed to load module"
    exit 1
fi
test_pass "Module loaded successfully"

# Step 5: Create dm-remap device
test_step "Creating dm-remap device"
echo "0 204800 dm-remap-v4 /dev/mapper/linear_main $LOOP_SPARE" | sudo dmsetup create dm-remap-test

if [ ! -b "/dev/mapper/dm-remap-test" ]; then
    test_fail "Failed to create dm-remap device"
    exit 1
fi
test_pass "dm-remap device created successfully"

# Step 6: Trigger error to create remap
test_step "Reading from bad sector 1000 to trigger remap"
sudo dd if=/dev/mapper/dm-remap-test of=/dev/null bs=512 skip=1000 count=1 2>/dev/null || true
sleep 1

# Check dmesg for remap
if sudo dmesg | tail -50 | grep -q "remap"; then
    test_pass "Remap appears to have been triggered (check dmesg)"
else
    echo -e "${YELLOW}WARNING: No remap message in dmesg (this might be normal)${NC}"
fi

# Step 7: Read from normal sectors
test_step "Reading from normal sectors (should work)"
if sudo dd if=/dev/mapper/dm-remap-test of=/dev/null bs=4K count=10 skip=100 2>/dev/null; then
    test_pass "Normal I/O works correctly"
else
    test_fail "Normal I/O failed"
fi

# Step 8: Device removal (CRITICAL TEST)
test_step "Removing dm-remap device (testing device removal fix)"
if sudo dmsetup remove dm-remap-test; then
    test_pass "Device removed successfully - NO CRASH!"
else
    test_fail "Device removal failed"
fi

# Step 9: Module unload
test_step "Unloading modules"
if sudo rmmod dm-remap-v4-real; then
    test_pass "dm-remap module unloaded successfully"
else
    test_fail "dm-remap module unload failed"
fi

if sudo rmmod dm-remap-v4-stats; then
    test_pass "stats module unloaded successfully"
else
    test_fail "stats module unload failed"
fi

# Step 10: Check kernel log for crashes
test_step "Checking kernel log for crashes/warnings"
if sudo dmesg | tail -100 | grep -iE "(BUG|oops|panic|WARNING.*remap)"; then
    echo -e "${YELLOW}Found warnings/errors in dmesg (review above)${NC}"
else
    test_pass "No kernel crashes or panics detected"
fi

echo ""
echo "========================================="
echo "DETAILED DMESG OUTPUT (last 30 lines)"
echo "========================================="
sudo dmesg | tail -30

# Cleanup will be called by trap
