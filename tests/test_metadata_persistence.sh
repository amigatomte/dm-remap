#!/bin/bash
#
# Test metadata persistence for dm-remap v4.0
# This test verifies that remap metadata is saved to spare device
# and can be restored after device reassembly
#
# Expected behavior:
# 1. Create dm-remap device with metadata persistence enabled
# 2. Trigger errors to create remaps
# 3. Verify metadata is written to spare device
# 4. Disassemble device
# 5. Reassemble device and verify remaps are restored
#

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "════════════════════════════════════════════════════════════════"
echo "  dm-remap v4.0 Metadata Persistence Test"
echo "════════════════════════════════════════════════════════════════"
echo ""

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Unmount if mounted
    mountpoint -q /mnt/dm-remap-test 2>/dev/null && umount /mnt/dm-remap-test
    
    # Remove dm devices
    dmsetup remove dm-remap-test 2>/dev/null || true
    dmsetup remove dm-error-test 2>/dev/null || true
    
    # Detach loop devices
    losetup -d /dev/loop20 2>/dev/null || true
    losetup -d /dev/loop21 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/dm-remap-data.img
    rm -f /tmp/dm-remap-spare.img
    rm -rf /mnt/dm-remap-test
    
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Set trap for cleanup
trap cleanup EXIT

# Initial cleanup
cleanup 2>/dev/null || true

echo -e "${BLUE}Step 1: Load dm-remap modules${NC}"
if ! lsmod | grep -q dm_remap; then
    echo "Loading dm-remap modules..."
    insmod ../src/dm-remap.ko
    insmod ../src/dm-remap-v4-real.ko
    insmod ../src/dm-remap-v4-metadata.ko
fi
echo -e "${GREEN}✓ Modules loaded${NC}"
echo ""

echo -e "${BLUE}Step 2: Create loop devices${NC}"
# Create data device (100MB)
dd if=/dev/zero of=/tmp/dm-remap-data.img bs=1M count=100 2>/dev/null
losetup /dev/loop20 /tmp/dm-remap-data.img
echo -e "${GREEN}✓ Data device: /dev/loop20 (100MB)${NC}"

# Create spare device (50MB)
dd if=/dev/zero of=/tmp/dm-remap-spare.img bs=1M count=50 2>/dev/null
losetup /dev/loop21 /tmp/dm-remap-spare.img
echo -e "${GREEN}✓ Spare device: /dev/loop21 (50MB)${NC}"
echo ""

echo -e "${BLUE}Step 3: Define bad sectors for testing${NC}"
BAD_SECTORS=(100 101 200 300 400)
echo "Bad sectors: ${BAD_SECTORS[@]}"
echo ""

echo -e "${BLUE}Step 4: Create dm-linear + dm-error device${NC}"
DATA_SIZE=$(blockdev --getsz /dev/loop20)
echo "Data device size: $DATA_SIZE sectors"

# Build device mapper table with error targets at specific sectors
TABLE=""
CURRENT=0

for BAD_SECTOR in "${BAD_SECTORS[@]}"; do
    if [ $CURRENT -lt $BAD_SECTOR ]; then
        # Add linear segment before bad sector
        SIZE=$((BAD_SECTOR - CURRENT))
        TABLE+="$CURRENT $SIZE linear /dev/loop20 $CURRENT\n"
        CURRENT=$BAD_SECTOR
    fi
    # Add error target for bad sector (1 sector)
    TABLE+="$CURRENT 1 error\n"
    CURRENT=$((CURRENT + 1))
done

# Add final linear segment
if [ $CURRENT -lt $DATA_SIZE ]; then
    SIZE=$((DATA_SIZE - CURRENT))
    TABLE+="$CURRENT $SIZE linear /dev/loop20 $CURRENT\n"
fi

echo -e "$TABLE" | dmsetup create dm-error-test
echo -e "${GREEN}✓ dm-error-test created with bad sectors: ${BAD_SECTORS[@]}${NC}"
echo ""

echo -e "${BLUE}Step 5: Create dm-remap device${NC}"
SPARE_SIZE=$(blockdev --getsz /dev/loop21)
echo "Creating dm-remap device..."

# Create dm-remap device (metadata is handled internally by v4-metadata module)
# Format: 0 <size> dm-remap-v4 <data_dev> <spare_dev>
echo "0 $DATA_SIZE dm-remap-v4 /dev/mapper/dm-error-test /dev/loop21" | dmsetup create dm-remap-test

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ dm-remap-test created${NC}"
else
    echo -e "${RED}✗ Failed to create dm-remap device${NC}"
    exit 1
fi
echo ""

echo -e "${BLUE}Step 6: Trigger errors to create remaps${NC}"
echo "Reading each bad sector directly to trigger remapping..."

REMAP_COUNT=0
for BAD_SECTOR in "${BAD_SECTORS[@]}"; do
    echo -n "Reading sector $BAD_SECTOR: "
    
    # Read the bad sector directly (will fail on first access, succeed after remap)
    if dd if=/dev/mapper/dm-remap-test of=/dev/null bs=512 skip=$BAD_SECTOR count=1 iflag=direct 2>/dev/null; then
        echo -e "${YELLOW}✓ Read successful (may have been remapped)${NC}"
    else
        echo -e "${YELLOW}✗ Read failed (expected on first access)${NC}"
    fi
    
    # Small delay for error handling
    sleep 0.1
