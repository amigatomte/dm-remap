#!/bin/bash
# Safe metadata persistence test

set -e

echo "=== Creating test devices ==="
dd if=/dev/zero of=/tmp/test_main.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=5 2>/dev/null

MAIN_LOOP=$(sudo losetup -f --show /tmp/test_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/test_spare.img)

echo "Main: $MAIN_LOOP, Spare: $SPARE_LOOP"

echo "=== Creating dm-remap device ==="
echo "0 $(sudo blockdev --getsz $MAIN_LOOP) dm-remap-v4 $MAIN_LOOP $SPARE_LOOP" | sudo dmsetup create test_remap

echo "=== Device created, checking dmesg ==="
sudo dmesg | tail -20 | grep dm-remap

echo "=== Triggering a remap via message interface ==="
echo "remap 100" | sudo dmsetup message test_remap 0

echo "=== Checking remap was created ==="
sudo dmsetup status test_remap

echo "=== Removing device (this should write metadata) ==="
sudo dmsetup remove test_remap

echo "=== Checking dmesg for metadata write ==="
sudo dmesg | tail -30 | grep -i metadata

echo "=== Cleaning up ==="
sudo losetup -d $MAIN_LOOP
sudo losetup -d $SPARE_LOOP
rm -f /tmp/test_main.img /tmp/test_spare.img

echo "=== Test completed successfully! ==="
