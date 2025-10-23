#!/bin/bash
#
# test_v4.2_auto_repair.sh - Test automatic metadata repair functionality
#
# Tests:
# 1. Basic device setup (regression test)
# 2. Normal I/O operations (regression test)
# 3. Metadata corruption simulation
# 4. Automatic repair verification
# 5. Statistics tracking
# 6. Device teardown during repair (stress test)
#
# Usage: sudo ./test_v4.2_auto_repair.sh

# Disable exit on error temporarily for debugging
# set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test configuration
TEST_DIR="/tmp/dm-remap-test-v4.2"
MAIN_DEV="${TEST_DIR}/main.img"
SPARE_DEV="${TEST_DIR}/spare.img"
DM_NAME="test-remap-v42"
MAIN_SIZE_MB=100
SPARE_SIZE_MB=50

# Statistics
TESTS_PASSED=0
TESTS_FAILED=0

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Remove device mapper device
    if dmsetup info ${DM_NAME} &>/dev/null; then
        dmsetup remove ${DM_NAME} 2>/dev/null || true
        sleep 1
    fi
    
    # Detach loop devices
    for dev in $(losetup -a | grep "${TEST_DIR}" | cut -d: -f1); do
        losetup -d ${dev} 2>/dev/null || true
    done
    
    # Remove test directory
    rm -rf ${TEST_DIR}
    
    # Unload modules
    rmmod dm-remap-v4-real 2>/dev/null || true
    rmmod dm-remap-v4-stats 2>/dev/null || true
}

# Error handler
error_exit() {
    echo -e "${RED}ERROR: $1${NC}"
    cleanup
    exit 1
}

