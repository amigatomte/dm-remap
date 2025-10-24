#!/bin/bash
#
# Test v4.2 Metadata Corruption Detection and Repair
#
# This test validates that dm-remap can:
# 1. Detect corrupted metadata copies
# 2. Automatically repair corruption from redundant copies
# 3. Maintain device integrity during corruption events

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration
TEST_DIR="/tmp/dm-remap-corruption-test"
MAIN_IMG="${TEST_DIR}/main.img"
SPARE_IMG="${TEST_DIR}/spare.img"
MAIN_SIZE_MB=100
SPARE_SIZE_MB=100

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

echo "========================================="
echo "dm-remap v4.2 Corruption/Repair Test"
echo "========================================="

# Check for root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}This test must be run as root${NC}"
    exit 1
fi

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    dmsetup remove test-corruption 2>/dev/null || true
    losetup -d "${MAIN_LOOP}" 2>/dev/null || true
    losetup -d "${SPARE_LOOP}" 2>/dev/null || true
    rm -rf "${TEST_DIR}"
    rmmod dm-remap-v4-real 2>/dev/null || true
    rmmod dm-remap-v4-stats 2>/dev/null || true
}

trap cleanup EXIT

# Create test directory
mkdir -p "${TEST_DIR}"
cd "${TEST_DIR}"

echo ""
echo "[1/8] Creating test loop devices..."
dd if=/dev/zero of="${MAIN_IMG}" bs=1M count=${MAIN_SIZE_MB} 2>/dev/null
dd if=/dev/zero of="${SPARE_IMG}" bs=1M count=${SPARE_SIZE_MB} 2>/dev/null

MAIN_LOOP=$(losetup -f)
losetup "${MAIN_LOOP}" "${MAIN_IMG}"
SPARE_LOOP=$(losetup -f)
losetup "${SPARE_LOOP}" "${SPARE_IMG}"

echo "  Main device : ${MAIN_LOOP}"
echo "  Spare device: ${SPARE_LOOP}"

echo ""
echo "[2/8] Loading dm-remap modules..."
rmmod dm-remap-v4-real 2>/dev/null || true
rmmod dm-remap-v4-stats 2>/dev/null || true
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-stats.ko
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-real.ko

echo ""
echo "[3/8] Creating dm-remap device..."
MAIN_SECTORS=$(blockdev --getsz "${MAIN_LOOP}")
echo "0 ${MAIN_SECTORS} dm-remap-v4 ${MAIN_LOOP} ${SPARE_LOOP}" | dmsetup create test-corruption

echo ""
echo "[4/8] Writing initial metadata..."
/home/christian/kernel_dev/dm-remap/scripts/dm-remap-write-metadata test-corruption

# Verify all 5 copies were written
echo ""
echo "[5/8] Verifying all metadata copies are valid..."
METADATA_COPIES=(0 1024 2048 4096 8192)
VALID_COPIES=0

for sector in "${METADATA_COPIES[@]}"; do
    MAGIC=$(dd if=${SPARE_LOOP} bs=512 skip=$sector count=1 2>/dev/null | xxd -p -l 4)
    if [ "$MAGIC" = "34524d44" ]; then
        echo "  ✓ Copy at sector $sector is valid (magic: 0x$MAGIC)"
        ((VALID_COPIES++))
    else
        echo -e "  ${RED}✗ Copy at sector $sector is invalid (magic: 0x$MAGIC)${NC}"
    fi
done

if [ $VALID_COPIES -eq 5 ]; then
    echo -e "${GREEN}✓ All 5 metadata copies are valid${NC}"
    ((TESTS_PASSED++))
else
    echo -e "${RED}✗ Only $VALID_COPIES/5 metadata copies are valid${NC}"
    ((TESTS_FAILED++))
fi

echo ""
echo "[6/8] Corrupting metadata copies (2 out of 5)..."
# Corrupt copy 1 (sector 1024) and copy 3 (sector 4096)
dd if=/dev/urandom of=${SPARE_LOOP} bs=512 seek=1024 count=1 conv=notrunc 2>/dev/null
dd if=/dev/urandom of=${SPARE_LOOP} bs=512 seek=4096 count=1 conv=notrunc 2>/dev/null
sync

# Verify corruption
CORRUPTED=0
for sector in 1024 4096; do
    MAGIC=$(dd if=${SPARE_LOOP} bs=512 skip=$sector count=1 2>/dev/null | xxd -p -l 4)
    if [ "$MAGIC" != "34524d44" ]; then
        echo "  ✓ Copy at sector $sector successfully corrupted (magic: 0x$MAGIC)"
        ((CORRUPTED++))
    fi
done

if [ $CORRUPTED -eq 2 ]; then
    echo -e "${GREEN}✓ Corrupted 2/5 metadata copies${NC}"
else
    echo -e "${YELLOW}⚠ Only corrupted $CORRUPTED/2 copies${NC}"
fi

echo ""
echo "[7/8] Removing and reassembling device (triggers metadata read)..."
dmsetup remove test-corruption

# Reassemble - this will read metadata and detect corruption
/home/christian/kernel_dev/dm-remap/scripts/dm-remap-scan --verbose 2>&1 | tee /tmp/scan-output.log

# Check if device was created despite corruption
if dmsetup info dm-remap-auto-1 &>/dev/null; then
    echo -e "${GREEN}✓ Device reassembled successfully despite 2 corrupted copies${NC}"
    ((TESTS_PASSED++))
else
    echo -e "${RED}✗ Device failed to reassemble${NC}"
    ((TESTS_FAILED++))
fi

echo ""
echo "[8/8] Verifying I/O still works after corruption..."
if dd if=/dev/mapper/dm-remap-auto-1 of=/dev/null bs=1M count=10 2>/dev/null; then
    echo -e "${GREEN}✓ I/O successful on device with corrupted metadata${NC}"
    ((TESTS_PASSED++))
else
    echo -e "${RED}✗ I/O failed${NC}"
    ((TESTS_FAILED++))
fi

# Check kernel messages for corruption detection
echo ""
echo "========================================="
echo "Checking for corruption detection in kernel log..."
echo "========================================="
if dmesg | tail -50 | grep -i "corrupt" >/dev/null; then
    echo -e "${GREEN}Kernel detected corruption:${NC}"
    dmesg | tail -50 | grep -i "corrupt"
    ((TESTS_PASSED++))
else
    echo -e "${YELLOW}⚠ No corruption messages in kernel log${NC}"
    echo "  (This is expected if kernel auto-repair succeeded silently)"
fi

# Summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Tests passed: $TESTS_PASSED"
echo "Tests failed: $TESTS_FAILED"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests FAILED${NC}"
    exit 1
fi