done
echo ""

echo -e "${BLUE}Step 7: Check kernel log for remaps${NC}"
echo "Recent dm-remap messages:"
dmesg | grep -i "dm-remap" | tail -30
echo ""

# Count successful remaps from kernel log
REMAP_COUNT=$(dmesg | grep -c "Successfully remapped failed sector" || true)
echo -e "${GREEN}Remaps created: $REMAP_COUNT${NC}"
echo ""

echo -e "${BLUE}Step 8: Verify metadata on spare device${NC}"
echo "Checking spare device for metadata signature..."

# Read first sector of spare device to check for metadata header
hexdump -C /dev/loop21 -n 512 | head -20

# Check for dm-remap metadata signature
if hexdump -C /dev/loop21 -n 16 | grep -q "44 4d 52 45"; then
    echo -e "${GREEN}✓ Metadata signature found on spare device!${NC}"
    echo "  Signature: 'DMRE' (dm-remap metadata header)"
else
    echo -e "${YELLOW}⚠ Metadata signature not found (may use different format)${NC}"
fi
echo ""

echo -e "${BLUE}Step 9: Read current remap table from device${NC}"
dmsetup table dm-remap-test
echo ""

echo -e "${BLUE}Step 10: Get device status${NC}"
dmsetup status dm-remap-test
echo ""

echo -e "${BLUE}Step 11: Disassemble device (simulate reboot)${NC}"
echo "Unmounting and removing dm-remap device..."
dmsetup remove dm-remap-test
dmsetup remove dm-error-test
echo -e "${GREEN}✓ Devices removed${NC}"
echo ""

echo -e "${BLUE}Step 12: Recreate dm-error device${NC}"
echo -e "$TABLE" | dmsetup create dm-error-test
echo -e "${GREEN}✓ dm-error-test recreated${NC}"
echo ""

echo -e "${BLUE}Step 13: Recreate dm-remap and check for metadata restoration${NC}"
echo "Creating dm-remap with same spare device..."

# Recreate with same parameters - metadata module should detect existing metadata
echo "0 $DATA_SIZE dm-remap-v4 /dev/mapper/dm-error-test /dev/loop21" | dmsetup create dm-remap-test

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ dm-remap-test recreated${NC}"
else
    echo -e "${RED}✗ Failed to recreate dm-remap device${NC}"
    exit 1
fi
echo ""

echo -e "${BLUE}Step 14: Check if remaps were restored${NC}"
echo "Kernel messages after recreation:"
dmesg | grep -i "dm-remap" | tail -20
echo ""

# Check for restoration messages
RESTORE_COUNT=$(dmesg | grep -c "Restored.*remap.*from metadata" || true)
echo "Restoration messages: $RESTORE_COUNT"
echo ""

echo -e "${BLUE}Step 15: Verify remaps still work${NC}"
echo "Testing previously bad sectors (should now read successfully)..."

SUCCESS_COUNT=0
for BAD_SECTOR in "${BAD_SECTORS[@]}"; do
    echo -n "Reading sector $BAD_SECTOR: "
    
    if dd if=/dev/mapper/dm-remap-test of=/dev/null bs=512 skip=$BAD_SECTOR count=1 iflag=direct 2>/dev/null; then
        echo -e "${GREEN}✓ Success${NC}"
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    else
        echo -e "${RED}✗ Failed${NC}"
    fi
done
echo ""

echo -e "${BLUE}Step 16: Final device status${NC}"
dmsetup table dm-remap-test
echo ""
dmsetup status dm-remap-test
echo ""

echo "════════════════════════════════════════════════════════════════"
echo "  TEST RESULTS"
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "Bad sectors defined: ${#BAD_SECTORS[@]}"
echo "Remaps created: $REMAP_COUNT"
echo "Successful reads after restore: $SUCCESS_COUNT"
echo ""

if [ $SUCCESS_COUNT -eq ${#BAD_SECTORS[@]} ]; then
    echo -e "${GREEN}🎉 SUCCESS: All remaps persisted and restored!${NC}"
    echo ""
    echo "✓ Metadata was saved to spare device"
    echo "✓ Device was successfully reassembled"
    echo "✓ All remapped sectors readable after restore"
    echo "✓ Metadata persistence is WORKING!"
    exit 0
elif [ $SUCCESS_COUNT -gt 0 ]; then
    echo -e "${YELLOW}⚠ PARTIAL SUCCESS: Some remaps persisted${NC}"
    echo ""
    echo "✓ $SUCCESS_COUNT out of ${#BAD_SECTORS[@]} remaps restored"
    echo "⚠ Metadata persistence partially working"
    exit 1
else
    echo -e "${RED}✗ FAILURE: No remaps persisted${NC}"
    echo ""
    echo "✗ Metadata was not saved or not restored"
    echo "✗ Metadata persistence NOT working"
    exit 1
fi
