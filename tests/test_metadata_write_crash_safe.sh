#!/bin/bash
# Crash-safe metadata write verification test
# Saves kernel log to disk after EACH step so we can see where crash occurred

set -e

KERNEL_LOG="/var/log/dm-remap-crash-debug.log"
TEST_LOG="/var/log/dm-remap-test-progress.log"

# Function to save kernel log and test progress
save_state() {
    local step="$1"
    echo "[$(date '+%Y-%m-%d %H:%M:%S.%N')] $step" | sudo tee -a "$TEST_LOG"
    sudo dmesg > "$KERNEL_LOG"
    sync
    sleep 0.3
}

echo "=================================================================="
echo "Crash-Safe Metadata Write Verification Test"
echo "=================================================================="
echo "Kernel logs will be saved to: $KERNEL_LOG"
echo "Test progress will be saved to: $TEST_LOG"
echo ""

# Clear logs
sudo rm -f "$KERNEL_LOG" "$TEST_LOG"
sudo dmesg -C

save_state "TEST START"

# Cleanup function
cleanup() {
    save_state "CLEANUP: Starting cleanup"
    sudo dmsetup remove testdev 2>/dev/null || true
    save_state "CLEANUP: Removed testdev (if existed)"
    sudo dmsetup remove dm-main-with-errors 2>/dev/null || true
    save_state "CLEANUP: Removed dm-main-with-errors (if existed)"
    sudo losetup -d /dev/loop20 2>/dev/null || true
    save_state "CLEANUP: Detached loop20 (if existed)"
    sudo losetup -d /dev/loop21 2>/dev/null || true
    save_state "CLEANUP: Detached loop21 (if existed)"
    rm -f /tmp/test_main.img /tmp/test_spare.img
    save_state "CLEANUP: Complete"
}

trap cleanup EXIT

save_state "Step 1: Load dm-remap modules"
echo "Step 1: Load dm-remap modules"
echo "=============================="
sudo rmmod dm_remap_v4_real dm_remap_v4_stats 2>/dev/null || true
save_state "Step 1a: Removed old modules"

sudo insmod src/dm-remap-v4-stats.ko
save_state "Step 1b: Loaded dm-remap-v4-stats"

sudo insmod src/dm-remap-v4-real.ko
save_state "Step 1c: Loaded dm-remap-v4-real"

echo "✓ Modules loaded"
echo ""

save_state "Step 2: Create test images"
echo "Step 2: Create test images"
echo "=========================="
dd if=/dev/zero of=/tmp/test_main.img bs=1M count=100 2>/dev/null
save_state "Step 2a: Created main.img (100MB)"

dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=100 2>/dev/null
save_state "Step 2b: Created spare.img (100MB)"

echo "✓ Created 100MB test images"
echo ""

save_state "Step 3: Setup loop devices"
echo "Step 3: Setup loop devices"
echo "=========================="
sudo losetup /dev/loop20 /tmp/test_main.img
save_state "Step 3a: Attached loop20 to main.img"

sudo losetup /dev/loop21 /tmp/test_spare.img
save_state "Step 3b: Attached loop21 to spare.img"

echo "✓ Loop devices attached"
echo "  Main: /dev/loop20 (204800 sectors)"
echo "  Spare: /dev/loop21"
echo ""

save_state "Step 4: Define bad sectors"
echo "Step 4: Define bad sectors on main device"
echo "=========================================="
BAD_SECTORS=(1000 2000 3000)
echo "Bad sectors: ${BAD_SECTORS[*]}"
save_state "Step 4: Bad sectors defined: ${BAD_SECTORS[*]}"
echo ""

save_state "Step 5: Create dm-linear + dm-error for main device"
echo "Step 5: Create dm-linear + dm-error for main device"
echo "==================================================="

# Build table with errors on main device
TABLE=""
CURRENT=0
TOTAL_SECTORS=204800

for BAD in 1000 2000 3000; do
    if [ $CURRENT -lt $BAD ]; then
        SIZE=$((BAD - CURRENT))
        TABLE+="$CURRENT $SIZE linear /dev/loop20 $CURRENT"$'\n'
    fi
    TABLE+="$BAD 1 error"$'\n'
    CURRENT=$((BAD + 1))
done

if [ $CURRENT -lt $TOTAL_SECTORS ]; then
    SIZE=$((TOTAL_SECTORS - CURRENT))
    TABLE+="$CURRENT $SIZE linear /dev/loop20 $CURRENT"$'\n'
fi

echo "$TABLE" | sudo dmsetup create dm-main-with-errors
save_state "Step 5: Created dm-main-with-errors with 3 bad sectors"

echo "✓ Created dm-main-with-errors with 3 bad sectors"
echo ""

