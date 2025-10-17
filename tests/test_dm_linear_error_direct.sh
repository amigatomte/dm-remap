#!/bin/bash

# test_dm_linear_error_direct.sh
# Force all bad sectors to be hit using direct I/O (no filesystem)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=================================================================="
echo "   dm-remap v4 Testing - Direct I/O to Force All Remaps"
echo "=================================================================="
echo "Date: $(date)"
echo
echo "This test uses DIRECT I/O to guarantee hitting all bad sectors"
echo "Unlike filesystem tests, this FORCES errors on each bad sector"
echo "=================================================================="
echo

# Cleanup function
cleanup() {
    echo
    echo "Cleaning up..."
    sudo dmsetup remove remap_direct 2>/dev/null || true
    sudo dmsetup remove main_direct_errors 2>/dev/null || true
    if [ -n "$MAIN_LOOP" ]; then
        sudo losetup -d $MAIN_LOOP 2>/dev/null || true
    fi
    if [ -n "$SPARE_LOOP" ]; then
        sudo losetup -d $SPARE_LOOP 2>/dev/null || true
    fi
    rm -f /tmp/main_direct.img /tmp/spare_direct.img
    echo "Cleanup complete"
}

trap cleanup EXIT

echo "Step 1: Load dm-remap modules"
echo "============================="
cd "$PROJECT_ROOT/src"

sudo insmod dm-remap-v4-stats.ko 2>/dev/null || echo "  (stats already loaded)"
sudo insmod dm-remap-v4-metadata.ko 2>/dev/null || echo "  (metadata already loaded)"
sudo insmod dm-remap-v4-real.ko 2>/dev/null || echo "  (real already loaded)"

echo "✓ Modules loaded"

echo
echo "Step 2: Create loop devices"
echo "=========================="
rm -f /tmp/main_direct.img /tmp/spare_direct.img

dd if=/dev/zero of=/tmp/main_direct.img bs=1M count=100 2>/dev/null
MAIN_LOOP=$(sudo losetup -f --show /tmp/main_direct.img)
echo "✓ Created main device: $MAIN_LOOP (100MB)"

dd if=/dev/zero of=/tmp/spare_direct.img bs=1M count=50 2>/dev/null
SPARE_LOOP=$(sudo losetup -f --show /tmp/spare_direct.img)
echo "✓ Created spare device: $SPARE_LOOP (50MB)"

TOTAL_SECTORS=$(sudo blockdev --getsz $MAIN_LOOP)
echo "  Total sectors: $TOTAL_SECTORS"

echo
echo "Step 3: Define precise bad sectors"
echo "=================================="
BAD_SECTORS=(50000 50001 75000 100000 150000)
echo "Bad sectors: ${BAD_SECTORS[*]}"

echo
echo "Step 4: Create dm-linear + dm-error table"
echo "========================================="

TABLE=""
CURRENT=0

IFS=$'\n' SORTED_BAD=($(printf '%s\n' "${BAD_SECTORS[@]}" | sort -n))
unset IFS

echo "Building precision bad sector map..."
for BAD in "${SORTED_BAD[@]}"; do
    if [ $CURRENT -lt $BAD ]; then
        GOOD_COUNT=$((BAD - CURRENT))
        TABLE+="$CURRENT $GOOD_COUNT linear $MAIN_LOOP $CURRENT"$'\n'
        echo "  Sectors $CURRENT-$((BAD-1)): GOOD (linear)"
    fi
    
    TABLE+="$BAD 1 error"$'\n'
    echo "  Sector $BAD: BAD (error)"
    
    CURRENT=$((BAD + 1))
done

if [ $CURRENT -lt $TOTAL_SECTORS ]; then
    REMAINING=$((TOTAL_SECTORS - CURRENT))
    TABLE+="$CURRENT $REMAINING linear $MAIN_LOOP $CURRENT"$'\n'
    echo "  Sectors $CURRENT-$((TOTAL_SECTORS-1)): GOOD (linear)"
fi

echo "$TABLE" | sudo dmsetup create main_direct_errors

