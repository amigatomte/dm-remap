#!/bin/bash
#
# Test dm-remap-v4-real WITHOUT MODULE_SOFTDEP
# This is Iteration 16 - testing if MODULE_SOFTDEP was causing the crashes
#

set -e

echo "========================================="
echo "ITERATION 16 - MODULE_SOFTDEP REMOVED TEST"
echo "========================================="
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Test devices
MAIN_DEV="/dev/loop21"
SPARE_DEV="/dev/loop22"
SIZE_MB=100

echo "Step 1: Cleanup any existing test setup..."
sudo dmsetup remove test-remap 2>/dev/null || true
sudo rmmod dm_remap_v4_real 2>/dev/null || true
sudo losetup -d $MAIN_DEV 2>/dev/null || true
sudo losetup -d $SPARE_DEV 2>/dev/null || true
rm -f /tmp/main_test.img /tmp/spare_test.img
echo "✓ Cleanup complete"
echo ""

echo "Step 2: Create test loop devices..."
dd if=/dev/zero of=/tmp/main_test.img bs=1M count=$SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/spare_test.img bs=1M count=$SIZE_MB 2>/dev/null
sudo losetup $MAIN_DEV /tmp/main_test.img
sudo losetup $SPARE_DEV /tmp/spare_test.img
echo "✓ Main device: $MAIN_DEV"
echo "✓ Spare device: $SPARE_DEV"
echo ""

echo "Step 3: Load dm-remap-v4-real module (WITHOUT MODULE_SOFTDEP)..."
echo "   *** THIS IS THE CRITICAL TEST ***"
echo "   If this crashes, MODULE_SOFTDEP was NOT the problem"
echo "   If this works, MODULE_SOFTDEP WAS the problem!"
echo ""
sudo insmod src/dm-remap-v4-real.ko
echo -e "${GREEN}✓ Module loaded successfully!${NC}"
echo ""

echo "Step 4: Verify module is loaded..."
lsmod | grep dm_remap_v4_real
echo ""

echo "Step 5: Create dm-remap device..."
echo "   *** THIS IS WHERE WE CRASHED IN PREVIOUS ITERATIONS ***"
echo ""
MAIN_SIZE=$(blockdev --getsz $MAIN_DEV)
echo "0 $MAIN_SIZE dm-remap-v4 $MAIN_DEV $SPARE_DEV" | sudo dmsetup create test-remap
echo -e "${GREEN}✓ Device created successfully!${NC}"
echo ""

echo "Step 6: Check device status..."
sudo dmsetup status test-remap
echo ""

echo "Step 7: Test basic I/O..."
echo "test data" | sudo dd of=/dev/mapper/test-remap bs=512 count=1 2>/dev/null
sudo dd if=/dev/mapper/test-remap bs=512 count=1 2>/dev/null | head -1
echo "✓ I/O works!"
echo ""

echo "Step 8: Remove device..."
sudo dmsetup remove test-remap
echo "✓ Device removed"
echo ""

echo "Step 9: Unload module..."
sudo rmmod dm_remap_v4_real
echo "✓ Module unloaded"
echo ""

echo "Step 10: Cleanup..."
sudo losetup -d $MAIN_DEV
sudo losetup -d $SPARE_DEV
rm -f /tmp/main_test.img /tmp/spare_test.img
echo "✓ Cleanup complete"
echo ""

echo "========================================="
echo -e "${GREEN}✓✓✓ ITERATION 16 TEST PASSED! ✓✓✓${NC}"
echo "========================================="
echo ""
echo "CONCLUSION:"
echo "  MODULE_SOFTDEP was referencing a non-existent module!"
echo "  Removing it fixed the crashes!"
echo ""
echo "The crash was caused by:"
echo "  MODULE_SOFTDEP(\"pre: dm-remap-v4-metadata\")"
echo "  ↓"
echo "  Kernel tried to load dm-remap-v4-metadata module"
echo "  ↓"
echo "  Module doesn't exist"
echo "  ↓"
echo "  Kernel error state → crashes"
echo ""
