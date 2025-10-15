#!/bin/bash
#
# test_spare_sizing_v4.0.1.sh - Demonstrate intelligent spare sizing (v4.0.1)
#
# This test shows the dramatic improvement in spare device sizing:
# - OLD (v4.0 Phase 1): Spare must be ≥105% of main (wasteful!)
# - NEW (v4.0.1): Spare can be <10% of main (realistic!)
#

set -e

# Configuration
MAIN_SIZE_MB=1000  # 1GB main device
EXPECTED_BAD_PCT=2 # Expect 2% bad sectors

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  dm-remap v4.0.1 - Intelligent Spare Sizing Demonstration${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# Calculate what we need
MAIN_SIZE_SECTORS=$((MAIN_SIZE_MB * 1024 * 2))  # 512-byte sectors
EXPECTED_BAD_SECTORS=$((MAIN_SIZE_SECTORS * EXPECTED_BAD_PCT / 100))
METADATA_SECTORS=$((4096 / 512 + 1))  # 4KB base metadata
MAPPING_OVERHEAD_SECTORS=$((EXPECTED_BAD_SECTORS * 64 / 512 + 1))
BASE_REQUIREMENT=$((METADATA_SECTORS + EXPECTED_BAD_SECTORS + MAPPING_OVERHEAD_SECTORS))
MIN_SPARE_SECTORS=$((BASE_REQUIREMENT + BASE_REQUIREMENT * 20 / 100))  # +20% safety margin
MIN_SPARE_MB=$((MIN_SPARE_SECTORS / 2 / 1024))

echo -e "${BLUE}Test Configuration:${NC}"
echo -e "  Main device size: ${YELLOW}${MAIN_SIZE_MB}MB${NC}"
echo -e "  Expected bad sectors: ${YELLOW}${EXPECTED_BAD_PCT}%${NC}"
echo ""

echo -e "${BLUE}Sizing Calculation (Optimized Mode):${NC}"
echo -e "  Main: ${MAIN_SIZE_SECTORS} sectors (${MAIN_SIZE_MB}MB)"
echo -e "  Expected bad sectors (${EXPECTED_BAD_PCT}%): ${EXPECTED_BAD_SECTORS} sectors (~$((EXPECTED_BAD_SECTORS / 2 / 1024))MB)"
echo -e "  Metadata overhead: $((METADATA_SECTORS + MAPPING_OVERHEAD_SECTORS)) sectors (~$(( (METADATA_SECTORS + MAPPING_OVERHEAD_SECTORS) / 2))KB)"
echo -e "  Safety margin (20%): $((BASE_REQUIREMENT * 20 / 100)) sectors"
echo -e "  ${GREEN}Minimum spare: ${MIN_SPARE_SECTORS} sectors (${MIN_SPARE_MB}MB)${NC}"
echo ""

# Compare with old method
OLD_SPARE_MB=$((MAIN_SIZE_MB * 105 / 100))
SAVINGS_MB=$((OLD_SPARE_MB - MIN_SPARE_MB))
SAVINGS_PCT=$((SAVINGS_MB * 100 / OLD_SPARE_MB))

echo -e "${BLUE}Comparison with v4.0 Phase 1:${NC}"
echo -e "  ${RED}OLD (strict): ${OLD_SPARE_MB}MB spare required (105% of main)${NC}"
echo -e "  ${GREEN}NEW (optimized): ${MIN_SPARE_MB}MB spare required${NC}"
echo -e "  ${CYAN}💾 SAVINGS: ${SAVINGS_MB}MB (${SAVINGS_PCT}% reduction!)${NC}"
echo ""

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    sudo dmsetup remove dm-remap-test 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/test_main.img /tmp/test_spare.img
    sudo rmmod dm-remap-v4-real 2>/dev/null || true
    sudo rmmod dm-remap-v4-spare-pool 2>/dev/null || true
    sudo rmmod dm-remap-v4-metadata 2>/dev/null || true
    sudo rmmod dm-remap-v4-setup-reassembly 2>/dev/null || true
}

trap cleanup EXIT

# Test 1: Optimized mode (default)
echo -e "${CYAN}════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  TEST 1: Optimized Mode (spare_overhead_percent=2)${NC}"
echo -e "${CYAN}════════════════════════════════════════════════${NC}"
echo ""

echo "Creating loop devices..."
dd if=/dev/zero of=/tmp/test_main.img bs=1M count=${MAIN_SIZE_MB} 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=$((MIN_SPARE_MB + 5)) 2>/dev/null

sudo losetup /dev/loop20 /tmp/test_main.img
sudo losetup /dev/loop21 /tmp/test_spare.img

echo "Loading modules with optimized sizing..."
sudo insmod src/dm-remap-v4-metadata.ko || true
sudo insmod src/dm-remap-v4-spare-pool.ko || true
sudo insmod src/dm-remap-v4-setup-reassembly.ko || true
sudo insmod src/dm-remap-v4-real.ko spare_overhead_percent=2 strict_spare_sizing=0

