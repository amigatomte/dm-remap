#!/bin/bash
#
# corrected_remap_verification.sh - Properly test remapped sector access
#

set -e

echo "=== CORRECTED REMAP VERIFICATION TEST ==="

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove remap-verify 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/verify_main.img /tmp/verify_spare.img
    sudo rmmod dm_remap 2>/dev/null || true
}

trap cleanup EXIT

echo "Phase 1: Setup"
sudo insmod ../src/dm_remap.ko debug_level=1

# Create test devices - larger for better testing
dd if=/dev/zero of=/tmp/verify_main.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=/tmp/verify_spare.img bs=1M count=5 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/verify_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/verify_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Create dm-remap target"
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 100" | sudo dmsetup create remap-verify

echo "Phase 3: Write unique data patterns"
# Write unique data to main device at sector 2000
echo -n "MAIN_DEVICE_ORIGINAL_DATA_AT_SECTOR_2000" | sudo dd of=$MAIN_LOOP bs=512 seek=2000 count=1 conv=notrunc 2>/dev/null

# Write different data to spare device at sector 10 (where remapped data should go)
echo -n "SPARE_DEVICE_REMAP_TARGET_AT_SECTOR_10" | sudo dd of=$SPARE_LOOP bs=512 seek=10 count=1 conv=notrunc 2>/dev/null

echo "Phase 4: Verify original data before remapping"
echo "Reading sector 2000 from main device directly:"
MAIN_DIRECT=$(sudo dd if=$MAIN_LOOP bs=512 skip=2000 count=1 2>/dev/null | head -c 32)
echo "Main direct: '$MAIN_DIRECT'"

echo "Reading sector 2000 through dm-remap (should be same as main):"
DMREMAP_BEFORE=$(sudo dd if=/dev/mapper/remap-verify bs=512 skip=2000 count=1 2>/dev/null | head -c 32)
echo "dm-remap before: '$DMREMAP_BEFORE'"

echo "Phase 5: Create remap entry"
# Add remap: main sector 2000 -> spare sector 10
echo "remap_add 2000 10" | sudo tee /sys/kernel/dm-remap-global/targets/remap-verify/control

echo "Phase 6: Verify remapped access"
echo "Reading sector 2000 through dm-remap (should now read from spare sector 10):"
DMREMAP_AFTER=$(sudo dd if=/dev/mapper/remap-verify bs=512 skip=2000 count=1 2>/dev/null | head -c 32)
echo "dm-remap after: '$DMREMAP_AFTER'"

echo "For comparison, reading spare sector 10 directly:"
SPARE_DIRECT=$(sudo dd if=$SPARE_LOOP bs=512 skip=10 count=1 2>/dev/null | head -c 32)
echo "Spare direct: '$SPARE_DIRECT'"

echo "Phase 7: Analysis"
echo "Expected sequence:"
echo "  1. Before remap: dm-remap[2000] == main[2000] == '$MAIN_DIRECT'"
echo "  2. After remap:  dm-remap[2000] == spare[10]  == '$SPARE_DIRECT'"

echo ""
echo "Actual results:"
echo "  Main direct:      '$MAIN_DIRECT'"
echo "  dm-remap before:  '$DMREMAP_BEFORE'"
echo "  dm-remap after:   '$DMREMAP_AFTER'"
echo "  Spare direct:     '$SPARE_DIRECT'"

if [ "$MAIN_DIRECT" = "$DMREMAP_BEFORE" ] && [ "$SPARE_DIRECT" = "$DMREMAP_AFTER" ]; then
    echo ""
    echo "✅ REMAP VERIFICATION SUCCESSFUL!"
    echo "   - Before remap: dm-remap correctly accessed main device"
    echo "   - After remap:  dm-remap correctly accessed spare device"
    echo "   - Remapped sectors DO access the correct physical locations"
else
    echo ""
    echo "❌ REMAP VERIFICATION FAILED!"
    if [ "$MAIN_DIRECT" != "$DMREMAP_BEFORE" ]; then
        echo "   - Before remap: dm-remap did not match main device"
    fi
    if [ "$SPARE_DIRECT" != "$DMREMAP_AFTER" ]; then
        echo "   - After remap: dm-remap did not match spare device"
    fi
fi

echo ""
echo "Test completed."