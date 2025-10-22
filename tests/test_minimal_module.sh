#!/bin/bash
#
# Test script for minimal dm-target module
# This tests if the basic module loading works without our complex code
#

set -e  # Exit on error

echo "========================================="
echo "MINIMAL MODULE TEST"
echo "========================================="
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test device
LOOP_DEV="/dev/loop20"
LOOP_SIZE_MB=100

echo "Step 1: Unload any existing dm-remap modules..."
sudo rmmod dm_remap_minimal_test 2>/dev/null || true
sudo rmmod dm_remap_v4_real 2>/dev/null || true
echo "✓ Modules unloaded (if they were loaded)"
echo ""

echo "Step 2: Create test loop device..."
if [ ! -e "$LOOP_DEV" ]; then
    sudo mknod "$LOOP_DEV" b 7 20 2>/dev/null || true
fi
sudo losetup -d "$LOOP_DEV" 2>/dev/null || true
dd if=/dev/zero of=/tmp/minimal_test.img bs=1M count=$LOOP_SIZE_MB 2>/dev/null
sudo losetup "$LOOP_DEV" /tmp/minimal_test.img
echo "✓ Loop device created: $LOOP_DEV"
echo ""

echo "Step 3: Load MINIMAL test module..."
echo "   This module has NO complex code - just basic dm-target structure"
echo "   If this crashes, the problem is fundamental (kernel compatibility)"
echo "   If this works, the problem is in our complex initialization code"
echo ""
sudo insmod src/dm-remap-minimal-test.ko
echo "✓ Minimal module loaded successfully!"
echo ""

echo "Step 4: Check module is loaded..."
lsmod | grep minimal_test
echo ""

echo "Step 5: Create minimal dm-target device..."
echo "0 $(blockdev --getsz $LOOP_DEV) minimal-test $LOOP_DEV" | sudo dmsetup create minimal-test-device
echo "✓ Minimal device created successfully!"
echo ""

echo "Step 6: Check device status..."
sudo dmsetup status minimal-test-device
echo ""

echo "Step 7: Test basic I/O on the device..."
echo "   Writing test data..."
echo "test data" | sudo dd of=/dev/mapper/minimal-test-device bs=512 count=1 2>/dev/null
echo "   Reading test data..."
sudo dd if=/dev/mapper/minimal-test-device bs=512 count=1 2>/dev/null | head -1
echo "✓ I/O test complete!"
echo ""

echo "Step 8: Remove minimal device..."
sudo dmsetup remove minimal-test-device
echo "✓ Device removed successfully!"
echo ""

echo "Step 9: Unload minimal module..."
sudo rmmod dm_remap_minimal_test
echo "✓ Module unloaded successfully!"
echo ""

echo "Step 10: Cleanup loop device..."
sudo losetup -d "$LOOP_DEV"
rm -f /tmp/minimal_test.img
echo "✓ Cleanup complete!"
echo ""

echo "========================================="
echo -e "${GREEN}✓ MINIMAL MODULE TEST PASSED!${NC}"
echo "========================================="
echo ""
echo "CONCLUSION:"
echo "  The minimal dm-target module works perfectly."
echo "  This means the crash is NOT a fundamental kernel issue."
echo "  The problem is in the COMPLEX INITIALIZATION CODE in dm-remap-v4-real."
echo ""
echo "Next steps:"
echo "  1. Systematically remove complex features from dm-remap-v4-real"
echo "  2. Test after each removal to find the problematic code"
echo "  3. Focus on: workqueue, global stats, MODULE_SOFTDEP, suspend hooks"
echo ""
