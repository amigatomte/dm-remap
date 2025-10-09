#!/bin/bash
# Quick test script for device creation hang debugging
set -e

echo "=== DEVICE CREATION HANG TEST ==="
echo "Systems disabled: Health Scanner, Auto-Save, Sysfs, Debug"
echo ""

echo "1. Building module..."
cd src && make clean > /dev/null && make > /dev/null && cd ..

echo "2. Loading module..."
sudo insmod src/dm_remap.ko
echo "   ✅ Module loaded successfully"

echo "3. Setting up test devices..."
truncate -s 10M main_test.img spare_test.img
sudo losetup /dev/loop20 main_test.img
sudo losetup /dev/loop21 spare_test.img
echo "   ✅ Test devices ready"

echo "4. Testing device creation (10s timeout)..."
timeout 10s sudo dmsetup create dm_remap_test --table "0 2048 remap /dev/loop20 /dev/loop21 0 1024" && echo "   🎯 SUCCESS! Device creation completed!" || echo "   🚨 STILL HANGING!"

echo ""
echo "5. Checking results..."
sudo dmsetup ls | grep remap && echo "   Device exists in system"
sudo dmesg | tail -5
echo ""
echo "=== TEST COMPLETE ==="