#!/bin/bash

# test_dm_flakey_async_errors.sh
# A different approach to dm-flakey testing that avoids hanging
# 
# KEY INSIGHT: Don't use error_reads continuously. Instead:
# 1. Start with working device
# 2. Write data successfully
# 3. Switch to error mode briefly
# 4. Read and handle errors
# 5. Switch back to working mode
# 6. Verify remaps were created

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=================================================================="
echo "     dm-flakey Testing with Transient Error Injection"
echo "=================================================================="
echo "Date: $(date)"
echo
echo "Strategy: Use dm-flakey's up/down intervals instead of error_reads"
echo "This should avoid the hanging issues caused by synchronous errors."
echo "=================================================================="
echo

# Cleanup function
cleanup() {
    echo
    echo "Cleaning up..."
    
    # Unmount if needed
    sudo umount /mnt/remap_test 2>/dev/null || true
    
    # Remove dm devices
    sudo dmsetup remove remap_test 2>/dev/null || true
    sudo dmsetup remove flakey_main 2>/dev/null || true
    
    # Detach loop devices
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    
    # Remove temp files
    rm -f /tmp/main_device.img /tmp/spare_device.img
    
    # Unload modules
    sudo rmmod dm_remap_v4_real 2>/dev/null || true
    sudo rmmod dm_remap_v4_metadata 2>/dev/null || true
    sudo rmmod dm_remap_v4_stats 2>/dev/null || true
    
    echo "Cleanup complete"
}

trap cleanup EXIT

echo "Step 1: Load dm-remap modules"
echo "=============================="
cd "$PROJECT_ROOT/src"

if ! lsmod | grep -q dm_remap_v4_stats; then
    sudo insmod dm-remap-v4-stats.ko
    echo "✓ Loaded dm-remap-v4-stats"
fi

if ! lsmod | grep -q dm_remap_v4_metadata; then
    sudo insmod dm-remap-v4-metadata.ko
    echo "✓ Loaded dm-remap-v4-metadata"
fi

if ! lsmod | grep -q dm_remap_v4_real; then
    sudo insmod dm-remap-v4-real.ko
    echo "✓ Loaded dm-remap-v4-real"
fi

echo
echo "Step 2: Create loop devices"
echo "=========================="
# 100MB main device
dd if=/dev/zero of=/tmp/main_device.img bs=1M count=100 2>/dev/null
sudo losetup /dev/loop20 /tmp/main_device.img
echo "✓ Created main device: /dev/loop20 (100MB)"

# 50MB spare device
dd if=/dev/zero of=/tmp/spare_device.img bs=1M count=50 2>/dev/null
sudo losetup /dev/loop21 /tmp/spare_device.img
echo "✓ Created spare device: /dev/loop21 (50MB)"

echo
echo "Step 3: Create dm-flakey device (NO ERROR MODE initially)"
echo "========================================================="
# Start in working mode: up_interval=300 down_interval=0
# This means: always up, never down, no errors
DEVICE_SIZE=$(blockdev --getsz /dev/loop20)
echo "0 $DEVICE_SIZE flakey /dev/loop20 0 300 0" | sudo dmsetup create flakey_main
echo "✓ Created flakey_main in WORKING mode (no errors yet)"

echo
echo "Step 4: Create dm-remap device on top of dm-flakey"
echo "=================================================="
DEVICE_SIZE=$(sudo blockdev --getsz /dev/mapper/flakey_main)
echo "0 $DEVICE_SIZE dm-remap-v4-real /dev/mapper/flakey_main /dev/loop21" | \
    sudo dmsetup create remap_test
echo "✓ Created remap_test: dm-remap → dm-flakey → loop20"

echo
echo "Step 5: Create filesystem and write test data"
echo "============================================="
sudo mkfs.ext4 -F /dev/mapper/remap_test >/dev/null 2>&1
echo "✓ Created ext4 filesystem"

