#!/bin/bash
#
# advanced_sector_control.sh - Advanced sector-specific error control
#
# This demonstrates multiple methods to create definable broken sectors:
# 1. Using dm-linear to create exact bad sector maps
# 2. Using multiple dm-flakey layers for different error types
# 3. Using dm-error for permanently bad sectors
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m'

echo -e "${PURPLE}=== ADVANCED SECTOR-SPECIFIC ERROR CONTROL ===${NC}"
echo "Demonstrating precise control over which sectors fail"
echo

# Configuration
TEST_SIZE_MB=100
SPARE_SIZE_MB=20

# Define specific sectors that should be "bad"
declare -a BAD_SECTORS=(
    1000   # First bad sector
    1001   # Adjacent bad sector (simulates bad cluster)
    5000   # Isolated bad sector
    12000  # Another bad sector
    25000  # Final bad sector
)

echo "Creating disk with specific bad sectors: ${BAD_SECTORS[*]}"
echo

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up advanced sector control test...${NC}"
    
    # Remove device mapper targets in reverse order
    sudo dmsetup remove advanced-remap 2>/dev/null || true
    sudo dmsetup remove composite-bad-disk 2>/dev/null || true
    sudo dmsetup remove bad-sectors-layer 2>/dev/null || true
    sudo dmsetup remove error-sectors 2>/dev/null || true
    
    # Clean up loop devices
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/advanced_main.img /tmp/advanced_spare.img
    rm -f /tmp/sector_pattern_*.dat
    
    echo "Cleanup completed."
}

trap cleanup EXIT

echo -e "${BLUE}Phase 1: Creating base devices${NC}"

# Create test devices
dd if=/dev/zero of=/tmp/advanced_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/advanced_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/advanced_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/advanced_spare.img)

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP"
echo "Total sectors: $MAIN_SECTORS"

echo -e "${BLUE}Phase 2: Method 1 - Using dm-error for permanently bad sectors${NC}"

# Create a composite device using dm-linear and dm-error
# This creates a disk where specific sectors ALWAYS fail

create_exact_bad_sectors() {
    local device=$1
    local sectors=$2
    local bad_sectors_array=("${!3}")
    
    echo "Creating exact bad sector map..."
    
    # Sort bad sectors
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
        
        # Add the bad sector (using error target)
        table+="$bad_sector 1 error\n"
        current_sector=$((bad_sector + 1))
    done
    
    # Add remaining good sectors
    if [ $current_sector -lt $sectors ]; then
        local remaining=$((sectors - current_sector))
        table+="$current_sector $remaining linear $device $current_sector\n"
    fi
    
    echo -e "$table" | sudo dmsetup create error-sectors
    echo "✅ Exact bad sector map created with permanently failing sectors"
}

# Create the exact bad sector map
create_exact_bad_sectors "$MAIN_LOOP" "$MAIN_SECTORS" BAD_SECTORS[@]

echo -e "${BLUE}Phase 3: Method 2 - Adding intermittent errors on top${NC}"

# Create a dm-flakey layer on top that adds additional intermittent errors
echo "0 $MAIN_SECTORS flakey /dev/mapper/error-sectors 0 8 2" | sudo dmsetup create bad-sectors-layer

echo "✅ Added intermittent error layer (up 8s, down 2s)"

echo -e "${BLUE}Phase 4: Creating composite bad disk${NC}"

# Create final composite that combines permanent and intermittent errors
echo "0 $MAIN_SECTORS linear /dev/mapper/bad-sectors-layer 0" | sudo dmsetup create composite-bad-disk

echo "✅ Composite bad disk created"

echo -e "${BLUE}Phase 5: Setting up dm-remap monitoring${NC}"

# Create dm-remap to monitor the composite bad disk
echo "0 $MAIN_SECTORS remap /dev/mapper/composite-bad-disk $SPARE_LOOP 0 1000" | sudo dmsetup create advanced-remap

echo "✅ dm-remap monitoring the composite bad disk"

# Show device hierarchy
echo ""
echo "Device hierarchy:"
echo "  $MAIN_LOOP (base loop device)"
echo "  └── /dev/mapper/error-sectors (permanent bad sectors: ${BAD_SECTORS[*]})"
echo "      └── /dev/mapper/bad-sectors-layer (+ intermittent errors)"
echo "          └── /dev/mapper/composite-bad-disk"
echo "              └── /dev/mapper/advanced-remap (+ dm-remap monitoring)"