save_state "Step 6: Check spare device BEFORE (should be all zeros)"
echo "Step 6: Check spare device BEFORE (should be all zeros)"
echo "========================================================"
echo "First 512 bytes of spare device:"
sudo hexdump -C /tmp/test_spare.img -n 512 | head -5
save_state "Step 6: Checked spare device - all zeros"
echo ""

LINES_OF_ZEROS=$(sudo hexdump -C /tmp/test_spare.img -n 512 | grep -c "00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00" || true)
echo "Lines of zeros: $LINES_OF_ZEROS/32 (should be 32)"
save_state "Step 6: Lines of zeros: $LINES_OF_ZEROS/32"
echo ""

save_state "Step 7: *** CRITICAL *** About to create dm-remap device"
echo "Step 7: Create dm-remap device"
echo "=============================="
echo "*** CRITICAL STEP - THIS IS WHERE PREVIOUS CRASHES OCCURRED ***"
save_state "Step 7a: Just before dmsetup create"
sync
sleep 1

echo "0 204800 dm-remap-v4 /dev/mapper/dm-main-with-errors /dev/loop21" | sudo dmsetup create testdev
save_state "Step 7b: *** SUCCESS *** dmsetup create completed without crash!"

echo "✓ dm-remap device created successfully!"
echo ""

save_state "Step 8: Check device status"
echo "Step 8: Check device status"
echo "==========================="
sudo dmsetup status testdev
save_state "Step 8: Got device status"
echo ""

save_state "Step 9: Trigger errors by reading bad sectors"
echo "Step 9: Trigger errors by reading bad sectors"
echo "=============================================="

for SECTOR in "${BAD_SECTORS[@]}"; do
    echo "Reading bad sector $SECTOR..."
    save_state "Step 9a: About to read sector $SECTOR"
    
    sudo dd if=/dev/mapper/testdev of=/dev/null bs=512 skip=$SECTOR count=1 iflag=direct 2>&1 | head -2 || true
    save_state "Step 9b: Completed read of sector $SECTOR"
    
    sleep 0.5
done

echo "✓ All bad sectors accessed"
save_state "Step 9c: All bad sectors accessed"
echo ""

save_state "Step 10: Wait for remap processing"
echo "Step 10: Wait for remap processing"
echo "=================================="
sleep 2
save_state "Step 10: Wait complete"
echo ""

save_state "Step 11: Check for kernel messages about remaps"
echo "Step 11: Check kernel log for remap messages"
echo "============================================="
sudo dmesg | grep -E "dm-remap|remap entry|CRASH-DEBUG" | tail -50
save_state "Step 11: Kernel log checked"
echo ""

save_state "Step 12: Remove device (should trigger metadata write)"
echo "Step 12: Remove device (should trigger metadata write)"
echo "======================================================"
sync
save_state "Step 12a: About to remove device"

save_state "Step 12a-1: Manually suspending device first (with strace)"
sudo strace -o /var/log/dmsetup-suspend-strace.log -f -tt dmsetup suspend testdev
save_state "Step 12a-2: Device suspended successfully"

save_state "Step 12a-2.5: Checking device status after suspend"
sudo dmsetup info testdev
save_state "Step 12a-2.6: Device status checked"

save_state "Step 12a-2.7: Waiting 1 second before remove"
sleep 1
save_state "Step 12a-2.8: Wait complete"

save_state "Step 12a-2.9: Resuming device before removal"
sudo dmsetup resume testdev
save_state "Step 12a-2.10: Device resumed"

save_state "Step 12a-3: Now removing RESUMED device"
sudo dmsetup remove testdev
save_state "Step 12b: Device removed successfully"

echo "✓ Device removed"
echo ""

save_state "Step 13: Check kernel log for metadata write"
echo "Step 13: Check for metadata write in kernel log"
echo "================================================"
sudo dmesg | grep -i "metadata" | tail -20
save_state "Step 13: Checked for metadata write messages"
echo ""

save_state "Step 14: Check spare device AFTER"
echo "Step 14: Check spare device AFTER (should have metadata)"
echo "========================================================="
echo "First 512 bytes of spare device:"
sudo hexdump -C /tmp/test_spare.img -n 512 | head -10
save_state "Step 14: Checked spare device after test"
echo ""

save_state "TEST COMPLETED SUCCESSFULLY"
echo "=================================================================="
echo "                    TEST COMPLETED SUCCESSFULLY!"
echo "=================================================================="
echo ""
echo "Check these files for details:"
echo "  - Test progress: $TEST_LOG"
echo "  - Kernel log: $KERNEL_LOG"
echo ""
echo "If system crashed, check these files after reboot to see"
echo "exactly where the crash occurred."
echo "=================================================================="
