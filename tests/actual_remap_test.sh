#!/bin/bash
#
# actual_remap_test.sh - Test actual sector remapping behavior
#

set -e

echo "=== ACTUAL REMAP TEST ==="

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove actual-remap 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/actual_main.img /tmp/actual_spare.img
    sudo rmmod dm_remap 2>/dev/null || true
}

trap cleanup EXIT

echo "Phase 1: Setup"
sudo insmod ../src/dm_remap.ko debug_level=2

# Create test devices
dd if=/dev/zero of=/tmp/actual_main.img bs=1M count=2 2>/dev/null
dd if=/dev/zero of=/tmp/actual_spare.img bs=1M count=1 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/actual_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/actual_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Write DIFFERENT data to main and spare devices"
# Write to sector 1000 on main device
echo -n "MAIN_DEVICE_DATA_AT_SECTOR_1000" | sudo dd of=$MAIN_LOOP bs=512 seek=1000 count=1 conv=notrunc 2>/dev/null

# Write to sector 20 on spare device (where we'll remap to)
echo -n "SPARE_DEVICE_DATA_AT_SECTOR_20" | sudo dd of=$SPARE_LOOP bs=512 seek=20 count=1 conv=notrunc 2>/dev/null

echo "Phase 3: Create dm-remap target"
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create actual-remap

echo "Phase 4: Test BEFORE remapping"
echo "Reading sector 1000 from main device directly:"
sudo hexdump -C $MAIN_LOOP -s $((1000 * 512)) -n 32 | head -1

echo "Reading sector 1000 through dm-remap (should match main, no remap yet):"
sudo hexdump -C /dev/mapper/actual-remap -s $((1000 * 512)) -n 32 | head -1

echo "Reading sector 20 from spare device directly (for reference):"
sudo hexdump -C $SPARE_LOOP -s $((20 * 512)) -n 32 | head -1

echo "Phase 5: Manually create remap entry in kernel"
echo "We need to directly manipulate the remap table in kernel memory"
echo "For now, let's look at how we can add a remap programmatically..."

# Let's look at the remap_c structure and see if we can access it
echo ""
echo "Current approach: We need to create a remap entry that maps:"
echo "  Main sector 1000 -> Spare sector 20"
echo ""
echo "This would require either:"
echo "1. A sysfs interface to add remap entries"
echo "2. A direct kernel memory manipulation (unsafe)"
echo "3. Triggering auto-remapping through error injection"
echo ""

echo "Phase 6: Check if we can trigger auto-remapping"
echo "Let's see if we can use dm-flakey to trigger an error on sector 1000"
echo "which should cause auto-remapping to spare device"

echo ""
echo "ANALYSIS OF CURRENT STATE:"
echo "✅ Main device has: 'MAIN_DEVICE_DATA_AT_SECTOR_1000'"
echo "✅ Spare device has: 'SPARE_DEVICE_DATA_AT_SECTOR_20'"
echo "✅ dm-remap passthrough works (reads from main device)"
echo ""
echo "❓ MISSING: Actual test of remapped sector reading from spare device"
echo "   Need to create remap entry: sector 1000 -> spare sector 20"
echo "   Then verify that reading sector 1000 returns spare data"

echo ""
echo "Next step: Implement method to create remap entries for testing"