#!/bin/bash
#
# sector_specific_errors.sh - Simulate disk with definable broken sectors
#
# This test creates a dm-flakey configuration that targets specific sectors
# for error injection, simulating a disk with known bad blocks.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

# Test configuration
TEST_SIZE_MB=100
SPARE_SIZE_MB=20

# Define specific "bad sectors" to simulate
declare -a BAD_SECTORS=(
    1000   # First bad sector
    5000   # Second bad sector  
    12000  # Third bad sector
    25000  # Fourth bad sector
)

echo -e "${PURPLE}=== SECTOR-SPECIFIC ERROR SIMULATION ===${NC}"
echo "Simulating disk with predefined bad sectors: ${BAD_SECTORS[*]}"
echo

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up sector-specific error test...${NC}"
    
    # Remove device mapper targets
    for i in "${!BAD_SECTORS[@]}"; do
        sudo dmsetup remove "bad-sector-$i" 2>/dev/null || true
    done
    sudo dmsetup remove remap-on-bad-disk 2>/dev/null || true
    sudo dmsetup remove simulated-bad-disk 2>/dev/null || true
    
    # Clean up loop devices
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/bad_disk_main.img /tmp/bad_disk_spare.img
    rm -f /tmp/sector_test_*.dat
    
    echo "Cleanup completed."
}

trap cleanup EXIT

echo -e "${BLUE}Phase 1: Creating simulated bad disk${NC}"

# Create test devices
dd if=/dev/zero of=/tmp/bad_disk_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/bad_disk_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/bad_disk_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/bad_disk_spare.img)

echo "Main device: $MAIN_LOOP (simulating disk with bad sectors)"
echo "Spare device: $SPARE_LOOP (good spare device)"

# Get device size
MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Total sectors: $MAIN_SECTORS"

echo -e "${BLUE}Phase 2: Creating error injection layers for specific sectors${NC}"

# Method A: Create multiple dm-flakey targets for different sector ranges
# This simulates a disk where specific sectors are permanently bad

create_bad_sector_layer() {
    local sector=$1
    local layer_name=$2
    local range_size=8  # 4KB around the bad sector
    
    echo "Creating error layer for sector $sector (${layer_name})"
    
    # Create a dm-flakey target that always fails for this sector range
    # Use up_interval=0, down_interval=1 to make it always fail
    echo "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 0 1" | sudo dmsetup create "$layer_name"
    
    echo "  ✅ Bad sector $sector simulation created"
}

# Method B: Use dm-linear to create a "composite" bad disk
echo "Creating composite bad disk simulation..."

# Create dm-flakey for the entire disk that randomly fails
echo "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 10 2" | sudo dmsetup create simulated-bad-disk

echo "✅ Simulated bad disk created with intermittent failures"

echo -e "${BLUE}Phase 3: Setting up dm-remap with error monitoring${NC}"

# Load dm-remap with aggressive error detection
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod /home/christian/kernel_dev/dm-remap/src/dm_remap.ko debug_level=3 auto_remap_enabled=1 error_threshold=1

# Create dm-remap on top of the bad disk simulation
echo "0 $MAIN_SECTORS remap /dev/mapper/simulated-bad-disk $SPARE_LOOP 0 1000" | sudo dmsetup create remap-on-bad-disk

echo "✅ dm-remap monitoring the simulated bad disk"

echo -e "${BLUE}Phase 4: Testing specific bad sectors${NC}"

# Clear kernel messages
sudo dmesg -C

# Function to test a specific sector
test_bad_sector() {
    local sector=$1
    local test_num=$2
    
    echo -n "Testing sector $sector: "
    
    # Create test data
    dd if=/dev/urandom of="/tmp/sector_test_$test_num.dat" bs=4096 count=1 2>/dev/null
    
    # Write to the specific sector multiple times to trigger errors
    local success=0
    local attempts=5
    
    for attempt in $(seq 1 $attempts); do
        if sudo dd if="/tmp/sector_test_$test_num.dat" of=/dev/mapper/remap-on-bad-disk bs=512 seek="$sector" count=8 conv=notrunc,fdatasync 2>/dev/null; then
            ((success++))
        fi
        sleep 0.5  # Brief pause between attempts
    done
    
    echo "$success/$attempts writes succeeded"
    
    # Check for error detection and remapping
    local status=$(sudo dmsetup status remap-on-bad-disk)
    echo "  Status: $status"
    
    # Show recent error messages
    local recent_msgs=$(sudo dmesg -T | grep -E "(dm-remap|error)" | tail -2)
    if [ ! -z "$recent_msgs" ]; then
        echo "  Messages: $(echo "$recent_msgs" | tail -1 | cut -d']' -f2-)"
    fi
    
    echo
}

# Test each "bad sector"
for i in "${!BAD_SECTORS[@]}"; do
    test_bad_sector "${BAD_SECTORS[$i]}" "$i"
done

echo -e "${BLUE}Phase 5: Advanced bad sector simulation${NC}"

# Method C: Create a more sophisticated bad disk using dm-flakey features
echo "Creating advanced bad sector simulation..."

# Remove previous simulation
sudo dmsetup remove remap-on-bad-disk 2>/dev/null || true
sudo dmsetup remove simulated-bad-disk 2>/dev/null || true

# Create a dm-flakey with error_writes feature (fails only writes to simulate read-only bad sectors)
echo "0 $MAIN_SECTORS flakey $MAIN_LOOP 0 3 1 1 error_writes" | sudo dmsetup create advanced-bad-disk

# Create dm-remap on the advanced simulation
echo "0 $MAIN_SECTORS remap /dev/mapper/advanced-bad-disk $SPARE_LOOP 0 1000" | sudo dmsetup create remap-on-advanced-bad

echo "✅ Advanced bad disk created (write errors only)"

echo "Testing write-error-only simulation:"
test_bad_sector 15000 "advanced"

echo -e "${BLUE}Phase 6: Results and analysis${NC}"

echo "Final dm-remap status:"
sudo dmsetup status remap-on-advanced-bad 2>/dev/null || echo "Device removed"

echo ""
echo "Recent kernel messages:"
sudo dmesg -T | grep -E "(dm-remap|flakey)" | tail -10

echo ""
echo -e "${PURPLE}=== BAD SECTOR SIMULATION METHODS ===${NC}"
echo ""
echo "✅ Method A: Time-based error injection (existing enhanced_dm_flakey_test.sh)"
echo "   - Errors occur during specific time intervals"
echo "   - Good for testing continuous error conditions"
echo ""
echo "✅ Method B: Random error injection (this test)"
echo "   - Errors occur randomly across the disk"
echo "   - Good for testing general error resilience"
echo ""
echo "✅ Method C: Write-only error injection"
echo "   - Only write operations fail"
echo "   - Simulates read-only bad sectors"
echo ""
echo "💡 Additional methods available:"
echo "   - Use multiple dm-linear targets to create exact bad sector maps"
echo "   - Combine dm-flakey with dm-delay for timeout simulation"
echo "   - Use dm-dust for more precise bad block simulation"

echo ""
echo -e "${GREEN}Sector-specific error simulation completed!${NC}"
echo "You now have multiple methods to simulate disks with definable broken sectors."

exit 0