echo "✓ Created main_direct_errors with ${#BAD_SECTORS[@]} bad sectors"

echo
echo "Step 5: Create dm-remap on top"
echo "=============================="
DEVICE_SIZE=$(sudo blockdev --getsz /dev/mapper/main_direct_errors)
echo "0 $DEVICE_SIZE dm-remap-v4 /dev/mapper/main_direct_errors $SPARE_LOOP" | \
    sudo dmsetup create remap_direct

echo "✓ Created remap_direct: dm-remap-v4 → main_direct_errors → $MAIN_LOOP"
echo "  Stack: dm-remap → (dm-linear+dm-error) → loop device"

echo
echo "Step 6: Write good data to non-bad sectors first"
echo "================================================"
echo "Writing test pattern to good sectors..."
# Write a known pattern to some good sectors
for sector in 10000 20000 30000 40000 60000; do
    echo "Writing to good sector $sector..."
    sudo dd if=/dev/urandom of=/dev/mapper/remap_direct \
        bs=512 seek=$sector count=1 oflag=direct 2>/dev/null
done
echo "✓ Test data written to 5 good sectors"

echo
echo "Step 7: FORCE errors by writing directly to bad sectors"
echo "======================================================="
echo "This bypasses filesystem and guarantees hitting each bad sector!"
echo

REMAP_COUNT_BEFORE=$(sudo dmesg | grep "Successfully remapped" | wc -l)

for sector in "${BAD_SECTORS[@]}"; do
    echo -n "Forcing I/O to bad sector $sector... "
    # Try to write - this WILL fail and trigger remap
    if sudo dd if=/dev/urandom of=/dev/mapper/remap_direct \
        bs=512 seek=$sector count=1 oflag=direct 2>&1 | grep -q "error"; then
        echo "✅ Error triggered (as expected)"
    else
        # Try to read instead
        if sudo dd if=/dev/mapper/remap_direct of=/dev/null \
            bs=512 skip=$sector count=1 iflag=direct 2>&1 | grep -q "error"; then
            echo "✅ Error triggered on read"
        else
            echo "⚠️  No error (unexpected)"
        fi
    fi
    # Small delay to let dm-remap process
    sleep 0.1
done

echo
echo "Step 8: Check how many remaps were created"
echo "=========================================="
REMAP_COUNT_AFTER=$(sudo dmesg | grep "Successfully remapped" | wc -l)
NEW_REMAPS=$((REMAP_COUNT_AFTER - REMAP_COUNT_BEFORE))

echo "Remaps before test: $REMAP_COUNT_BEFORE"
echo "Remaps after test:  $REMAP_COUNT_AFTER"
echo "New remaps created: $NEW_REMAPS"
echo

if [ $NEW_REMAPS -ge 5 ]; then
    echo "✅ SUCCESS: Created $NEW_REMAPS remaps (expected 5)"
elif [ $NEW_REMAPS -gt 0 ]; then
    echo "⚠️  PARTIAL: Created $NEW_REMAPS remaps (expected 5)"
else
    echo "❌ FAILED: No remaps created"
fi

echo
echo "Step 9: Show all remap operations from kernel log"
echo "================================================="
sudo dmesg | grep -E "Successfully remapped|Added remap entry" | tail -20

echo
echo "Step 10: Check dm-remap status"
echo "=============================="
sudo dmsetup status remap_direct

echo
echo "=================================================================="
echo "                     TEST COMPLETE"
echo "=================================================================="
echo
echo "Summary:"
echo "--------"
echo "Bad sectors defined: ${#BAD_SECTORS[@]} (${BAD_SECTORS[*]})"
echo "New remaps created:  $NEW_REMAPS"
echo "Method: Direct I/O (bypasses filesystem intelligence)"
echo
echo "Key Difference from Previous Test:"
echo "  - Previous: Used filesystem (ext4 avoided bad sectors)"
echo "  - This test: Direct I/O (forces access to every bad sector)"
echo "  - Result: All bad sectors should trigger remaps!"
echo "=================================================================="