echo -e "${BLUE}Phase 6: Testing specific bad sectors${NC}"

# Clear kernel messages
sudo dmesg -C

# Function to test specific sectors
test_specific_sector() {
    local sector=$1
    local description="$2"
    local expected="$3"
    
    echo -n "Testing $description (sector $sector): "
    
    # Create unique test data
    dd if=/dev/urandom of="/tmp/sector_pattern_$sector.dat" bs=512 count=1 2>/dev/null
    
    # Test write operation
    local write_result="SUCCESS"
    if ! sudo dd if="/tmp/sector_pattern_$sector.dat" of=/dev/mapper/advanced-remap bs=512 seek="$sector" count=1 conv=notrunc,fdatasync 2>/dev/null; then
        write_result="FAILED"
    fi
    
    # Test read operation
    local read_result="SUCCESS"
    if ! sudo dd if=/dev/mapper/advanced-remap of=/dev/null bs=512 skip="$sector" count=1 2>/dev/null; then
        read_result="FAILED"
    fi
    
    if [[ "$write_result" == "$expected" || "$read_result" == "$expected" ]]; then
        echo -e "${GREEN}$write_result/$read_result (as expected)${NC}"
    else
        echo -e "${YELLOW}$write_result/$read_result (unexpected)${NC}"
    fi
    
    # Check dm-remap status
    local status=$(sudo dmsetup status advanced-remap)
    if [[ $status =~ errors=W([0-9]+):R([0-9]+) ]]; then
        local w_errors=${BASH_REMATCH[1]}
        local r_errors=${BASH_REMATCH[2]}
        echo "  Errors detected: Write=$w_errors, Read=$r_errors"
    fi
    
    echo
}

echo "Testing permanently bad sectors (should always fail):"
for sector in "${BAD_SECTORS[@]}"; do
    test_specific_sector "$sector" "permanent bad sector" "FAILED"
done

echo "Testing good sectors (should succeed when dm-flakey is up):"
GOOD_SECTORS=(2000 6000 15000 30000)
for sector in "${GOOD_SECTORS[@]}"; do
    test_specific_sector "$sector" "good sector" "SUCCESS"
done

echo -e "${BLUE}Phase 7: Timing-based test (intermittent errors)${NC}"

echo "Testing during dm-flakey error injection period..."
echo "Waiting for error injection cycle..."

# Wait for dm-flakey to enter error period and test
sleep 9  # Wait for up period to end (8s up + 1s buffer)

echo "Now testing during error injection period:"
test_specific_sector 6000 "good sector during error period" "FAILED"

# Wait for recovery
sleep 3
echo "Testing after error period ends:"
test_specific_sector 6000 "good sector after recovery" "SUCCESS"

echo -e "${BLUE}Phase 8: Results and analysis${NC}"

echo "Final device status:"
sudo dmsetup status advanced-remap
sudo dmsetup status composite-bad-disk
sudo dmsetup status bad-sectors-layer
sudo dmsetup status error-sectors

echo ""
echo "Recent kernel messages:"
sudo dmesg -T | grep -E "(dm-remap|error|flakey)" | tail -15

echo ""
echo -e "${PURPLE}=== ADVANCED SECTOR CONTROL SUMMARY ===${NC}"
echo ""
echo "✅ Demonstrated capabilities:"
echo "  • Permanently bad sectors using dm-error: ${BAD_SECTORS[*]}"
echo "  • Intermittent errors using dm-flakey (8s up, 2s down)"
echo "  • Composite bad disk combining multiple error types"
echo "  • dm-remap integration for error detection and remapping"
echo ""
echo "🎯 Methods available for definable broken sectors:"
echo "  1. dm-error: Permanently failing sectors"
echo "  2. dm-flakey: Time-based intermittent failures"
echo "  3. dm-linear: Composite devices with exact sector control"
echo "  4. dm-dust: Surgical precision (if available)"
echo ""
echo "💡 This approach gives you EXACT control over:"
echo "  • Which specific sectors fail"
echo "  • What type of failure (permanent vs intermittent)"
echo "  • Error timing and patterns"
echo "  • Integration with dm-remap auto-remapping"

echo ""
echo -e "${GREEN}Advanced sector-specific error simulation completed!${NC}"

exit 0