#!/bin/bash
# Test hanging v4 branch to confirm contrast with working baseline
set -e

echo "TESTING v4 BRANCH (WITH OPTIMIZATIONS) FOR HANGING"
echo "=================================================="
echo "Branch: $(git branch --show-current)"
echo "Commit: $(git log --oneline -1)"
echo ""

# Set up test devices
echo "1. Setting up test devices..."
dd if=/dev/zero of=v4_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=v4_spare.img bs=1M count=5 2>/dev/null

LOOP1=$(sudo losetup -f --show v4_main.img)
LOOP2=$(sudo losetup -f --show v4_spare.img)
echo "   ✅ Test devices ready: $LOOP1 (main), $LOOP2 (spare)"

# Build v4 with optimizations
echo "2. Building v4 with optimizations..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi

echo "3. Loading v4 module with optimizations..."
sudo insmod dm_remap.ko
lsmod | grep dm_remap
echo "   ✅ Module loaded: $(lsmod | grep dm_remap | awk '{print $2}')"

# Test with correct dm-remap parameters - THIS SHOULD HANG
echo "4. Testing device creation (15s timeout - EXPECTING HANG)..."
echo "   Command: 0 10240 remap $LOOP1 $LOOP2 0 10240"

timeout 15s bash -c "
    echo '0 10240 remap $LOOP1 $LOOP2 0 10240' | sudo dmsetup create test-v4-hang
    echo '✅ Device created - NO HANG DETECTED!'
    
    # If we get here, test I/O
    sudo dd if=/dev/zero of=/dev/mapper/test-v4-hang bs=4k count=1 2>/dev/null
    echo '✅ I/O operations work!'
" || {
    echo ""
    echo "🎯 HANGING CONFIRMED in v4 branch!"
    echo "This proves the optimization initialization causes hanging."
    cleanup_v4_and_exit
}

# Clean up if no hang
echo "5. Cleaning up..."
sudo dmsetup remove test-v4-hang
sudo rmmod dm_remap
sudo losetup -d $LOOP1
sudo losetup -d $LOOP2
rm -f v4_main.img v4_spare.img

echo ""
echo "🤔 UNEXPECTED: v4 branch did not hang!"
echo "This needs further investigation..."

cd ..

function cleanup_v4_and_exit() {
    sudo dmsetup remove_all 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    sudo losetup -d $LOOP1 2>/dev/null || true
    sudo losetup -d $LOOP2 2>/dev/null || true
    rm -f v4_main.img v4_spare.img
    cd ..
    exit 1
}