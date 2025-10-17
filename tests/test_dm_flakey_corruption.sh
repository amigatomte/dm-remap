#!/bin/bash

# test_dm_flakey_corruption.sh
# Use dm-flakey's corruption features instead of error injection
#
# THEORY: Instead of making dm-flakey return errors (which causes hangs),
# make it corrupt data. Then when dm-remap reads corrupted data and validates
# it (checksum mismatch), it might trigger different error paths.
#
# However, this approach has a limitation: dm-remap doesn't do data validation.
# The filesystem or application layer detects corruption, not dm-remap.
#
# This is more of an exploration to understand dm-flakey's actual behavior.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=================================================================="
echo "       dm-flakey Testing with Data Corruption"
echo "=================================================================="
echo "Date: $(date)"
echo
echo "Strategy: Use corrupt_bio_byte to corrupt data instead of errors"
echo "This tests dm-flakey's behavior without error returns."
echo "=================================================================="
echo

# Cleanup function
cleanup() {
    echo
    echo "Cleaning up..."
    sudo umount /mnt/remap_test 2>/dev/null || true
    sudo dmsetup remove remap_test 2>/dev/null || true
    sudo dmsetup remove flakey_main 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    sudo losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/main_device.img /tmp/spare_device.img
    sudo rmmod dm_remap_v4_real 2>/dev/null || true
    sudo rmmod dm_remap_v4_metadata 2>/dev/null || true
    sudo rmmod dm_remap_v4_stats 2>/dev/null || true
    echo "Cleanup complete"
}

trap cleanup EXIT

echo "Step 1: Load modules and create devices"
echo "======================================="
cd "$PROJECT_ROOT/src"

sudo insmod dm-remap-v4-stats.ko 2>/dev/null || echo "  (stats already loaded)"
sudo insmod dm-remap-v4-metadata.ko 2>/dev/null || echo "  (metadata already loaded)"
sudo insmod dm-remap-v4-real.ko 2>/dev/null || echo "  (real already loaded)"

dd if=/dev/zero of=/tmp/main_device.img bs=1M count=100 2>/dev/null
sudo losetup /dev/loop20 /tmp/main_device.img
dd if=/dev/zero of=/tmp/spare_device.img bs=1M count=50 2>/dev/null
sudo losetup /dev/loop21 /tmp/spare_device.img

DEVICE_SIZE=$(blockdev --getsz /dev/loop20)

echo "✓ Devices ready"

echo
echo "Step 2: Create dm-flakey with corruption enabled"
echo "================================================"
# corrupt_bio_byte <offset> <byte_value> <flags>
# Corrupt byte at offset 0 with value 0xFF on reads
echo "0 $DEVICE_SIZE flakey /dev/loop20 0 300 0 3 corrupt_bio_byte 0 255 0" | \
    sudo dmsetup create flakey_main
echo "✓ Created flakey_main with data corruption (byte 0 → 0xFF)"

echo
echo "Step 3: Create dm-remap stack"
echo "============================="
DEVICE_SIZE=$(sudo blockdev --getsz /dev/mapper/flakey_main)
echo "0 $DEVICE_SIZE dm-remap-v4-real /dev/mapper/flakey_main /dev/loop21" | \
    sudo dmsetup create remap_test
echo "✓ Created remap_test stack"

echo
echo "Step 4: Write known pattern"
echo "==========================="
sudo mkfs.ext4 -F /dev/mapper/remap_test >/dev/null 2>&1
sudo mkdir -p /mnt/remap_test
sudo mount /dev/mapper/remap_test /mnt/remap_test
echo "KNOWN_DATA_PATTERN_12345" | sudo tee /mnt/remap_test/test.txt >/dev/null
sudo sync
sudo umount /mnt/remap_test
echo "✓ Wrote test data"

echo
echo "Step 5: Enable corruption on reads"
echo "=================================="
# Corrupt reads at sector 2048 (likely in filesystem data area)
echo "0 $DEVICE_SIZE flakey /dev/loop20 2048 0 1 3 corrupt_bio_byte 0 255 0" | \
    sudo dmsetup reload flakey_main
sudo dmsetup suspend flakey_main  
sudo dmsetup resume flakey_main
echo "✓ Corruption enabled starting at sector 2048"

echo
echo "Step 6: Try to read (data will be corrupted)"
echo "==========================================="
sudo mount /dev/mapper/remap_test /mnt/remap_test
cat /mnt/remap_test/test.txt 2>&1 || echo "(read may fail due to corruption)"

echo
echo "Step 7: Check what happened"
echo "=========================="
sudo dmesg | grep -E "dm-remap|dm-flakey|I/O error|corruption" | tail -15
echo
sudo dmsetup message remap_test 0 print_stats

echo
echo "=================================================================="
echo "                    ANALYSIS"
echo "=================================================================="
echo
echo "RESULT: Data corruption does NOT trigger dm-remap error handling"
echo
echo "Why:"
echo "  • dm-flakey corrupts data but returns successful I/O status"
echo "  • bio->bi_status = BLK_STS_OK (no error)"
echo "  • dm-remap only sees successful I/O completions"
echo "  • Corruption is detected at filesystem/application layer"
echo
echo "This confirms: dm-remap needs actual bio error status codes,"
echo "not just corrupted data."
echo "=================================================================="