echo "Creating dm-remap device..."
echo "0 $((MAIN_SIZE_MB * 2048)) dm-remap-v4 /dev/loop20 /dev/loop21" | sudo dmsetup create dm-remap-test

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ SUCCESS: Device created with ${MIN_SPARE_MB}MB spare for ${MAIN_SIZE_MB}MB main!${NC}"
    echo ""
    echo "Checking kernel log for sizing info..."
    sudo dmesg | tail -15 | grep -E "Optimized spare sizing|Main device:|Expected bad|Metadata overhead|Minimum spare|efficiency"
    echo ""
else
    echo -e "${RED}✗ FAILED: Could not create device${NC}"
    exit 1
fi

# Cleanup before test 2
sudo dmsetup remove dm-remap-test
sudo rmmod dm-remap-v4-real
sudo rmmod dm-remap-v4-spare-pool
sudo rmmod dm-remap-v4-metadata
sudo rmmod dm-remap-v4-setup-reassembly

# Test 2: Try with too-small spare to verify validation
echo -e "${CYAN}════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  TEST 2: Validation Check (spare too small)${NC}"
echo -e "${CYAN}════════════════════════════════════════════════${NC}"
echo ""

TOO_SMALL_MB=$((MIN_SPARE_MB - 10))
echo "Creating spare that's too small (${TOO_SMALL_MB}MB < ${MIN_SPARE_MB}MB required)..."
sudo losetup -d /dev/loop21
rm -f /tmp/test_spare.img
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=${TOO_SMALL_MB} 2>/dev/null
sudo losetup /dev/loop21 /tmp/test_spare.img

echo "Loading modules..."
sudo insmod src/dm-remap-v4-metadata.ko || true
sudo insmod src/dm-remap-v4-spare-pool.ko || true
sudo insmod src/dm-remap-v4-setup-reassembly.ko || true
sudo insmod src/dm-remap-v4-real.ko spare_overhead_percent=2 strict_spare_sizing=0

echo "Attempting to create device (should fail)..."
if echo "0 $((MAIN_SIZE_MB * 2048)) dm-remap-v4 /dev/loop20 /dev/loop21" | sudo dmsetup create dm-remap-test 2>&1; then
    echo -e "${RED}✗ FAILED: Should have rejected too-small spare${NC}"
    exit 1
else
    echo -e "${GREEN}✓ SUCCESS: Correctly rejected insufficient spare device${NC}"
    echo ""
    echo "Error message from kernel:"
    sudo dmesg | tail -10 | grep -E "Spare device insufficient|Increase spare size"
    echo ""
fi

# Cleanup
sudo rmmod dm-remap-v4-real
sudo rmmod dm-remap-v4-spare-pool
sudo rmmod dm-remap-v4-metadata
sudo rmmod dm-remap-v4-setup-reassembly

# Test 3: Strict mode (legacy)
echo -e "${CYAN}════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  TEST 3: Strict Mode (strict_spare_sizing=1, legacy)${NC}"
echo -e "${CYAN}════════════════════════════════════════════════${NC}"
echo ""

echo "Creating large spare for strict mode (${OLD_SPARE_MB}MB)..."
sudo losetup -d /dev/loop21
rm -f /tmp/test_spare.img
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=${OLD_SPARE_MB} 2>/dev/null
sudo losetup /dev/loop21 /tmp/test_spare.img

echo "Loading modules with strict sizing (legacy mode)..."
sudo insmod src/dm-remap-v4-metadata.ko || true
sudo insmod src/dm-remap-v4-spare-pool.ko || true
sudo insmod src/dm-remap-v4-setup-reassembly.ko || true
sudo insmod src/dm-remap-v4-real.ko strict_spare_sizing=1

echo "Creating dm-remap device..."
echo "0 $((MAIN_SIZE_MB * 2048)) dm-remap-v4 /dev/loop20 /dev/loop21" | sudo dmsetup create dm-remap-test

if [ $? -eq 0 ]; then
    echo -e "${YELLOW}✓ Device created in strict mode (wasteful legacy sizing)${NC}"
    echo ""
    echo "Checking kernel log..."
    sudo dmesg | tail -10 | grep -E "strict spare sizing|efficiency"
    echo ""
else
    echo -e "${RED}✗ FAILED: Could not create device in strict mode${NC}"
    exit 1
fi

# Final summary
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}                    TEST SUMMARY                              ${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${GREEN}✓ All 3 tests passed!${NC}"
echo ""
echo -e "${BLUE}Results:${NC}"
echo -e "  1. ${GREEN}Optimized mode: ${MIN_SPARE_MB}MB spare works perfectly${NC}"
echo -e "  2. ${GREEN}Validation: Correctly rejects too-small spares${NC}"
echo -e "  3. ${YELLOW}Strict mode: ${OLD_SPARE_MB}MB spare (legacy, wasteful)${NC}"
echo ""
echo -e "${CYAN}💡 Key Takeaway:${NC}"
echo -e "   v4.0.1 optimized mode uses ${SAVINGS_PCT}% less space!"
echo -e "   ${MIN_SPARE_MB}MB vs ${OLD_SPARE_MB}MB for ${MAIN_SIZE_MB}MB main device"
echo ""
echo -e "${BLUE}Module Parameters:${NC}"
echo -e "  spare_overhead_percent=N  - Expected bad sector % (default: 2)"
echo -e "  strict_spare_sizing=0|1   - Use legacy mode (default: 0/off)"
echo ""
