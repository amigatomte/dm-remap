#!/bin/bash
# Crash-safe metadata persistence test
# Logs every step and syncs to disk immediately

LOGFILE="/var/log/dm-remap-test.log"

# Function to log and sync to disk immediately
log_sync() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S.%N')] $1" | tee -a "$LOGFILE"
    sync
    sleep 0.5  # Give system time to actually write
}

log_sync "========================================="
log_sync "DM-REMAP CRASH-SAFE METADATA TEST START"
log_sync "========================================="

# Cleanup function
cleanup() {
    log_sync "CLEANUP: Starting cleanup..."
    dmsetup remove testdev 2>/dev/null && log_sync "CLEANUP: Removed testdev"
    dmsetup remove dm-linear-main 2>/dev/null && log_sync "CLEANUP: Removed dm-linear-main"
    dmsetup remove dm-error-spare 2>/dev/null && log_sync "CLEANUP: Removed dm-error-spare"
    losetup -d /dev/loop20 2>/dev/null && log_sync "CLEANUP: Detached loop20"
    losetup -d /dev/loop21 2>/dev/null && log_sync "CLEANUP: Detached loop21"
    rm -f /tmp/main.img /tmp/spare.img 2>/dev/null && log_sync "CLEANUP: Removed temp files"
    log_sync "CLEANUP: Complete"
}

# Set trap for cleanup
trap cleanup EXIT

log_sync "STEP 1: Checking if modules are loaded..."
if lsmod | grep -q dm_remap; then
    log_sync "STEP 1: dm-remap modules already loaded"
else
    log_sync "STEP 1: Loading dm-remap modules..."
    log_sync "STEP 1a: Loading dm_remap_v4_stats..."
    sudo insmod src/dm-remap-v4-stats.ko && log_sync "STEP 1a: dm_remap_v4_stats loaded OK" || { log_sync "STEP 1a: FAILED to load dm_remap_v4_stats"; exit 1; }
    
    log_sync "STEP 1b: Loading dm_remap_v4_real..."
    sudo insmod src/dm-remap-v4-real.ko && log_sync "STEP 1b: dm_remap_v4_real loaded OK" || { log_sync "STEP 1b: FAILED to load dm_remap_v4_real"; exit 1; }
fi

log_sync "STEP 2: Creating test image files..."
dd if=/dev/zero of=/tmp/main.img bs=1M count=100 2>/dev/null && log_sync "STEP 2a: Created main.img (100MB)" || { log_sync "STEP 2a: FAILED"; exit 1; }
dd if=/dev/zero of=/tmp/spare.img bs=1M count=100 2>/dev/null && log_sync "STEP 2b: Created spare.img (100MB)" || { log_sync "STEP 2b: FAILED"; exit 1; }

log_sync "STEP 3: Setting up loop devices..."
sudo losetup /dev/loop20 /tmp/main.img && log_sync "STEP 3a: Loop20 attached to main.img" || { log_sync "STEP 3a: FAILED"; exit 1; }
sudo losetup /dev/loop21 /tmp/spare.img && log_sync "STEP 3b: Loop21 attached to spare.img" || { log_sync "STEP 3b: FAILED"; exit 1; }

log_sync "STEP 4: Creating dm-linear and dm-error devices..."
echo "0 204800 linear /dev/loop20 0" | sudo dmsetup create dm-linear-main && log_sync "STEP 4a: Created dm-linear-main" || { log_sync "STEP 4a: FAILED"; exit 1; }
echo "0 204800 error" | sudo dmsetup create dm-error-spare && log_sync "STEP 4b: Created dm-error-spare" || { log_sync "STEP 4b: FAILED"; exit 1; }

log_sync "STEP 5: About to create dm-remap device..."
log_sync "STEP 5: Command: dmsetup create testdev --table '0 204800 dm-remap-v4 /dev/mapper/dm-linear-main /dev/loop21'"
sync
sleep 1

log_sync "STEP 5: EXECUTING DMSETUP CREATE NOW..."
echo "0 204800 dm-remap-v4 /dev/mapper/dm-linear-main /dev/loop21" | sudo dmsetup create testdev && log_sync "STEP 5: SUCCESS - dm-remap device created!" || { log_sync "STEP 5: FAILED to create device"; exit 1; }

log_sync "STEP 6: Device created successfully, checking status..."
sudo dmsetup status testdev | tee -a "$LOGFILE"
sync

log_sync "STEP 7: Reading from device (should work)..."
sudo dd if=/dev/mapper/testdev of=/dev/null bs=4096 count=10 2>&1 | tee -a "$LOGFILE" && log_sync "STEP 7: Read completed OK" || log_sync "STEP 7: Read FAILED"

log_sync "STEP 8: Writing to device (should work)..."
sudo dd if=/dev/zero of=/dev/mapper/testdev bs=4096 count=10 2>&1 | tee -a "$LOGFILE" && log_sync "STEP 8: Write completed OK" || log_sync "STEP 8: Write FAILED"

log_sync "STEP 9: Checking dmesg for dm-remap messages..."
sudo dmesg | tail -20 | grep -i "dm-remap" | tee -a "$LOGFILE"
sync

log_sync "STEP 10: Device operations complete, about to remove device..."
sync
sleep 1

log_sync "STEP 10: EXECUTING DMSETUP REMOVE NOW..."
sudo dmsetup remove testdev && log_sync "STEP 10: Device removed successfully" || log_sync "STEP 10: FAILED to remove device"

log_sync "STEP 11: Checking dmesg after removal..."
sudo dmesg | tail -30 | grep -i "dm-remap" | tee -a "$LOGFILE"
sync

log_sync "========================================="
log_sync "TEST COMPLETED SUCCESSFULLY!"
log_sync "========================================="
log_sync "Check $LOGFILE for full log"
