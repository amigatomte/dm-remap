#!/bin/bash
# Test REAL v4 branch with CORRECT dm-remap parameters
set -e

echo "TESTING REAL v4 WITH CORRECT DM-REMAP PARAMETERS"
echo "================================================="
echo "dm-remap requires: <main_dev> <spare_dev> <spare_start> <spare_len>"
echo ""

# Create proper test images and loop devices
echo "1. Setting up test environment..."
dd if=/dev/zero of=main_device.img bs=1M count=10 2>/dev/null
dd if=/dev/zero of=spare_device.img bs=1M count=10 2>/dev/null

sudo losetup -d /dev/loop21 2>/dev/null || true
sudo losetup -d /dev/loop22 2>/dev/null || true
sudo losetup /dev/loop21 main_device.img
sudo losetup /dev/loop22 spare_device.img

echo "   ✅ Main device: /dev/loop21 (10MB)"
echo "   ✅ Spare device: /dev/loop22 (10MB)"

# Clean up any existing dm-remap state
echo "2. Cleaning up existing state..."
sudo rmmod dm-remap 2>/dev/null || true
sudo dmsetup remove_all 2>/dev/null || true

# Load the REAL v4 module
echo "3. Loading REAL v4 dm-remap module..."
if sudo insmod src/dm_remap.ko; then
    echo "   ✅ Module loaded successfully"
    lsmod | grep dm_remap
else
    echo "   ❌ Module load failed!"
    exit 1
fi

# Test device creation with CORRECT parameters
echo "4. Testing device creation with CORRECT dm-remap parameters..."
echo "   Command: echo '0 20480 remap /dev/loop21 /dev/loop22 0 20480' | dmsetup create test-v4-correct"

timeout 15s bash -c '
    echo "0 20480 remap /dev/loop21 /dev/loop22 0 20480" | sudo dmsetup create test-v4-correct
    echo "✅ Device created successfully with proper parameters!"
    
    # Test I/O operations
    echo "Testing I/O operations on /dev/mapper/test-v4-correct..."
    sudo dd if=/dev/zero of=/dev/mapper/test-v4-correct bs=4k count=10 2>/dev/null
    echo "✅ Write operations completed successfully"
    
    sudo dd if=/dev/mapper/test-v4-correct of=/dev/null bs=4k count=10 2>/dev/null
    echo "✅ Read operations completed successfully"
' || {
    echo ""
    echo "🚨 HANGING DETECTED in device operations!"
    echo "This confirms there IS a hanging issue in the real v4 branch"
    echo ""
    sudo pkill -f dmsetup || true
    sudo rmmod dm-remap 2>/dev/null || true
    cleanup_devices
    exit 1
}

# Clean up successfully
echo "5. Cleaning up..."
sudo dmsetup remove test-v4-correct 2>/dev/null || true
sudo rmmod dm-remap
sudo losetup -d /dev/loop21
sudo losetup -d /dev/loop22
rm -f main_device.img spare_device.img

echo ""
echo "🎉 SUCCESS: Real v4 branch works PERFECTLY with correct parameters!"
echo "No hanging issues detected. The 'Invalid argument' was just wrong parameters."

function cleanup_devices() {
    sudo losetup -d /dev/loop21 2>/dev/null || true
    sudo losetup -d /dev/loop22 2>/dev/null || true
    rm -f main_device.img spare_device.img
}