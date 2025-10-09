#!/bin/bash
# Minimal Hotpath Debug: Test each step of dmr_hotpath_init() 
set -e

echo "MINIMAL HOTPATH DEBUG: STEP-BY-STEP ANALYSIS"
echo "============================================"
echo "Goal: Identify which specific line in dmr_hotpath_init() causes hanging"
echo ""

# Strategy: Create a minimal hotpath init that only does basic allocation
echo "1. Creating minimal hotpath initialization test..."

# First, let's look at the problematic dmr_hotpath_init function
echo "Examining dmr_hotpath_init() function..."
echo "Key steps in the function:"
echo "- manager = dmr_alloc_cache_aligned(sizeof(*manager))"
echo "- Initialize manager fields"
echo "- Set rc->hotpath_manager = manager"
echo ""

# Create a test version with MINIMAL hotpath init (just allocation)
cp src/dm_remap_main.c.baseline src/dm_remap_main.c.minimal_hotpath

# Add includes
sed -i '/dm-remap-performance.h/a #include "dm-remap-hotpath-optimization.h"' src/dm_remap_main.c.minimal_hotpath

# Add MINIMAL hotpath init - just test the allocation
cat >> temp_hotpath_init.txt << 'EOF'

    /* MINIMAL HOTPATH TEST: Only test allocation */
    {
        struct dmr_hotpath_manager *test_manager;
        DMR_DEBUG(0, "Testing hotpath allocation...");
        
        test_manager = kmalloc(sizeof(*test_manager), GFP_KERNEL);
        if (!test_manager) {
            DMR_DEBUG(0, "Hotpath allocation failed");
        } else {
            DMR_DEBUG(0, "Hotpath allocation successful");
            kfree(test_manager);
            DMR_DEBUG(0, "Hotpath deallocation successful");
        }
    }
EOF

# Insert this after autosave start
sed -i '/dm_remap_autosave_start.*metadata/r temp_hotpath_init.txt' src/dm_remap_main.c.minimal_hotpath
rm temp_hotpath_init.txt

# Install minimal version
cp src/dm_remap_main.c.minimal_hotpath src/dm_remap_main.c

echo "   ✅ Minimal hotpath test created"
echo ""

# Build minimal test
echo "2. Building minimal hotpath test..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi
cd ..

echo "3. Testing minimal hotpath (just allocation)..."

# Test module loading with basic allocation
echo "   Testing module load..."
timeout 8s sudo insmod src/dm_remap.ko || {
    echo ""
    echo "🚨 HANGING DETECTED in basic allocation test!"
    echo "Issue might be in kmalloc or basic memory allocation"
    exit 1
}

echo "   ✅ Module loaded - basic allocation works"
lsmod | grep dm_remap

echo "4. Testing device creation..."
dd if=/dev/zero of=minimal_main.img bs=1M count=2 2>/dev/null
dd if=/dev/zero of=minimal_spare.img bs=1M count=2 2>/dev/null

LOOP1=$(sudo losetup -f --show minimal_main.img)
LOOP2=$(sudo losetup -f --show minimal_spare.img)

timeout 8s bash -c "
    echo '0 4096 remap $LOOP1 $LOOP2 0 4096' | sudo dmsetup create test-minimal
    echo '✅ Device created successfully!'
" || {
    echo ""
    echo "🚨 HANGING in device creation with minimal hotpath"
    cleanup_minimal
    exit 1
}

echo "5. Cleaning up..."
sudo dmsetup remove test-minimal
sudo rmmod dm_remap
sudo losetup -d $LOOP1
sudo losetup -d $LOOP2
rm -f minimal_main.img minimal_spare.img

echo ""
echo "🎉 MINIMAL TEST PASSED!"
echo "Basic allocation is NOT the issue. The problem is deeper in dmr_hotpath_init()"
echo ""
echo "Next step: Test individual components of dmr_hotpath_init()"

function cleanup_minimal() {
    sudo dmsetup remove_all 2>/dev/null || true
    sudo rmmod dm_remap 2>/dev/null || true
    sudo losetup -d $LOOP1 2>/dev/null || true
    sudo losetup -d $LOOP2 2>/dev/null || true
    rm -f minimal_main.img minimal_spare.img
}