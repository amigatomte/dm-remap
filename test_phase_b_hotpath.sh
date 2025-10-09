#!/bin/bash
# Phase B: Test ONLY Hotpath Initialization
set -e

echo "PHASE B: TESTING HOTPATH INITIALIZATION ONLY"
echo "============================================="
echo "Goal: Determine if dmr_hotpath_init() causes hanging"
echo ""

# Create Phase B version with ONLY hotpath initialization (no memory pool)
echo "1. Creating Phase B main file (baseline + hotpath only)..."

# Start with baseline
cp src/dm_remap_main.c.baseline src/dm_remap_main.c.phase_b

# Add hotpath headers
echo "Adding hotpath includes..."
sed -i '/dm-remap-performance.h/a #include "dm-remap-hotpath-optimization.h" \/\/ Phase B: Hotpath Only' src/dm_remap_main.c.phase_b

# Add ONLY hotpath initialization (no memory pool, no sysfs)
echo "Adding hotpath initialization..."
sed -i '/dm_remap_autosave_start.*metadata/a\\n    \/\* Phase B: Initialize ONLY Hotpath Optimization \*\/\n    ret = dmr_hotpath_init(rc);\n    if (ret) {\n        DMR_DEBUG(0, "Phase B: Failed to initialize hotpath optimization: %d", ret);\n        rc->hotpath_manager = NULL;\n    } else {\n        DMR_DEBUG(0, "Phase B: Hotpath optimization initialized successfully");\n    }' src/dm_remap_main.c.phase_b

# Install Phase B version
cp src/dm_remap_main.c.phase_b src/dm_remap_main.c

echo "   ✅ Phase B main file created with ONLY hotpath initialization"
echo ""

# Build Phase B
echo "2. Building Phase B (baseline + hotpath only)..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Phase B build failed"
    exit 1
fi

echo "   ✅ Phase B built successfully"
echo "   Module size: $(ls -lh dm_remap.ko | awk '{print $5}')"
cd ..

# Test Phase B for hanging
echo "3. Testing Phase B for hanging..."

# Set up test devices
dd if=/dev/zero of=phase_b_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=phase_b_spare.img bs=1M count=5 2>/dev/null

LOOP1=$(sudo losetup -f --show phase_b_main.img)
LOOP2=$(sudo losetup -f --show phase_b_spare.img)

echo "   Test devices: $LOOP1 (main), $LOOP2 (spare)"

# Load module
sudo insmod src/dm_remap.ko
echo "   ✅ Module loaded: $(lsmod | grep dm_remap | awk '{print $2}')"

# Test device creation with timeout
echo "   Testing device creation (10s timeout)..."
timeout 10s bash -c "
    echo '0 10240 remap $LOOP1 $LOOP2 0 10240' | sudo dmsetup create test-phase-b
    echo '✅ Device created successfully!'
    sudo dd if=/dev/zero of=/dev/mapper/test-phase-b bs=4k count=1 2>/dev/null
    echo '✅ I/O operations work!'
" || {
    echo ""
    echo "🚨 PHASE B RESULT: HANGING DETECTED!"
    echo "Hotpath initialization (dmr_hotpath_init) causes hanging"
    cleanup_phase_b
    exit 1
}

# Clean up successful test
echo "4. Cleaning up Phase B..."
sudo dmsetup remove test-phase-b
sudo rmmod dm_remap
sudo losetup -d $LOOP1
sudo losetup -d $LOOP2
rm -f phase_b_main.img phase_b_spare.img

echo ""
echo "🎉 PHASE B RESULT: NO HANGING!"
echo "Hotpath initialization does NOT cause hanging"
echo "Ready to proceed to Phase C (Hotpath Sysfs initialization)"

function cleanup_phase_b() {
    sudo dmsetup remove_all 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    sudo losetup -d $LOOP1 2>/dev/null || true
    sudo losetup -d $LOOP2 2>/dev/null || true
    rm -f phase_b_main.img phase_b_spare.img
}