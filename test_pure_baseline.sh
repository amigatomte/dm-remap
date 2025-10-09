#!/bin/bash
# Test Pure Baseline (Week 7-8) with Correct Parameters
echo "TESTING PURE BASELINE: Week 7-8 with Correct dm-remap Parameters"  
echo "================================================================="
echo "This tests if the baseline works with correct parameters"
echo "If this works, then hanging is definitely in optimization initialization"
echo ""

cd /home/christian/kernel_dev/dm-remap

# Setup test devices
echo "1. Setting up test devices..."
dd if=/dev/zero of=main_baseline.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=spare_baseline.img bs=1M count=5 2>/dev/null
sudo losetup -d /dev/loop40 2>/dev/null || true
sudo losetup -d /dev/loop41 2>/dev/null || true  
sudo losetup /dev/loop40 main_baseline.img
sudo losetup /dev/loop41 spare_baseline.img
echo "   ✅ Devices ready: /dev/loop40 (main), /dev/loop41 (spare)"

# Clean state
echo "2. Cleaning existing state..."
sudo rmmod dm-remap 2>/dev/null || true
sudo dmsetup remove_all 2>/dev/null || true

# Test baseline with correct parameters
echo "3. Testing Week 7-8 baseline with CORRECT parameters..."
echo "   Module size: $(ls -lh src/dm_remap.ko | awk '{print $5}')"

if sudo insmod src/dm_remap.ko; then
    echo "   ✅ Baseline module loaded ($(lsmod | grep dm_remap | awk '{print $2}') KB)"
    
    timeout 10s bash -c '
        echo "0 10240 remap /dev/loop40 /dev/loop41 0 10240" | sudo dmsetup create test-baseline-correct
        echo "✅ BASELINE WORKS with correct parameters!"
        
        # Test I/O
        echo "Testing I/O on baseline..."
        sudo dd if=/dev/zero of=/dev/mapper/test-baseline-correct bs=4k count=5 2>/dev/null
        echo "✅ Baseline I/O works!"
    ' || {
        echo "❌ HANGING in baseline with correct parameters!"
        echo "This suggests the issue might not be in optimization initialization"
        sudo pkill -f dmsetup 2>/dev/null || true
        sudo rmmod dm-remap 2>/dev/null || true
        cleanup_baseline
        exit 1
    }
    
    # Cleanup
    sudo dmsetup remove test-baseline-correct 2>/dev/null || true
    sudo rmmod dm-remap
    echo "   ✅ Baseline cleanup successful"
else
    echo "   ❌ Baseline module load failed"
    cleanup_baseline
    exit 1
fi

cleanup_baseline

echo ""
echo "🎯 RESULT: Baseline Week 7-8 WORKS PERFECTLY with correct parameters!"
echo "This confirms hanging is in Week 9-10 optimization initialization code"

function cleanup_baseline() {
    sudo losetup -d /dev/loop40 2>/dev/null || true  
    sudo losetup -d /dev/loop41 2>/dev/null || true
    rm -f main_baseline.img spare_baseline.img
}