#!/bin/bash
# Simple Baseline Test
set -e

echo "SIMPLE BASELINE TEST: Week 7-8 Only"
echo "==================================="

# Create test devices
dd if=/dev/zero of=test_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=test_spare.img bs=1M count=5 2>/dev/null
sudo losetup /dev/loop50 test_main.img
sudo losetup /dev/loop51 test_spare.img

# Clean and load
sudo rmmod dm-remap 2>/dev/null || true
sudo dmsetup remove_all 2>/dev/null || true

echo "Loading baseline module..."
sudo insmod src/dm_remap.ko
echo "Module loaded: $(lsmod | grep dm_remap)"

echo "Testing device creation with timeout..."
timeout 10s bash -c 'echo "0 10240 remap /dev/loop50 /dev/loop51 0 10240" | sudo dmsetup create test-simple' || {
    echo "HANGING DETECTED!"
    sudo pkill -f dmsetup || true
    sudo rmmod dm-remap || true
    sudo losetup -d /dev/loop50 || true
    sudo losetup -d /dev/loop51 || true
    rm -f test_main.img test_spare.img
    exit 1
}

echo "SUCCESS! Baseline works!"
sudo dmsetup remove test-simple
sudo rmmod dm-remap
sudo losetup -d /dev/loop50
sudo losetup -d /dev/loop51
rm -f test_main.img test_spare.img