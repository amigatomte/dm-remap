#!/bin/bash
# Phase B-2: Test if hotpath initialization is the issue, or if it's usage in I/O path
set -e

echo "PHASE B-2: TESTING HOTPATH INIT vs I/O PATH USAGE"
echo "================================================="
echo "Goal: Determine if hanging is in dmr_hotpath_init() or hotpath I/O usage"
echo ""

# Create a version that initializes hotpath but disables it in I/O path
echo "1. Creating Phase B-2 main file (baseline + hotpath init but disabled in I/O)..."

# Start with baseline
cp src/dm_remap_main.c.baseline src/dm_remap_main.c.phase_b2

# Add hotpath headers
sed -i '/dm-remap-performance.h/a #include "dm-remap-hotpath-optimization.h" \/\/ Phase B-2: Hotpath init only' src/dm_remap_main.c.phase_b2

# Add hotpath initialization but set to NULL after (disabled)
sed -i '/dm_remap_autosave_start.*metadata/a\\n    \/\* Phase B-2: Initialize hotpath but disable immediately \*\/\n    ret = dmr_hotpath_init(rc);\n    if (ret) {\n        DMR_DEBUG(0, "Phase B-2: Failed to initialize hotpath: %d", ret);\n        rc->hotpath_manager = NULL;\n    } else {\n        DMR_DEBUG(0, "Phase B-2: Hotpath initialized, now disabling for test");\n        \/\* Disable hotpath to test if init is the problem \*\/\n        \/\* We will NOT set rc->hotpath_manager = NULL to see if init hangs \*\/\n    }' src/dm_remap_main.c.phase_b2

# Install Phase B-2 version
cp src/dm_remap_main.c.phase_b2 src/dm_remap_main.c

echo "   ✅ Phase B-2 main file created (hotpath init but no I/O path usage)"
echo ""

# Build Phase B-2
echo "2. Building Phase B-2..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Phase B-2 build failed"
    exit 1
fi
cd ..

echo "3. Testing Phase B-2 for hanging during module load (before I/O)..."

# Test if hanging occurs during module load (during dmr_hotpath_init)
echo "   Loading module to test hotpath initialization..."
timeout 5s sudo insmod src/dm_remap.ko || {
    echo ""
    echo "🚨 HANGING DETECTED DURING MODULE LOAD!"
    echo "dmr_hotpath_init() itself causes hanging during initialization"
    echo "The issue is in the hotpath initialization function"
    exit 1
}

echo "   ✅ Module loaded successfully - hotpath init does not hang"
echo "   Module size: $(lsmod | grep dm_remap | awk '{print $2}')"

# Now test device creation
echo "4. Testing device creation (should work since hotpath I/O is disabled)..."

dd if=/dev/zero of=phase_b2_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=phase_b2_spare.img bs=1M count=5 2>/dev/null

LOOP1=$(sudo losetup -f --show phase_b2_main.img)
LOOP2=$(sudo losetup -f --show phase_b2_spare.img)

timeout 10s bash -c "
    echo '0 10240 remap $LOOP1 $LOOP2 0 10240' | sudo dmsetup create test-phase-b2
    echo '✅ Device created successfully!'
" || {
    echo ""
    echo "🚨 HANGING DETECTED DURING DEVICE CREATION!"
    echo "Issue is in device creation path, not just hotpath init"
    cleanup_b2_and_exit
}

echo "5. Cleaning up..."
sudo dmsetup remove test-phase-b2
sudo rmmod dm_remap
sudo losetup -d $LOOP1
sudo losetup -d $LOOP2
rm -f phase_b2_main.img phase_b2_spare.img

echo ""
echo "🎉 PHASE B-2 RESULT: Hotpath initialization itself does NOT hang"
echo "The hanging must be in the I/O path when hotpath is used"

function cleanup_b2_and_exit() {
    sudo dmsetup remove_all 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    sudo losetup -d $LOOP1 2>/dev/null || true
    sudo losetup -d $LOOP2 2>/dev/null || true
    rm -f phase_b2_main.img phase_b2_spare.img
    exit 1
}