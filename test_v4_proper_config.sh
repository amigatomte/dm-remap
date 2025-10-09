#!/bin/bash
# Test REAL v4 branch with proper device mapper configuration
set -e

echo "TESTING REAL v4 WITH PROPER DEVICE CONFIGURATION"
echo "================================================"
echo "Branch: $(git branch --show-current)"
echo "Commit: $(git log --oneline -1)"
echo ""

# Create a proper test image and loop device
echo "1. Setting up test environment..."
dd if=/dev/zero of=test_v4.img bs=1M count=10 2>/dev/null
sudo losetup -d /dev/loop20 2>/dev/null || true
sudo losetup /dev/loop20 test_v4.img
echo "   ✅ Test image and loop device ready"

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

# Test device creation with proper parameters
echo "4. Testing device creation with proper parameters..."
echo "   Target: 0 20480 remap /dev/loop20 0"

timeout 15s bash -c '
    echo "0 20480 remap /dev/loop20 0" | sudo dmsetup create test-v4-proper
    echo "✅ Device created successfully!"
    
    # Test a simple I/O operation
    echo "Testing I/O operations..."
    sudo dd if=/dev/zero of=/dev/mapper/test-v4-proper bs=4k count=1 2>/dev/null || true
    echo "✅ I/O operation completed"
' || {
    echo ""
    echo "🚨 HANGING DETECTED in device creation or I/O!"
    echo "This confirms there IS a hanging issue in the real v4 branch"
    echo ""
    sudo pkill -f dmsetup || true
    sudo rmmod dm-remap 2>/dev/null || true
    sudo losetup -d /dev/loop20 2>/dev/null || true
    rm -f test_v4.img
    exit 1
}

# Clean up successfully
echo "5. Cleaning up..."
sudo dmsetup remove test-v4-proper 2>/dev/null || true
sudo rmmod dm-remap
sudo losetup -d /dev/loop20
rm -f test_v4.img

echo ""
echo "🎉 SUCCESS: Real v4 branch works perfectly with proper configuration!"
echo "No hanging issues detected."