#!/bin/bash
# Test metadata persistence with actual remaps
# This creates errors to trigger remapping, then verifies metadata is written

set -e

LOG_FILE="test_remap_output.log"

log_sync() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S.%N')] $1" | tee -a "$LOG_FILE"
    sync
}

# Cleanup function
cleanup() {
    log_sync "CLEANUP: Starting cleanup..."
    sudo dmsetup remove testdev 2>/dev/null || true
    sudo dmsetup remove dm-error-spare 2>/dev/null || true
    sudo dmsetup remove dm-linear-main 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/test_main.img /tmp/test_spare.img
    log_sync "CLEANUP: Complete"
}

trap cleanup EXIT

log_sync "========================================="
log_sync "DM-REMAP METADATA TEST WITH REMAPS"
log_sync "========================================="

# Reload modules
log_sync "STEP 1: Loading modules..."
sudo rmmod dm_remap_v4_real dm_remap_v4_stats 2>/dev/null || true
sudo insmod src/dm-remap-v4-stats.ko
sudo insmod src/dm-remap-v4-real.ko
log_sync "STEP 1: Modules loaded"

# Create test images
log_sync "STEP 2: Creating test images..."
dd if=/dev/zero of=/tmp/test_main.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=100 2>/dev/null
log_sync "STEP 2: Images created"

# Setup loop devices
log_sync "STEP 3: Setting up loop devices..."
MAIN_LOOP=$(sudo losetup -f --show /tmp/test_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/test_spare.img)
log_sync "STEP 3: Loop devices: main=$MAIN_LOOP, spare=$SPARE_LOOP"

# Get sector count
SECTORS=$(sudo blockdev --getsz $MAIN_LOOP)
log_sync "STEP 4: Sectors=$SECTORS"

# Create dm-linear for main device
log_sync "STEP 5: Creating dm-linear for main..."
echo "0 $SECTORS linear $MAIN_LOOP 0" | sudo dmsetup create dm-linear-main
log_sync "STEP 5: dm-linear-main created"

# Create dm-error for specific sectors to trigger remapping
# Error on sectors 100-109 (10 sectors)
log_sync "STEP 6: Creating dm-error to inject errors..."
cat << EOF | sudo dmsetup create dm-error-spare
0 100 linear $SPARE_LOOP 0
100 10 error
110 $((SECTORS - 110)) linear $SPARE_LOOP 110
EOF
log_sync "STEP 6: dm-error-spare created with errors at sectors 100-109"

# Clear dmesg
sudo dmesg -C

# Create dm-remap device
log_sync "STEP 7: Creating dm-remap device..."
echo "0 $SECTORS dm-remap-v4 /dev/mapper/dm-linear-main /dev/mapper/dm-error-spare" | sudo dmsetup create testdev
log_sync "STEP 7: dm-remap device created"

# Show initial status
log_sync "STEP 8: Initial device status:"
sudo dmsetup status testdev | tee -a "$LOG_FILE"

# Write to sectors that will trigger errors and remapping
log_sync "STEP 9: Writing to sectors that will cause errors (100-109)..."
# Write to sector 100 (will fail and trigger remap)
sudo dd if=/dev/urandom of=/dev/mapper/testdev bs=512 count=1 seek=100 oflag=direct 2>&1 | tee -a "$LOG_FILE" || true
log_sync "STEP 9: Write attempt completed"

# Wait a bit for error handling
sleep 2

# Check status for remaps
log_sync "STEP 10: Checking for remaps..."
sudo dmsetup status testdev | tee -a "$LOG_FILE"

# Check kernel messages
log_sync "STEP 11: Kernel messages:"
sudo dmesg | grep "dm-remap" | tee -a "$LOG_FILE"

# Now remove device - this should trigger metadata write
log_sync "STEP 12: Removing device (triggers metadata write)..."
sync
sudo dmsetup remove testdev
log_sync "STEP 12: Device removed"

# Check for metadata write message
log_sync "STEP 13: Checking for metadata write..."
sudo dmesg | grep -i "metadata" | tail -10 | tee -a "$LOG_FILE"

# Check spare device for metadata signature
log_sync "STEP 14: Checking spare device for metadata..."
echo "First 256 bytes of spare device:" | tee -a "$LOG_FILE"
sudo hexdump -C /tmp/test_spare.img | head -16 | tee -a "$LOG_FILE"

log_sync "========================================="
log_sync "TEST COMPLETED"
log_sync "========================================="
log_sync "Check $LOG_FILE for full log"
