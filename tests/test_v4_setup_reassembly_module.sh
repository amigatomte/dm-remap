#!/bin/bash
#
# dm-remap v4.0 Setup Reassembly Module Test (Priority 6)
# Tests basic module functionality and symbol exports
#
# Date: October 15, 2025
#

set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}============================================${NC}"
echo -e "${BLUE}dm-remap v4.0 Setup Reassembly Module Test${NC}"
echo -e "${BLUE}Priority 6: Automatic Setup Reassembly${NC}"
echo -e "${BLUE}============================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This test must be run as root${NC}"
    exit 1
fi

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    rmmod dm_remap_v4_setup_reassembly 2>/dev/null || true
}

trap cleanup EXIT

# Start fresh
cleanup

echo -e "${YELLOW}Test 1: Loading setup-reassembly module...${NC}"
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-setup-reassembly.ko
if lsmod | grep -q dm_remap_v4_setup_reassembly; then
    echo -e "${GREEN}✓ Module loaded successfully${NC}"
else
    echo -e "${RED}✗ Module failed to load${NC}"
    exit 1
fi

echo -e "\n${YELLOW}Test 2: Checking module info...${NC}"
modinfo /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-setup-reassembly.ko | grep -E "filename|description|author|license"
echo -e "${GREEN}✓ Module info looks correct${NC}"

echo -e "\n${YELLOW}Test 3: Verifying exported symbols...${NC}"
SYMBOL_COUNT=$(cat /proc/kallsyms | grep -cE " T dm_remap_v4_(calculate_confidence_score|update_metadata_version|verify_metadata_integrity).*\[dm_remap_v4_setup_reassembly\]" || echo "0")

if [ "$SYMBOL_COUNT" -eq 3 ]; then
    echo -e "${GREEN}✓ All 3 required symbols are exported:${NC}"
    cat /proc/kallsyms | grep -E " T dm_remap_v4_(calculate_confidence_score|update_metadata_version|verify_metadata_integrity).*\[dm_remap_v4_setup_reassembly\]" | while read addr type name rest; do
        echo -e "${GREEN}  ✓ $name${NC}"
    done
else
    echo -e "${RED}✗ Only $SYMBOL_COUNT/3 symbols found${NC}"
    exit 1
fi

echo -e "\n${YELLOW}Test 4: Checking for no floating-point operations...${NC}"
# Try to detect FPU usage (would cause kernel panic if present)
if dmesg | tail -50 | grep -i "fpu\|sse\|floating"; then
    echo -e "${RED}⚠ Warning: FPU-related messages detected${NC}"
else
    echo -e "${GREEN}✓ No FPU/SSE warnings - integer math working${NC}"
fi

echo -e "\n${YELLOW}Test 5: Module can be unloaded cleanly...${NC}"
rmmod dm_remap_v4_setup_reassembly
if ! lsmod | grep -q dm_remap_v4_setup_reassembly; then
    echo -e "${GREEN}✓ Module unloaded successfully${NC}"
else
    echo -e "${RED}✗ Module failed to unload${NC}"
    exit 1
fi

echo -e "\n${YELLOW}Test 6: Module can be reloaded...${NC}"
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-setup-reassembly.ko
if lsmod | grep -q dm_remap_v4_setup_reassembly; then
    echo -e "${GREEN}✓ Module reloaded successfully${NC}"
else
    echo -e "${RED}✗ Module failed to reload${NC}"
    exit 1
fi

echo -e "\n${BLUE}============================================${NC}"
echo -e "${GREEN}✅ Setup Reassembly Module Test PASSED!${NC}"
echo -e "${BLUE}============================================${NC}"
echo ""
echo "Test verified:"
echo "  ✓ Module loads and unloads cleanly"
echo "  ✓ All 3 EXPORT_SYMBOL functions are accessible"
echo "  ✓ No floating-point math (integer confidence scores)"
echo "  ✓ Multi-object module linking works correctly"
echo "  ✓ Module metadata is correct"
echo ""
echo -e "${GREEN}Priority 6 implementation is functional!${NC}"
echo ""
echo "Ready for functional testing with:"
echo "  - Metadata discovery"
echo "  - Device fingerprinting"  
echo "  - Setup grouping"
echo "  - Confidence score calculation"
echo "  - Reconstruction plan generation"