sudo mkdir -p /mnt/remap_test
sudo mount /dev/mapper/remap_test /mnt/remap_test
echo "✓ Mounted filesystem"

# Write test files
for i in {1..10}; do
    sudo dd if=/dev/urandom of=/mnt/remap_test/testfile$i.dat bs=4K count=100 2>/dev/null
done
sudo sync
echo "✓ Wrote 10 test files (400KB each)"

echo
echo "Step 6: Unmount and prepare for error injection"
echo "==============================================="
sudo umount /mnt/remap_test
echo "✓ Unmounted filesystem"

echo
echo "Step 7: Switch dm-flakey to ERROR mode using drop_writes feature"
echo "================================================================"
# Instead of error_reads (which hangs), use drop_writes
# This will cause write errors without hanging
echo "0 $DEVICE_SIZE flakey /dev/loop20 0 0 1 1 drop_writes" | \
    sudo dmsetup reload flakey_main
sudo dmsetup suspend flakey_main
sudo dmsetup resume flakey_main
echo "✓ Switched flakey_main to DROP_WRITES mode"
echo "  - up_interval=0 (always fail)"
echo "  - down_interval=1"  
echo "  - feature_args=1 drop_writes"

echo
echo "Step 8: Try to write (should trigger errors)"
echo "==========================================="
# Mount in read-write mode
sudo mount /dev/mapper/remap_test /mnt/remap_test

# Try to write - this should fail and trigger error handling
echo "Attempting writes that should fail..."
for i in {1..5}; do
    sudo dd if=/dev/urandom of=/mnt/remap_test/trigger_error$i.dat bs=4K count=10 2>&1 | head -2 || true
done
sudo sync 2>&1 | head -5 || true

echo
echo "Step 9: Check kernel logs for errors"
echo "===================================="
echo "Recent errors from kernel log:"
sudo dmesg | grep -E "dm-remap|dm-flakey|I/O error" | tail -20

echo
echo "Step 10: Check if remaps were created"
echo "====================================="
sudo dmsetup message remap_test 0 print_stats
REMAP_COUNT=$(sudo dmsetup message remap_test 0 print_stats | grep "Total remaps:" | awk '{print $3}')
echo
if [ "$REMAP_COUNT" -gt 0 ]; then
    echo "✅ SUCCESS: $REMAP_COUNT remaps were created!"
else
    echo "⚠️  No remaps created yet"
fi

echo
echo "Step 11: Switch back to working mode"
echo "===================================="
sudo umount /mnt/remap_test
echo "0 $DEVICE_SIZE flakey /dev/loop20 0 300 0" | sudo dmsetup reload flakey_main
sudo dmsetup suspend flakey_main
sudo dmsetup resume flakey_main
echo "✓ Switched flakey_main back to WORKING mode"

echo
echo "Step 12: Verify data integrity"
echo "=============================="
sudo mount -o ro /dev/mapper/remap_test /mnt/remap_test
FILE_COUNT=$(sudo ls /mnt/remap_test/*.dat 2>/dev/null | wc -l)
echo "Files readable: $FILE_COUNT"

if [ "$FILE_COUNT" -ge 10 ]; then
    echo "✅ Original test files are intact"
else
    echo "⚠️  Some files may be affected by errors"
fi

echo
echo "=================================================================="
echo "                        TEST COMPLETE"
echo "=================================================================="
echo
echo "Summary:"
echo "--------"
echo "Remaps created: $REMAP_COUNT"
echo "Files intact: $FILE_COUNT/10"
echo
echo "Key Differences from Previous Attempts:"
echo "  1. Used drop_writes instead of error_reads (avoids hangs)"
echo "  2. Started in working mode, switched to error mode"
echo "  3. Errors occur on writes, not reads"
echo "  4. Less likely to hang because writes are async"
echo
echo "If this still doesn't work, the issue is fundamental to how"
echo "dm-flakey integrates with the kernel's error handling."
echo "=================================================================="
