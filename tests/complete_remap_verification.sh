#!/bin/bash
#
# complete_remap_verification.sh - Complete test of actual sector remapping
#

set -e

echo "=== COMPLETE REMAP VERIFICATION TEST ==="

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove complete-remap 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true  
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/complete_main.img /tmp/complete_spare.img
    sudo rmmod dm_remap 2>/dev/null || true
}

trap cleanup EXIT

echo "Phase 1: Setup with debug interface"
sudo insmod ../src/dm_remap.ko debug_level=2

# Create test devices
dd if=/dev/zero of=/tmp/complete_main.img bs=1M count=2 2>/dev/null
dd if=/dev/zero of=/tmp/complete_spare.img bs=1M count=1 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/complete_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/complete_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Write DIFFERENT test data"
# Write unique data to specific sectors
echo -n "MAIN_DATA_AT_SECTOR_1000_ORIGINAL" | sudo dd of=$MAIN_LOOP bs=512 seek=1000 count=1 conv=notrunc 2>/dev/null
echo -n "SPARE_DATA_AT_SECTOR_20_TARGET" | sudo dd of=$SPARE_LOOP bs=512 seek=20 count=1 conv=notrunc 2>/dev/null

echo "Phase 3: Create dm-remap target"
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create complete-remap

echo "Phase 4: Verify BEFORE remapping"
echo "Before remapping - reading sector 1000 through dm-remap should show MAIN data:"
sudo hexdump -C /dev/mapper/complete-remap -s $((1000 * 512)) -n 32 | head -1

echo "Spare device sector 20 contains (for reference):"
sudo hexdump -C $SPARE_LOOP -s $((20 * 512)) -n 32 | head -1

echo "Phase 5: Check if debug interface is available"
if [ -d /sys/kernel/debug/dm-remap ]; then
    echo "✅ Debug interface found at /sys/kernel/debug/dm-remap"
    ls -la /sys/kernel/debug/dm-remap/
else
    echo "❌ Debug interface not found"
    echo "Available debug paths:"
    find /sys/kernel/debug -name "*remap*" 2>/dev/null || echo "No remap debug entries found"
    echo "Checking if debugfs is mounted:"
    mount | grep debugfs || echo "debugfs not mounted"
fi

echo "Phase 6: Create manual remap entry"
if [ -f /sys/kernel/debug/dm-remap/remap_control ]; then
    echo "Adding remap: sector 1000 -> spare sector 20"
    echo "add 1000 20" | sudo tee /sys/kernel/debug/dm-remap/remap_control
    
    echo "List current remaps:"
    echo "list" | sudo tee /sys/kernel/debug/dm-remap/remap_control
    
    echo "Phase 7: Verify AFTER remapping"
    echo "After remapping - reading sector 1000 should now show SPARE data:"
    sudo hexdump -C /dev/mapper/complete-remap -s $((1000 * 512)) -n 32 | head -1
    
    echo "Phase 8: Final verification"
    echo ""
    echo "EXPECTED BEHAVIOR:"
    echo "  Before remap: sector 1000 -> 'MAIN_DATA_AT_SECTOR_1000_ORIGINAL'"
    echo "  After remap:  sector 1000 -> 'SPARE_DATA_AT_SECTOR_20_TARGET'"
    echo ""
    echo "If the data changed from MAIN to SPARE, remapping works correctly!"
    
else
    echo "❌ Debug control file not found"
    echo "Cannot create remap entries for testing"
    echo ""
    echo "MANUAL VERIFICATION NEEDED:"
    echo "The basic I/O forwarding works (confirmed in previous tests)"
    echo "But we need the debug interface to test actual remapping"
fi

echo ""
echo "Test completed."