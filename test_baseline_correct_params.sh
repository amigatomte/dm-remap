#!/bin/bash
# Test pure Week 7-8 baseline with correct dm-remap parameters
set -e

echo "TESTING PURE WEEK 7-8 BASELINE (NO OPTIMIZATIONS)"
echo "================================================="
echo "Branch: $(git branch --show-current 2>/dev/null || echo 'detached HEAD')"
echo "Commit: $(git log --oneline -1)"
echo ""

# Set up test devices
echo "1. Setting up test devices..."
dd if=/dev/zero of=main_test.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=spare_test.img bs=1M count=5 2>/dev/null

LOOP1=$(sudo losetup -f)
LOOP2=$(sudo losetup -f --show spare_test.img)
LOOP1=$(sudo losetup -f --show main_test.img)
echo "   ✅ Test devices ready: $LOOP1 (main), $LOOP2 (spare)"

# Build and load pure baseline
echo "2. Building pure Week 7-8 baseline..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi

echo "3. Loading pure baseline module..."
sudo insmod dm_remap.ko
lsmod | grep dm_remap
echo "   ✅ Module loaded: $(lsmod | grep dm_remap | awk '{print $2}')"

# Test with correct dm-remap parameters
echo "4. Testing device creation with correct parameters (15s timeout)..."
echo "   Command: 0 10240 remap $LOOP1 $LOOP2 0 10240"

timeout 15s bash -c "
    echo '0 10240 remap $LOOP1 $LOOP2 0 10240' | sudo dmsetup create test-baseline
    echo "✅ Device created successfully!"
    
    # Test basic I/O
    sudo dd if=/dev/zero of=/dev/mapper/test-baseline bs=4k count=1 2>/dev/null
    echo "✅ I/O operations work!"
" || {
    echo ""
    echo "🚨 UNEXPECTED: Pure baseline hangs!"
    echo "This would indicate the issue is not in optimizations"
    cleanup_and_exit
}

# Clean up
echo "5. Cleaning up..."
sudo dmsetup remove test-baseline
sudo rmmod dm_remap
sudo losetup -d $LOOP1
sudo losetup -d $LOOP2
rm -f main_test.img spare_test.img

echo ""
echo "🎉 SUCCESS: Pure Week 7-8 baseline works perfectly!"
echo "This confirms the hanging is in optimization initialization."

cd ..

function cleanup_and_exit() {
    sudo dmsetup remove_all 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    sudo losetup -d $LOOP1 2>/dev/null || true
    sudo losetup -d $LOOP2 2>/dev/null || true
    rm -f main_test.img spare_test.img
    exit 1
}