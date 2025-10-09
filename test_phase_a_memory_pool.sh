#!/bin/bash
# Phase A: Test ONLY Memory Pool Initialization
set -e

echo "PHASE A: TESTING MEMORY POOL INITIALIZATION ONLY"
echo "================================================"
echo "Goal: Determine if dmr_pool_manager_init() causes hanging"
echo ""

# Create Phase A version with ONLY memory pool initialization
echo "1. Creating Phase A main file (baseline + memory pool only)..."

# Start with baseline
cp src/dm_remap_main.c.baseline src/dm_remap_main.c.phase_a

# Add memory pool header include
echo "Adding memory pool include..."
sed -i '/dm-remap-performance.h/a #include "dm-remap-memory-pool.h" \/\/ Phase A: Memory Pool Only' src/dm_remap_main.c.phase_a

# Add ONLY memory pool initialization (find the location after autosave start)
echo "Adding memory pool initialization..."
sed -i '/dm_remap_autosave_start.*metadata/a\\n    \/\* Phase A: Initialize ONLY Memory Pool System \*\/\n    ret = dmr_pool_manager_init(rc);\n    if (ret) {\n        DMR_DEBUG(0, "Phase A: Failed to initialize memory pool system: %d", ret);\n        rc->pool_manager = NULL;\n    } else {\n        DMR_DEBUG(0, "Phase A: Memory pool system initialized successfully");\n    }' src/dm_remap_main.c.phase_a

# Install Phase A version
cp src/dm_remap_main.c.phase_a src/dm_remap_main.c

echo "   ✅ Phase A main file created with ONLY memory pool initialization"
echo ""

# Build Phase A
echo "2. Building Phase A (baseline + memory pool only)..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Phase A build failed"
    exit 1
fi

echo "   ✅ Phase A built successfully"
echo "   Module size: $(ls -lh dm_remap.ko | awk '{print $5}')"
cd ..

# Test Phase A for hanging
echo "3. Testing Phase A for hanging..."

# Set up test devices
dd if=/dev/zero of=phase_a_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=phase_a_spare.img bs=1M count=5 2>/dev/null

LOOP1=$(sudo losetup -f --show phase_a_main.img)
LOOP2=$(sudo losetup -f --show phase_a_spare.img)

echo "   Test devices: $LOOP1 (main), $LOOP2 (spare)"

# Load module
sudo insmod src/dm_remap.ko
echo "   ✅ Module loaded: $(lsmod | grep dm_remap | awk '{print $2}')"

# Test device creation with shorter timeout to avoid system lockup
echo "   Testing device creation (10s timeout)..."
timeout 10s bash -c "
    echo '0 10240 remap $LOOP1 $LOOP2 0 10240' | sudo dmsetup create test-phase-a
    echo '✅ Device created successfully!'
    sudo dd if=/dev/zero of=/dev/mapper/test-phase-a bs=4k count=1 2>/dev/null
    echo '✅ I/O operations work!'
" || {
    echo ""
    echo "🚨 PHASE A RESULT: HANGING DETECTED!"
    echo "Memory Pool initialization (dmr_pool_manager_init) causes hanging"
    cleanup_phase_a
    exit 1
}

# Clean up successful test
echo "4. Cleaning up Phase A..."
sudo dmsetup remove test-phase-a
sudo rmmod dm_remap
sudo losetup -d $LOOP1
sudo losetup -d $LOOP2
rm -f phase_a_main.img phase_a_spare.img

echo ""
echo "🎉 PHASE A RESULT: NO HANGING!"
echo "Memory Pool initialization does NOT cause hanging"
echo "Ready to proceed to Phase B (Hotpath initialization)"

function cleanup_phase_a() {
    sudo dmsetup remove_all 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    sudo losetup -d $LOOP1 2>/dev/null || true
    sudo losetup -d $LOOP2 2>/dev/null || true
    rm -f phase_a_main.img phase_a_spare.img
}