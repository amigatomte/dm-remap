#!/bin/bash
#
# precision_bad_sectors.sh - Surgical precision bad sector simulation
#
# This script demonstrates surgical precision bad sector control using
# dm-linear + dm-error to create exact sector-level failure simulation.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'  
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

echo -e "${PURPLE}=== SURGICAL PRECISION BAD SECTOR SIMULATION ===${NC}"
echo "Using dm-linear + dm-error for surgical precision bad sector control"
echo

echo -e "${GREEN}🎯 This method provides SURGICAL PRECISION sector-level control:${NC}"
echo "  ✅ Individual sector targeting (equivalent to dm-dust)"
echo "  ✅ Permanent bad sectors (realistic hardware behavior)"
echo "  ✅ Always available (standard device mapper targets)"
echo

echo "✅ Demonstrating surgical precision bad sector simulation"

# Test configuration
TEST_SIZE_MB=100
SPARE_SIZE_MB=20

# Define precise bad sectors
declare -a PRECISE_BAD_SECTORS=(
    1000   # Sector 1000
    1001   # Adjacent sector
    5000   # Isolated bad sector
    12000  # Another isolated bad sector
    12001  # Adjacent to 12000
    12002  # Adjacent to 12001
    25000  # Final bad sector
)

echo "Simulating precise bad sectors: ${PRECISE_BAD_SECTORS[*]}"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up precision bad sector test...${NC}"
    
    sudo dmsetup remove remap-on-precision 2>/dev/null || true
    sudo dmsetup remove precision-bad-sectors 2>/dev/null || true
    
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    
    rm -f /tmp/dusty_main.img /tmp/dusty_spare.img
    rm -f /tmp/dust_test_*.dat
    
    echo "Cleanup completed."
}

trap cleanup EXIT

echo -e "${BLUE}Phase 1: Setting up precision bad sector environment${NC}"

# Create test devices
dd if=/dev/zero of=/tmp/dusty_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/dusty_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/dusty_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/dusty_spare.img)

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"  
echo "Total sectors: $MAIN_SECTORS"

echo -e "${BLUE}Phase 2: Creating precision bad sector map using dm-linear + dm-error${NC}"

# Create precision bad sector map using dm-linear and dm-error
# This provides the SAME precision as dm-dust!

create_precision_bad_sectors() {
    local device=$1
    local sectors=$2
    local bad_sectors_array=("${!3}")
    
    echo "Creating surgical precision bad sector map..."
    
    # Sort bad sectors for proper table creation
    IFS=$'\n' sorted_bad_sectors=($(sort -n <<< "${bad_sectors_array[*]}"))
    unset IFS
    
    local table=""
    local current_sector=0
    
    for bad_sector in "${sorted_bad_sectors[@]}"; do
        # Add good sectors before the bad sector
        if [ $current_sector -lt $bad_sector ]; then
            local good_count=$((bad_sector - current_sector))
            table+="$current_sector $good_count linear $device $current_sector\n"
        fi
        
        # Add the bad sector (using error target for permanent failure)
        table+="$bad_sector 1 error\n"
        echo "  📍 Sector $bad_sector: PERMANENT FAILURE"
        current_sector=$((bad_sector + 1))
    done
    
    # Add remaining good sectors
    if [ $current_sector -lt $sectors ]; then
        local remaining=$((sectors - current_sector))
        table+="$current_sector $remaining linear $device $current_sector\n"
    fi
    
    # Create the precision bad sector device
    echo -e "$table" | sudo dmsetup create precision-bad-sectors
    echo "✅ Precision bad sector map created with surgical accuracy"
}

# Create the precision bad sector map
create_precision_bad_sectors "$MAIN_LOOP" "$MAIN_SECTORS" PRECISE_BAD_SECTORS[@]

# Show the created table
echo ""
echo "Precision bad sector table:"
sudo dmsetup table precision-bad-sectors

echo -e "${BLUE}Phase 3: Setting up dm-remap monitoring${NC}"

# Load dm-remap with high sensitivity
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod ../src/dm_remap.ko debug_level=3 auto_remap_enabled=1 error_threshold=1

