#!/bin/bash
#
# manual_remap_bad_sectors.sh - Manual remap control for specific bad sectors
#
# This demonstrates using the dm-remap debug interface to manually
# mark specific sectors as remapped, simulating pre-existing bad sectors
# that have already been discovered and remapped.
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m'

echo -e "${PURPLE}=== MANUAL REMAP CONTROL FOR BAD SECTORS ===${NC}"
echo "Demonstrating manual control of remapped sectors via debugfs"
echo

# Configuration
TEST_SIZE_MB=50
SPARE_SIZE_MB=10

# Define sectors to manually remap (simulating pre-discovered bad sectors)
declare -a REMAP_SECTORS=(
    1000   # Remap to spare sector 0
    2000   # Remap to spare sector 8  
    3000   # Remap to spare sector 16
    4000   # Remap to spare sector 24
)

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up manual remap test...${NC}"
    
    sudo dmsetup remove manual-remap-test 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/manual_main.img /tmp/manual_spare.img
    rm -f /tmp/remap_test_*.dat
    
    echo "Cleanup completed."
}

trap cleanup EXIT

echo -e "${BLUE}Phase 1: Setting up devices${NC}"

# Create test devices
dd if=/dev/zero of=/tmp/manual_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/manual_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/manual_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/manual_spare.img)

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"
echo "Total sectors: $MAIN_SECTORS"

echo -e "${BLUE}Phase 2: Creating dm-remap target${NC}"

# Create dm-remap target
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 1000" | sudo dmsetup create manual-remap-test

echo "✅ dm-remap target created"

# Check if debugfs interface is available
if [ ! -d "/sys/kernel/debug/dm-remap" ]; then
    echo -e "${YELLOW}⚠️ debugfs interface not available${NC}"
    echo "This could be because:"
    echo "  • debugfs is not mounted"
    echo "  • dm-remap debug interface is disabled"
    echo "  • Module was loaded without debug support"
    
    # Try to show current status anyway
    sudo dmsetup status manual-remap-test
    exit 0
fi

echo -e "${BLUE}Phase 3: Writing test patterns to main device${NC}"

# Write recognizable patterns to specific sectors on main device
for i in "${!REMAP_SECTORS[@]}"; do
    sector=${REMAP_SECTORS[$i]}
    
    # Create a unique pattern for this sector
    echo "MAIN_DATA_SECTOR_${sector}_PATTERN" > /tmp/remap_test_${sector}.dat
    
    # Pad to 512 bytes
    truncate -s 512 /tmp/remap_test_${sector}.dat
    
    # Write directly to main device
    echo "Writing pattern to main device sector $sector"
    sudo dd if=/tmp/remap_test_${sector}.dat of="$MAIN_LOOP" bs=512 seek="$sector" count=1 conv=notrunc 2>/dev/null
done

echo -e "${BLUE}Phase 4: Writing different patterns to spare device${NC}"

# Write different patterns to spare device sectors
for i in "${!REMAP_SECTORS[@]}"; do
    sector=${REMAP_SECTORS[$i]}
    spare_sector=$((i * 8))  # Use different spare sectors
    
    # Create a different pattern for spare
    echo "SPARE_DATA_FOR_SECTOR_${sector}_AT_SPARE_${spare_sector}" > /tmp/spare_test_${sector}.dat
    
    # Pad to 512 bytes
    truncate -s 512 /tmp/spare_test_${sector}.dat
    
    # Write to spare device
    echo "Writing spare pattern to spare sector $spare_sector (for main sector $sector)"
    sudo dd if=/tmp/spare_test_${sector}.dat of="$SPARE_LOOP" bs=512 seek="$spare_sector" count=1 conv=notrunc 2>/dev/null
done

echo -e "${BLUE}Phase 5: Testing before manual remapping${NC}"

echo "Reading sectors before remapping (should get main device data):"
for sector in "${REMAP_SECTORS[@]}"; do
    echo -n "Sector $sector: "
    data=$(sudo dd if=/dev/mapper/manual-remap-test bs=512 skip="$sector" count=1 2>/dev/null | head -c 20)
    echo "\"$data...\""
done

echo -e "${BLUE}Phase 6: Creating manual remaps${NC}"

