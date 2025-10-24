#!/bin/bash
#
# Test dm-remap-scan tool
#
# Creates test devices with metadata, runs scanner, verifies reassembly
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Test configuration
TEST_DIR="/tmp/dm-remap-scan-test"
MAIN_IMG="${TEST_DIR}/main.img"
SPARE_IMG="${TEST_DIR}/spare.img"
MAIN_SIZE_MB=100
SPARE_SIZE_MB=50

echo "========================================="
echo "dm-remap-scan Tool Test"
echo "========================================="
echo ""

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Remove any auto-created dm devices
    for dm in /dev/mapper/dm-remap-auto-*; do
        if [ -e "$dm" ]; then
            dmsetup remove "$(basename "$dm")" 2>/dev/null || true
        fi
    done
    
    # Remove manual test device
    dmsetup remove test-remap-scan 2>/dev/null || true
    
    # Detach loop devices
    for dev in $(losetup -a | grep "${TEST_DIR}" | cut -d: -f1); do
        losetup -d "${dev}" 2>/dev/null || true
    done
    
    # Remove test directory
    rm -rf "${TEST_DIR}"
}

trap cleanup EXIT

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}This test must be run as root${NC}"
    exit 1
fi

# Create test directory
mkdir -p "${TEST_DIR}"
cd "${TEST_DIR}"

echo "[1/6] Creating test loop devices..."
dd if=/dev/zero of="${MAIN_IMG}" bs=1M count=${MAIN_SIZE_MB} 2>/dev/null
dd if=/dev/zero of="${SPARE_IMG}" bs=1M count=${SPARE_SIZE_MB} 2>/dev/null

MAIN_LOOP=$(losetup -f)
SPARE_LOOP=$(losetup -f)
losetup "${MAIN_LOOP}" "${MAIN_IMG}"
# Find next free loop device after attaching first one
SPARE_LOOP=$(losetup -f)
losetup "${SPARE_LOOP}" "${SPARE_IMG}"

echo "  Main device : ${MAIN_LOOP}"
echo "  Spare device: ${SPARE_LOOP}"

echo ""
echo "[2/6] Loading dm-remap modules..."
rmmod dm-remap-v4-real 2>/dev/null || true
rmmod dm-remap-v4-stats 2>/dev/null || true
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-stats.ko
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-real.ko

echo ""
echo "[3/6] Creating dm-remap device..."
MAIN_SECTORS=$(blockdev --getsz "${MAIN_LOOP}")
echo "0 ${MAIN_SECTORS} dm-remap-v4 ${MAIN_LOOP} ${SPARE_LOOP}" | dmsetup create test-remap-scan

echo ""
echo "[4/6] Writing metadata using dm-remap-write-metadata tool..."
/home/christian/kernel_dev/dm-remap/scripts/dm-remap-write-metadata test-remap-scan

echo ""
echo "[5/6] Removing dm-remap device (leaving metadata on disks)..."
dmsetup remove test-remap-scan

echo ""
echo "[6/7] Running dm-remap-scan in scan-only mode..."
/home/christian/kernel_dev/dm-remap/scripts/dm-remap-scan --scan-only --verbose

echo ""
echo "[7/7] Running dm-remap-scan to auto-reassemble..."
/home/christian/kernel_dev/dm-remap/scripts/dm-remap-scan --verbose

echo ""
echo "========================================="
echo "Checking results..."
echo "========================================="

# Check if device was created
if dmsetup info dm-remap-auto-1 &>/dev/null; then
    echo -e "${GREEN}✓ PASS${NC}: dm-remap-auto-1 device created successfully"
    
    # Show device status
    echo ""
    echo "Device status:"
    dmsetup status dm-remap-auto-1
    
    # Try basic I/O
    echo ""
    echo "Testing I/O on reassembled device..."
    dd if=/dev/mapper/dm-remap-auto-1 of=/dev/null bs=1M count=10 2>/dev/null
    echo -e "${GREEN}✓ PASS${NC}: I/O test successful"
else
    echo -e "${RED}✗ FAIL${NC}: dm-remap-auto-1 device not created"
    exit 1
fi

echo ""
echo -e "${GREEN}All tests passed!${NC}"