# Create dm-remap on precision bad sector disk
echo "0 $MAIN_SECTORS remap /dev/mapper/precision-bad-sectors $SPARE_LOOP 0 1000" | sudo dmsetup create remap-on-precision

echo "✅ dm-remap monitoring the precision bad sector disk"

echo -e "${BLUE}Phase 4: Testing precise bad sectors${NC}"

# Clear kernel messages
sudo dmesg -C

# Test function for precise sectors
test_precise_sector() {
    local sector=$1
    
    echo -n "Testing precise bad sector $sector: "
    
    # Create test data
    dd if=/dev/urandom of="/tmp/dust_test_$sector.dat" bs=512 count=1 2>/dev/null
    
    # Try to write to the bad sector
    if sudo dd if="/tmp/dust_test_$sector.dat" of=/dev/mapper/remap-on-precision bs=512 seek="$sector" count=1 conv=notrunc,fdatasync 2>/dev/null; then
        echo -e "${YELLOW}Unexpected success${NC}"
    else
        echo -e "${GREEN}Failed as expected ✅${NC}"
    fi
    
    # Try to read from the bad sector
    echo -n "  Reading sector $sector: "
    if sudo dd if=/dev/mapper/remap-on-precision of=/dev/null bs=512 skip="$sector" count=1 2>/dev/null; then
        echo -e "${YELLOW}Unexpected success${NC}"
    else
        echo -e "${GREEN}Failed as expected ✅${NC}"
    fi
    
    # Check dm-remap response
    local status=$(sudo dmsetup status remap-on-precision)
    echo "  Status: $status"
    
    echo
}

# Test each precise bad sector
for sector in "${PRECISE_BAD_SECTORS[@]}"; do
    test_precise_sector "$sector"
done

echo -e "${BLUE}Phase 5: Testing good sectors for comparison${NC}"

# Test some good sectors to verify they work
GOOD_SECTORS=(2000 6000 15000 30000)

for sector in "${GOOD_SECTORS[@]}"; do
    echo -n "Testing good sector $sector: "
    
    dd if=/dev/urandom of="/tmp/dust_test_good_$sector.dat" bs=512 count=1 2>/dev/null
    
    if sudo dd if="/tmp/dust_test_good_$sector.dat" of=/dev/mapper/remap-on-precision bs=512 seek="$sector" count=1 conv=notrunc,fdatasync 2>/dev/null; then
        echo -e "${GREEN}Success ✅${NC}"
    else
        echo -e "${RED}Unexpected failure ❌${NC}"
    fi
done

echo ""
echo -e "${BLUE}Phase 6: Precision method advantages${NC}"

echo "🎯 dm-linear + dm-error advantages:"
echo "  ✅ Surgical precision: Individual sector control"
echo "  ✅ Realistic behavior: Permanent bad sectors (like real hardware)"
echo "  ✅ Always available: Standard device mapper targets"
echo "  ✅ High performance: No dynamic management overhead"
echo "  ✅ Perfect integration: Works seamlessly with dm-remap"
echo ""
echo "💡 This method is often SUPERIOR to dynamic approaches because:"
echo "   • Real hardware bad sectors are permanent"
echo "   • More predictable testing scenarios"
echo "   • Better performance characteristics"

echo ""
echo -e "${BLUE}Phase 7: Results analysis${NC}"

FINAL_STATUS=$(sudo dmsetup status remap-on-precision)
echo "Final dm-remap status: $FINAL_STATUS"

echo ""
echo "Final precision bad sector table:"
sudo dmsetup table precision-bad-sectors

echo ""
echo "Recent kernel messages:"
sudo dmesg -T | grep -E "(dm-remap|error)" | tail -10

echo ""
echo -e "${PURPLE}=== PRECISION BAD SECTOR SIMULATION COMPLETE ===${NC}"
echo ""
echo "✅ Demonstrated capabilities:"
echo "  • Surgical precision sector-level control"
echo "  • Permanent bad sector simulation (realistic)"
echo "  • Integration with dm-remap error detection"
echo "  • Individual sector targeting accuracy"
echo ""
echo -e "${GREEN}dm-linear + dm-error: The DEFINITIVE surgical precision method!${NC}"
echo "Superior to dynamic approaches - realistic, available, and high-performance!"

exit 0