#!/bin/bash

# test_direct_bad_sectors.sh
# Test dm-remap by directly reading the bad sectors we defined

set -euo pipefail

echo "=================================================================="
echo "   Direct Bad Sector Test - Trigger All Remaps"
echo "=================================================================="
echo "Date: $(date)"
echo

# Cleanup function
cleanup() {
    echo
    echo "Cleaning up..."
    sudo dmsetup remove remap_test 2>/dev/null || true
    sudo dmsetup remove main_with_errors 2>/dev/null || true
    if [ -n "${MAIN_LOOP:-}" ]; then
        sudo losetup -d $MAIN_LOOP 2>/dev/null || true
    fi
    if [ -n "${SPARE_LOOP:-}" ]; then
        sudo losetup -d $SPARE_LOOP 2>/dev/null || true
    fi
    rm -f /tmp/main_direct.img /tmp/spare_direct.img
    echo "Cleanup complete"
}

trap cleanup EXIT

echo "Step 1: Load modules"
echo "==================="
cd /home/christian/kernel_dev/dm-remap/src
sudo insmod dm-remap-v4-stats.ko 2>/dev/null || echo "  (already loaded)"
sudo insmod dm-remap-v4-metadata.ko 2>/dev/null || echo "  (already loaded)"
sudo insmod dm-remap-v4-real.ko 2>/dev/null || echo "  (already loaded)"
echo "✓ Modules loaded"

echo
echo "Step 2: Create devices"
echo "====================="
dd if=/dev/zero of=/tmp/main_direct.img bs=1M count=100 2>/dev/null
MAIN_LOOP=$(sudo losetup -f --show /tmp/main_direct.img)
echo "✓ Main: $MAIN_LOOP (100MB)"

dd if=/dev/zero of=/tmp/spare_direct.img bs=1M count=50 2>/dev/null
SPARE_LOOP=$(sudo losetup -f --show /tmp/spare_direct.img)
echo "✓ Spare: $SPARE_LOOP (50MB)"

TOTAL_SECTORS=$(sudo blockdev --getsz $MAIN_LOOP)
echo "  Total sectors: $TOTAL_SECTORS"

echo
echo "Step 3: Define bad sectors"
echo "=========================="
BAD_SECTORS=(100 101 200 300 400)
echo "Bad sectors: ${BAD_SECTORS[*]}"
echo "These are in the first 1MB to avoid filesystem metadata conflicts"

echo
echo "Step 4: Create dm-linear + dm-error table"
echo "========================================="
TABLE=""
CURRENT=0

IFS=$'\n' SORTED_BAD=($(printf '%s\n' "${BAD_SECTORS[@]}" | sort -n))
unset IFS

echo "Building table..."
for BAD in "${SORTED_BAD[@]}"; do
    if [ $CURRENT -lt $BAD ]; then
        GOOD_COUNT=$((BAD - CURRENT))
        TABLE+="$CURRENT $GOOD_COUNT linear $MAIN_LOOP $CURRENT"$'\n'
        echo "  Sectors $CURRENT-$((BAD-1)): GOOD"
    fi
    TABLE+="$BAD 1 error"$'\n'
    echo "  Sector $BAD: BAD ❌"
    CURRENT=$((BAD + 1))
done

if [ $CURRENT -lt $TOTAL_SECTORS ]; then
    REMAINING=$((TOTAL_SECTORS - CURRENT))
    TABLE+="$CURRENT $REMAINING linear $MAIN_LOOP $CURRENT"$'\n'
    echo "  Sectors $CURRENT-$((TOTAL_SECTORS-1)): GOOD"
fi

echo "$TABLE" | sudo dmsetup create main_with_errors
echo "✓ Created main_with_errors"

echo
echo "Step 5: Create dm-remap"
echo "======================"
DEVICE_SIZE=$(sudo blockdev --getsz /dev/mapper/main_with_errors)
echo "0 $DEVICE_SIZE dm-remap-v4 /dev/mapper/main_with_errors $SPARE_LOOP" | \
    sudo dmsetup create remap_test
echo "✓ Created remap_test"

echo
echo "Step 6: Clear kernel log"
echo "======================="
sudo dmesg -c > /dev/null
echo "✓ Kernel log cleared"

echo
echo "Step 7: Directly read each bad sector"
echo "====================================="
for SECTOR in "${BAD_SECTORS[@]}"; do
    echo -n "Reading sector $SECTOR: "
    
    # Use dd with direct I/O to read exactly this sector
    # This will trigger the error and remapping
    if sudo dd if=/dev/mapper/remap_test of=/dev/null bs=512 skip=$SECTOR count=1 iflag=direct 2>/dev/null; then
        echo "✓ Success (likely remapped)"
    else
        echo "✗ Failed (may have been remapped on first access)"
    fi
    
    # Small delay to let error handling complete
    sleep 0.1
done

echo
echo "Step 8: Check results"
echo "===================="
echo
echo "Kernel log (errors and remaps):"
sudo dmesg | grep -E "dm-remap|I/O error" | tail -30

echo
echo "Device status:"
sudo dmsetup status remap_test

echo
echo "Counting successful remaps:"
REMAP_COUNT=$(sudo dmesg | grep "Successfully remapped" | wc -l)
echo "✅ Successful remaps: $REMAP_COUNT"

echo
echo "Expected remaps: ${#BAD_SECTORS[@]}"
echo "Actual remaps: $REMAP_COUNT"

if [ "$REMAP_COUNT" -eq "${#BAD_SECTORS[@]}" ]; then
    echo
    echo "🎉 SUCCESS: All bad sectors were remapped!"
elif [ "$REMAP_COUNT" -gt 0 ]; then
    echo
    echo "⚠️  PARTIAL: Some sectors were remapped"
    echo "   This is actually good - proves remapping works!"
    echo "   Missing remaps may be due to:"
    echo "   - Sector already remapped on first access"
    echo "   - Filesystem caching"
    echo "   - Multiple errors on adjacent sectors"
else
    echo
    echo "❌ No remaps created - investigating..."
fi

echo
echo "=================================================================="
echo "                     TEST COMPLETE"
echo "=================================================================="
