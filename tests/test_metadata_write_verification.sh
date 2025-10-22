#!/bin/bash
#
# Test to verify metadata is actually written to spare device
# This test uses dm-error on the MAIN device to trigger remapping
#
set -euo pipefail

echo "=================================================================="
echo "  dm-remap Metadata Write Verification Test"
echo "=================================================================="
echo "Date: $(date)"
echo ""
echo "This test verifies that:"
echo "  1. Remaps are created when main device has errors"
echo "  2. Metadata is marked as dirty"
echo "  3. Metadata is written to spare device on device removal"
echo "  4. Spare device contains the metadata signature"
echo "=================================================================="
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    sudo dmsetup remove dm-remap-test 2>/dev/null || true
    sudo dmsetup remove dm-main-with-errors 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/main_test.img /tmp/spare_test.img
    echo "Cleanup complete"
}

trap cleanup EXIT

# Initial cleanup
cleanup 2>/dev/null || true

echo "Step 1: Load dm-remap modules"
echo "=============================="
cd "$(dirname "$0")/.."

if ! lsmod | grep -q dm_remap_v4_stats; then
    sudo insmod src/dm-remap-v4-stats.ko
    echo "✓ Loaded dm-remap-v4-stats"
else
    echo "✓ dm-remap-v4-stats already loaded"
fi

if ! lsmod | grep -q dm_remap_v4_real; then
    sudo insmod src/dm-remap-v4-real.ko
    echo "✓ Loaded dm-remap-v4-real"
else
    echo "✓ dm-remap-v4-real already loaded"
fi
echo ""

echo "Step 2: Create test images"
echo "=========================="
dd if=/dev/zero of=/tmp/main_test.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/spare_test.img bs=1M count=100 2>/dev/null
echo "✓ Created 100MB test images"
echo ""

echo "Step 3: Setup loop devices"
echo "=========================="
sudo losetup /dev/loop20 /tmp/main_test.img
sudo losetup /dev/loop21 /tmp/spare_test.img
MAIN_SECTORS=$(sudo blockdev --getsz /dev/loop20)
echo "✓ Loop devices attached"
echo "  Main: /dev/loop20 ($MAIN_SECTORS sectors)"
echo "  Spare: /dev/loop21"
echo ""

echo "Step 4: Define bad sectors on main device"
echo "=========================================="
# We'll inject errors at sectors 1000, 2000, 3000
BAD_SECTORS=(1000 2000 3000)
echo "Bad sectors: ${BAD_SECTORS[@]}"
echo ""

echo "Step 5: Create dm-linear + dm-error for main device"
echo "==================================================="
# Build table with errors at specific sectors
TABLE=""
CURRENT=0

for BAD in "${BAD_SECTORS[@]}"; do
    if [ $CURRENT -lt $BAD ]; then
        SIZE=$((BAD - CURRENT))
        TABLE+="$CURRENT $SIZE linear /dev/loop20 $CURRENT"$'\n'
    fi
    TABLE+="$BAD 1 error"$'\n'
    CURRENT=$((BAD + 1))
done

# Add final segment
if [ $CURRENT -lt $MAIN_SECTORS ]; then
    SIZE=$((MAIN_SECTORS - CURRENT))
    TABLE+="$CURRENT $SIZE linear /dev/loop20 $CURRENT"$'\n'
fi

echo -e "$TABLE" | sudo dmsetup create dm-main-with-errors
echo "✓ Created dm-main-with-errors with ${#BAD_SECTORS[@]} bad sectors"
echo ""

echo "Step 6: Check spare device BEFORE (should be all zeros)"
echo "========================================================"
echo "First 512 bytes of spare device:"
sudo hexdump -C /tmp/spare_test.img -n 512 | head -5
ZEROS_BEFORE=$(sudo hexdump -C /tmp/spare_test.img -n 512 | grep -c "00 00 00 00 00 00 00 00" || true)
echo "Lines of zeros: $ZEROS_BEFORE/32 (should be 32)"
echo ""

echo "Step 7: Create dm-remap device"
echo "==============================="
# Clear kernel log
sudo dmesg -C

# Create dm-remap with error-prone main device and clean spare
echo "0 $MAIN_SECTORS dm-remap-v4 /dev/mapper/dm-main-with-errors /dev/loop21" | \
    sudo dmsetup create dm-remap-test

echo "✓ Created dm-remap-test"
echo ""

echo "Step 8: Initial device status"
echo "============================="
sudo dmsetup status dm-remap-test
echo ""

