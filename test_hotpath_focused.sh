#!/bin/bash
# Focused Hotpath Investigation
# Test the actual hotpath system step by step

set -e

echo "=== FOCUSED HOTPATH INVESTIGATION ==="
echo "Testing hotpath system components individually"
echo ""

# Setup test devices
echo "Setting up test devices..."
truncate -s 10M main_test.img spare_test.img
sudo losetup /dev/loop20 main_test.img
sudo losetup /dev/loop21 spare_test.img

echo ""
echo "TEST 1: Memory Pool ONLY (hotpath disabled)"
echo "==========================================="

# Disable hotpath init in constructor
sed -i 's/ret = dmr_hotpath_init(rc);/\/\* DISABLED: ret = dmr_hotpath_init(rc); \*\/\n    ret = 0; \/\* Skip hotpath *\//' src/dm_remap_main.c

echo "Building with hotpath disabled..."
cd src && make clean > /dev/null && make > /dev/null && cd ..

echo "Testing module and device creation..."
sudo insmod src/dm_remap.ko
timeout 5s sudo dmsetup create test1 --table "0 2048 remap /dev/loop20 /dev/loop21 0 1024" && echo "✅ Memory Pool works!" || echo "❌ Memory Pool hangs!"

sudo dmsetup remove --force test1 2>/dev/null || true
sudo rmmod dm_remap

echo ""
echo "TEST 2: Full hotpath system (the culprit)"
echo "========================================"

# Restore hotpath init
sed -i 's/\/\* DISABLED: ret = dmr_hotpath_init(rc); \*\/\n    ret = 0; \/\* Skip hotpath \*\//ret = dmr_hotpath_init(rc);/' src/dm_remap_main.c

echo "Building with full hotpath..."
cd src && make clean > /dev/null && make > /dev/null && cd ..

echo "Testing module loading..."
sudo insmod src/dm_remap.ko && echo "✅ Module loads" || echo "❌ Module load fails"

echo "Testing device creation (should hang)..."
timeout 5s sudo dmsetup create test2 --table "0 2048 remap /dev/loop20 /dev/loop21 0 1024" && echo "✅ Hotpath works!" || echo "❌ Hotpath hangs as expected!"

# Check logs
echo ""
echo "=== DMESG LOGS ==="
sudo dmesg | tail -10

echo ""
echo "=== TEST COMPLETE ==="
echo "If hotpath hangs, we'll investigate the hotpath init function line by line"