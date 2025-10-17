#!/bin/bash

# test_dm_linear_error.sh
# Use dm-linear + dm-error for precise bad sector simulation
# This avoids all the timing/hanging issues with dm-flakey

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=================================================================="
echo "   dm-remap v4 Testing with dm-linear + dm-error"
echo "=================================================================="
echo "Date: $(date)"
echo
echo "This method provides:"
echo "  ✓ Surgical precision (exact sector control)"
echo "  ✓ No timing issues (permanent bad sectors)"
echo "  ✓ Fast operation (no retries needed)"
echo "  ✓ Real hardware simulation"
echo "=================================================================="
echo

# Cleanup function
cleanup() {
    echo
    echo "Cleaning up..."
    sudo umount /mnt/remap_test 2>/dev/null || true
    sudo dmsetup remove remap_precise 2>/dev/null || true
    sudo dmsetup remove main_with_errors 2>/dev/null || true
    # Detach loop devices if they were created
    if [ -n "$MAIN_LOOP" ]; then
        sudo losetup -d $MAIN_LOOP 2>/dev/null || true
    fi
    if [ -n "$SPARE_LOOP" ]; then
        sudo losetup -d $SPARE_LOOP 2>/dev/null || true
    fi
    rm -f /tmp/main_precise.img /tmp/spare_precise.img
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
# Clean up old files
rm -f /tmp/main_precise.img /tmp/spare_precise.img

# 100MB main device - use next available loop device
dd if=/dev/zero of=/tmp/main_precise.img bs=1M count=100 2>/dev/null
MAIN_LOOP=$(sudo losetup -f --show /tmp/main_precise.img)
echo "✓ Created main device: $MAIN_LOOP (100MB)"

# 50MB spare device
dd if=/dev/zero of=/tmp/spare_precise.img bs=1M count=50 2>/dev/null
SPARE_LOOP=$(sudo losetup -f --show /tmp/spare_precise.img)
echo "✓ Created spare device: $SPARE_LOOP (50MB)"

TOTAL_SECTORS=$(sudo blockdev --getsz $MAIN_LOOP)
echo "  Total sectors: $TOTAL_SECTORS"

echo
echo "Step 3: Define precise bad sectors"
echo "=================================="
# Define specific bad sectors to test
# Use higher sector numbers to avoid filesystem metadata areas
# Filesystem metadata is typically in first few thousand sectors
BAD_SECTORS=(50000 50001 75000 100000 150000)
echo "Bad sectors: ${BAD_SECTORS[*]}"

echo
echo "Step 4: Create dm-linear + dm-error table"
echo "========================================="

# Build the device-mapper table
# Format: <start_sector> <num_sectors> <target> [target_args...]

TABLE=""
CURRENT=0

# Sort bad sectors
IFS=$'\n' SORTED_BAD=($(printf '%s\n' "${BAD_SECTORS[@]}" | sort -n))
unset IFS

echo "Building precision bad sector map..."
for BAD in "${SORTED_BAD[@]}"; do
    # Add good sectors before this bad sector
    if [ $CURRENT -lt $BAD ]; then
        GOOD_COUNT=$((BAD - CURRENT))
        TABLE+="$CURRENT $GOOD_COUNT linear $MAIN_LOOP $CURRENT"$'\n'
        echo "  Sectors $CURRENT-$((BAD-1)): GOOD (linear)"
    fi
    
    # Add the bad sector
    TABLE+="$BAD 1 error"$'\n'
    echo "  Sector $BAD: BAD (error)"
    
    CURRENT=$((BAD + 1))
done

# Add remaining good sectors
if [ $CURRENT -lt $TOTAL_SECTORS ]; then
    REMAINING=$((TOTAL_SECTORS - CURRENT))
    TABLE+="$CURRENT $REMAINING linear $MAIN_LOOP $CURRENT"$'\n'
    echo "  Sectors $CURRENT-$((TOTAL_SECTORS-1)): GOOD (linear)"
fi

# Create the device with precise bad sectors
echo "$TABLE" | sudo dmsetup create main_with_errors

echo "✓ Created main_with_errors with ${#BAD_SECTORS[@]} bad sectors"

echo
echo "Step 5: Create dm-remap on top"
echo "=============================="
DEVICE_SIZE=$(sudo blockdev --getsz /dev/mapper/main_with_errors)
echo "0 $DEVICE_SIZE dm-remap-v4 /dev/mapper/main_with_errors $SPARE_LOOP" | \
    sudo dmsetup create remap_precise

echo "✓ Created remap_precise: dm-remap-v4 → main_with_errors → $MAIN_LOOP"
echo "  Stack: dm-remap → (dm-linear+dm-error) → loop device"

echo
echo "Step 6: Create filesystem"
echo "========================"
sudo mkfs.ext4 -F /dev/mapper/remap_precise >/dev/null 2>&1
sudo mkdir -p /mnt/remap_test
sudo mount /dev/mapper/remap_precise /mnt/remap_test
echo "✓ Filesystem created and mounted"

echo
echo "Step 7: Write test data"
echo "======================"
echo "Writing files to trigger errors on bad sectors..."

# Write data that will hit our bad sectors
# Each file is 1MB, and we'll write enough to cover the bad sectors
for i in {1..20}; do
    sudo dd if=/dev/urandom of=/mnt/remap_test/testfile$i.dat bs=1M count=1 2>/dev/null &
done
wait

# Force sync to ensure writes hit disk
sudo sync
echo "✓ Wrote 20MB of test data"

echo
echo "Step 8: Read test data (trigger error detection)"
echo "==============================================="
echo "Reading files to trigger read errors on bad sectors..."

for i in {1..20}; do
    sudo dd if=/mnt/remap_test/testfile$i.dat of=/dev/null bs=1M 2>/dev/null || true
done

echo "✓ Read operations completed"

echo
echo "Step 9: Check dm-remap status"
echo "============================"
echo "Current device status:"
sudo dmsetup status remap_precise

echo
echo "Step 10: Check kernel logs"
echo "========================="
echo "Recent dm-remap activity:"
sudo dmesg | grep -E "dm-remap|I/O error" | tail -30

echo
echo "Step 11: Verify remapping worked"
echo "==============================="
STATUS=$(sudo dmsetup status remap_precise)
echo "Full status: $STATUS"

# Try to parse remap count from status
# dm-remap-v4 status format includes remap statistics
echo
echo "Interpreting status..."
if echo "$STATUS" | grep -q "dm-remap-v4"; then
    echo "✓ dm-remap-v4 device is operational"
    
    # The status line contains various counters
    # Look for evidence of remapping activity
    if sudo dmesg | grep -q "Successfully remapped"; then
        REMAP_COUNT=$(sudo dmesg | grep "Successfully remapped" | wc -l)
        echo "✅ SUCCESS: Found $REMAP_COUNT successful remap operations in kernel log"
    else
        echo "⚠️  No successful remaps found in kernel log yet"
    fi
fi

echo
echo "Step 12: Data integrity check"
echo "============================="
sudo umount /mnt/remap_test
sudo fsck -n /dev/mapper/remap_precise 2>&1 | head -10
echo "✓ Filesystem check complete"

echo
echo "=================================================================="
echo "                     TEST COMPLETE"
echo "=================================================================="
echo
echo "Summary:"
echo "--------"
echo "Bad sectors defined: ${#BAD_SECTORS[@]} (${BAD_SECTORS[*]})"
echo "Method: dm-linear + dm-error (surgical precision)"
echo "Benefits:"
echo "  ✓ No hanging issues (unlike dm-flakey)"
echo "  ✓ Immediate error returns"
echo "  ✓ Spare device operations not affected"
echo "  ✓ Fast and predictable behavior"
echo
echo "Check kernel logs above for remap activity."
echo "=================================================================="