echo "Step 9: Trigger errors by reading bad sectors"
echo "=============================================="
echo "This will cause dm-remap to create remaps..."
echo ""

for sector in "${BAD_SECTORS[@]}"; do
    echo -n "Reading sector $sector (bad): "
    # First read will fail and trigger remap
    if sudo dd if=/dev/mapper/dm-remap-test of=/dev/null \
        bs=512 skip=$sector count=1 iflag=direct 2>&1 | grep -q "error"; then
        echo "✗ Failed (expected)"
    else
        echo "✓ Success (remapped?)"
    fi
    sleep 0.2  # Give time for error handling
done
echo ""

echo "Step 10: Check kernel log for remap messages"
echo "============================================"
echo "Recent dm-remap messages:"
sudo dmesg | grep -i "dm-remap" | tail -20
echo ""

# Count remaps from kernel log
REMAP_COUNT=$(sudo dmesg | grep -c "Added remap entry" || echo "0")
echo "Remaps created: $REMAP_COUNT"
echo ""

echo "Step 11: Device status after remapping"
echo "======================================"
sudo dmsetup status dm-remap-test
echo ""

echo "Step 12: Remove device (triggers metadata write)"
echo "================================================"
if [ "$REMAP_COUNT" -gt 0 ]; then
    echo "⚠️  Metadata should be written because remaps exist"
else
    echo "⚠️  No remaps created, metadata won't be written"
fi
echo ""

echo "Removing device..."
sudo dmsetup remove dm-remap-test
echo "✓ Device removed"
echo ""

echo "Step 13: Check kernel log for metadata write message"
echo "===================================================="
echo "Looking for 'Writing final metadata' message..."
if sudo dmesg | grep -q "Writing final metadata on device shutdown"; then
    echo "✅ FOUND: Metadata write message in kernel log!"
else
    echo "❌ NOT FOUND: No metadata write message"
fi
echo ""
echo "Recent messages:"
sudo dmesg | tail -15 | grep -i "metadata"
echo ""

echo "Step 14: Check spare device AFTER (should have metadata)"
echo "========================================================"
echo "First 512 bytes of spare device:"
sudo hexdump -C /tmp/spare_test.img -n 512 | head -10
echo ""

# Check for metadata signature
if sudo hexdump -C /tmp/spare_test.img -n 16 | grep -q "44 4d 52 34"; then
    echo "✅ FOUND: Metadata signature 'DMR4' (0x444D5234)!"
elif sudo hexdump -C /tmp/spare_test.img -n 16 | grep -q "52 45 4d 41"; then
    echo "✅ FOUND: Metadata signature 'REMA' (different format)!"
else
    echo "⚠️  No recognizable metadata signature found"
    echo "   Checking if device is still all zeros..."
    ZEROS_AFTER=$(sudo hexdump -C /tmp/spare_test.img -n 512 | grep -c "00 00 00 00 00 00 00 00" || true)
    if [ "$ZEROS_AFTER" -eq 32 ]; then
        echo "❌ Spare device is still all zeros - metadata NOT written!"
    else
        echo "✓ Spare device has changed - some data was written"
    fi
fi
echo ""

echo "=================================================================="
echo "                     TEST RESULTS"
echo "=================================================================="
echo "Bad sectors defined: ${#BAD_SECTORS[@]}"
echo "Remaps created: $REMAP_COUNT"
echo ""

if [ "$REMAP_COUNT" -eq 0 ]; then
    echo "❌ ISSUE: No remaps were created"
    echo "   Possible causes:"
    echo "   - dm-error not triggering properly"
    echo "   - Error handling not working"
    echo "   - Need different approach to trigger errors"
elif sudo dmesg | grep -q "Writing final metadata"; then
    echo "✅ SUCCESS: Metadata write was triggered!"
    echo ""
    # Check if spare actually changed
    ZEROS_AFTER=$(sudo hexdump -C /tmp/spare_test.img -n 512 | grep -c "00 00 00 00 00 00 00 00" || true)
    if [ "$ZEROS_AFTER" -lt 32 ]; then
        echo "✅ VERIFIED: Spare device contains data!"
        echo "   Next step: Verify metadata can be read back"
    else
        echo "⚠️  WARNING: Metadata write logged but spare still zeros"
        echo "   Possible issue with write function"
    fi
else
    echo "⚠️  PARTIAL: Remaps created but no metadata write message"
    echo "   Checking if metadata_dirty flag issue..."
fi

echo "=================================================================="