echo "Adding manual remaps via debugfs interface:"
for i in "${!REMAP_SECTORS[@]}"; do
    sector=${REMAP_SECTORS[$i]}
    spare_sector=$((i * 8))
    
    echo -n "Remapping sector $sector to spare sector $spare_sector: "
    
    # Add manual remap via debugfs
    remap_command="add $sector $spare_sector"
    if echo "$remap_command" | sudo tee /sys/kernel/debug/dm-remap/manual_remap > /dev/null; then
        echo -e "${GREEN}✅${NC}"
    else
        echo -e "${RED}❌${NC}"
    fi
done

echo -e "${BLUE}Phase 7: Verifying manual remaps${NC}"

echo "Current remap table:"
if [ -f "/sys/kernel/debug/dm-remap/remap_table" ]; then
    sudo cat /sys/kernel/debug/dm-remap/remap_table
else
    echo "Remap table not available"
fi

echo ""
echo "dm-remap status:"
sudo dmsetup status manual-remap-test

echo -e "${BLUE}Phase 8: Testing after manual remapping${NC}"

echo "Reading sectors after remapping (should get spare device data):"
for sector in "${REMAP_SECTORS[@]}"; do
    echo -n "Sector $sector: "
    data=$(sudo dd if=/dev/mapper/manual-remap-test bs=512 skip="$sector" count=1 2>/dev/null | head -c 30)
    echo "\"$data...\""
done

echo -e "${BLUE}Phase 9: Verification test${NC}"

echo "Performing verification reads to confirm remapping:"
for i in "${!REMAP_SECTORS[@]}"; do
    sector=${REMAP_SECTORS[$i]}
    
    # Read through dm-remap
    dm_remap_data=$(sudo dd if=/dev/mapper/manual-remap-test bs=512 skip="$sector" count=1 2>/dev/null | head -c 30)
    
    # Read directly from spare device
    spare_sector=$((i * 8))
    spare_data=$(sudo dd if="$SPARE_LOOP" bs=512 skip="$spare_sector" count=1 2>/dev/null | head -c 30)
    
    echo -n "Sector $sector verification: "
    if [ "$dm_remap_data" = "$spare_data" ]; then
        echo -e "${GREEN}✅ Correctly remapped${NC}"
    else
        echo -e "${RED}❌ Remap failed${NC}"
        echo "  dm-remap: \"$dm_remap_data\""
        echo "  spare:    \"$spare_data\""
    fi
done

echo -e "${BLUE}Phase 10: Write test${NC}"

echo "Testing writes to remapped sectors:"
for i in "${!REMAP_SECTORS[@]}"; do
    sector=${REMAP_SECTORS[$i]}
    
    # Create new test data
    echo "NEW_WRITE_TEST_SECTOR_${sector}" > /tmp/write_test_${sector}.dat
    truncate -s 512 /tmp/write_test_${sector}.dat
    
    echo -n "Writing to remapped sector $sector: "
    if sudo dd if=/tmp/write_test_${sector}.dat of=/dev/mapper/manual-remap-test bs=512 seek="$sector" count=1 conv=notrunc 2>/dev/null; then
        echo -e "${GREEN}Success${NC}"
        
        # Verify it went to spare device
        spare_sector=$((i * 8))
        spare_data=$(sudo dd if="$SPARE_LOOP" bs=512 skip="$spare_sector" count=1 2>/dev/null | head -c 20)
        echo "  Spare device now contains: \"$spare_data...\""
    else
        echo -e "${RED}Failed${NC}"
    fi
done

echo ""
echo -e "${PURPLE}=== MANUAL REMAP CONTROL SUMMARY ===${NC}"
echo ""
echo "✅ Demonstrated capabilities:"
echo "  • Manual remap creation via debugfs interface"
echo "  • Precise sector-to-sector remapping control"
echo "  • Verification of remap functionality"
echo "  • Read/write operations on remapped sectors"
echo ""
echo "🎯 This method allows you to:"
echo "  • Simulate pre-existing bad sectors that are already remapped"
echo "  • Test specific remap scenarios"
echo "  • Create controlled test environments"
echo "  • Verify remap table accuracy"
echo ""
echo "💡 Combined with dm-flakey/dm-error, you can:"
echo "  • Create complex failure scenarios"
echo "  • Test both automatic and manual remapping"
echo "  • Simulate real-world disk failure patterns"

echo ""
echo -e "${GREEN}Manual remap control demonstration completed!${NC}"

exit 0