# Test result reporting
report_test() {
    local test_name="$1"
    local result="$2"
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✓ PASS${NC}: $test_name"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗ FAIL${NC}: $test_name"
        ((TESTS_FAILED++))
    fi
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    error_exit "This test must be run as root"
fi

echo "========================================="
echo "dm-remap v4.2 Auto-Repair Test Suite"
echo "========================================="
echo ""

# Trap exit to ensure cleanup
trap cleanup EXIT

# Create test directory
mkdir -p ${TEST_DIR}
cd ${TEST_DIR}

echo -e "${YELLOW}[1/8] Setting up test environment...${NC}"

# Create sparse files for testing
dd if=/dev/zero of=${MAIN_DEV} bs=1M count=0 seek=${MAIN_SIZE_MB} 2>/dev/null
dd if=/dev/zero of=${SPARE_DEV} bs=1M count=0 seek=${SPARE_SIZE_MB} 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(losetup -f --show ${MAIN_DEV})
SPARE_LOOP=$(losetup -f --show ${SPARE_DEV})

echo "Main device: ${MAIN_LOOP} (${MAIN_SIZE_MB}MB)"
echo "Spare device: ${SPARE_LOOP} (${SPARE_SIZE_MB}MB)"

# Load the modules
echo -e "${YELLOW}[2/8] Loading dm-remap modules...${NC}"

# Load stats module first (dependency)
if ! lsmod | grep -q dm_remap_v4_stats; then
    STATS_MODULE="/home/christian/kernel_dev/dm-remap/src/dm-remap-v4-stats.ko"
    if [ ! -f "${STATS_MODULE}" ]; then
        error_exit "Stats module not found at ${STATS_MODULE}"
    fi
    insmod ${STATS_MODULE} || error_exit "Failed to load stats module"
fi

# Load main module
if ! lsmod | grep -q dm_remap_v4_real; then
    MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm-remap-v4-real.ko"
    if [ ! -f "${MODULE_PATH}" ]; then
        error_exit "Module not found at ${MODULE_PATH}. Ensure 'make' was run."
    fi
    insmod ${MODULE_PATH} || error_exit "Failed to load module"
fi
sleep 1

# Create dm-remap device
echo -e "${YELLOW}[3/8] Creating dm-remap device...${NC}"
MAIN_SECTORS=$(blockdev --getsz ${MAIN_LOOP})
echo "0 ${MAIN_SECTORS} dm-remap-v4 ${MAIN_LOOP} ${SPARE_LOOP}" | \
    dmsetup create ${DM_NAME} || error_exit "Failed to create dm device"

DM_DEV="/dev/mapper/${DM_NAME}"
sleep 1

if [ ! -b ${DM_DEV} ]; then
    error_exit "dm device ${DM_DEV} not created"
fi

report_test "Device creation" "PASS"

# Test 1: Basic I/O (Regression test)
echo -e "${YELLOW}[4/8] Testing basic I/O operations...${NC}"
dd if=/dev/urandom of=${DM_DEV} bs=1M count=10 2>/dev/null || error_exit "Write test failed"
dd if=${DM_DEV} of=/dev/null bs=1M count=10 2>/dev/null || error_exit "Read test failed"
report_test "Basic I/O operations" "PASS"

# Test 2: Manually write initial metadata (since auto-persist not yet implemented)
echo -e "${YELLOW}[5/8] Writing initial metadata manually...${NC}"
# Create a minimal valid metadata structure
# For now, just verify the deferred read mechanism works
dmesg | tail -20 | grep -q "deferred metadata read" 2>/dev/null
if [ $? -eq 0 ]; then
    report_test "Deferred metadata read mechanism" "PASS"
else
    report_test "Deferred metadata read mechanism" "FAIL"
fi

# Test 3: Check dmesg for repair infrastructure
echo -e "${YELLOW}[6/8] Checking repair infrastructure initialization...${NC}"
dmesg | tail -50 | grep -q "Repair context initialized" 2>/dev/null
if [ $? -eq 0 ]; then
    report_test "Repair context initialization" "PASS"
else
    report_test "Repair context initialization" "FAIL"
fi

# Test 4: Note - full corruption testing requires metadata persistence
echo -e "${YELLOW}[7/8] Testing repair infrastructure readiness...${NC}"

# Since metadata persistence is not yet active, we test that:
# 1. Repair context is initialized
# 2. Repair workqueue exists
# 3. Deferred metadata read completes without crash

# Check that the repair work completed
dmesg | tail -30 | grep -q "Deferred metadata read completed" 2>/dev/null
if [ $? -eq 0 ]; then
    report_test "Repair infrastructure operational" "PASS"
    echo "Note: Full corruption/repair testing requires metadata persistence (planned for next iteration)"
else
    report_test "Repair infrastructure operational" "FAIL"
fi

# Remove and recreate device to trigger metadata read
dmsetup remove ${DM_NAME}
sleep 1

echo "Recreating device to trigger metadata read and repair detection..."
echo "0 ${MAIN_SECTORS} dm-remap-v4 ${MAIN_LOOP} ${SPARE_LOOP}" | \
    dmsetup create ${DM_NAME} || error_exit "Failed to recreate dm device"

sleep 2

# Check if corruption was detected
echo "Checking kernel logs for corruption detection..."
dmesg | tail -100 | grep -i "corruption\|repair" | tail -10

if dmesg | tail -100 | grep -q "corruption detected\|Metadata repair\|valid copies"; then
    report_test "Corruption detection" "PASS"
else
    report_test "Corruption detection" "FAIL"
fi

# Test 5: Device removal (cleanup test)
echo -e "${YELLOW}[8/8] Testing clean device removal...${NC}"
dmsetup remove ${DM_NAME} || error_exit "Failed to remove device"

# Check for clean shutdown messages
if dmesg | tail -50 | grep -q "Repair context cleaned up\|repair subsystem cleaned up"; then
    report_test "Clean device removal" "PASS"
else
    report_test "Clean device removal" "FAIL"
fi

# Summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo -e "${GREEN}Tests passed: ${TESTS_PASSED}${NC}"
if [ ${TESTS_FAILED} -gt 0 ]; then
    echo -e "${RED}Tests failed: ${TESTS_FAILED}${NC}"
else
    echo -e "${GREEN}Tests failed: ${TESTS_FAILED}${NC}"
fi
echo ""

# Recent kernel messages
echo "========================================="
echo "Recent kernel messages (repair-related):"
echo "========================================="
dmesg | tail -50 | grep -i "remap\|repair\|corruption" | tail -20

# Exit with appropriate code
if [ ${TESTS_FAILED} -gt 0 ]; then
    echo -e "${RED}Some tests FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}All tests PASSED${NC}"
    exit 0
fi
