#!/bin/bash
#
# Test that remaps trigger immediate metadata persistence
#

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "Testing Remap-Triggered Metadata Writes"
echo "========================================="

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Must run as root${NC}"
    exit 1
fi

# Cleanup
cleanup() {
    echo "Cleaning up..."
    dmsetup remove test-remap-persist 2>/dev/null || true
    dmsetup remove dm-flakey-test 2>/dev/null || true
    losetup -d /dev/loop20 2>/dev/null || true
    losetup -d /dev/loop21 2>/dev/null || true
    losetup -d /dev/loop22 2>/dev/null || true
    rmmod dm-remap-v4-real dm-remap-v4-stats dm_flakey 2>/dev/null || true
    rm -f /tmp/main-remap-test.img /tmp/spare-remap-test.img /tmp/flakey-test.img
}

trap cleanup EXIT

# Create test images
echo "[1] Creating test devices..."
dd if=/dev/zero of=/tmp/main-remap-test.img bs=1M count=50 2>/dev/null
dd if=/dev/zero of=/tmp/spare-remap-test.img bs=1M count=50 2>/dev/null
dd if=/dev/zero of=/tmp/flakey-test.img bs=1M count=50 2>/dev/null

losetup /dev/loop20 /tmp/flakey-test.img
losetup /dev/loop21 /tmp/spare-remap-test.img

# Load modules
echo "[2] Loading modules..."
rmmod dm-remap-v4-real dm-remap-v4-stats dm_flakey 2>/dev/null || true
modprobe dm_flakey
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-stats.ko
insmod /home/christian/kernel_dev/dm-remap/src/dm-remap-v4-real.ko

# Create dm-flakey device that will fail I/O on demand
echo "[3] Creating dm-flakey device (to simulate failures)..."
SECTORS=$(blockdev --getsz /dev/loop20)
# Start with device UP (no errors)
echo "0 $SECTORS flakey /dev/loop20 0 0 0" | dmsetup create dm-flakey-test

# Create dm-remap on top of flakey
echo "[4] Creating dm-remap device..."
echo "0 $SECTORS dm-remap-v4 /dev/mapper/dm-flakey-test /dev/loop21" | dmsetup create test-remap-persist

# Write test data
echo "[5] Writing test data..."
echo "TEST DATA FOR REMAP" | dd of=/dev/mapper/test-remap-persist bs=512 seek=100 count=1 conv=notrunc 2>/dev/null

# Check initial metadata - should be empty
echo "[6] Checking initial metadata state..."
INITIAL_MAGIC=$(dd if=/dev/loop21 bs=4 count=1 2>/dev/null | xxd -p)
if [ "$INITIAL_MAGIC" = "34524d44" ]; then
    echo -e "${YELLOW}⚠ Metadata already exists (from previous test?)${NC}"
else
    echo "✓ No metadata yet (as expected for fresh device)"
fi

# Now make the flakey device fail on sector 100
echo "[7] Causing I/O error on sector 100..."
dmsetup suspend test-remap-persist
dmsetup reload dm-flakey-test --table "0 $SECTORS flakey /dev/loop20 0 30 1"  # Drop all I/O
dmsetup resume dm-flakey-test
dmsetup resume test-remap-persist

# Try to read the sector - this should fail and trigger remap
echo "[8] Reading from failed sector (triggers automatic remap)..."
dd if=/dev/mapper/test-remap-persist of=/dev/null bs=512 skip=100 count=1 2>/dev/null || echo "  (Error expected - triggering remap)"

# Give kernel time to process the remap and write metadata
echo "[9] Waiting for metadata write..."
sleep 2

# Check if metadata was written
echo "[10] Checking if metadata was persisted..."
MAGIC=$(dd if=/dev/loop21 bs=4 count=1 2>/dev/null | xxd -p)
if [ "$MAGIC" = "34524d44" ]; then
    echo -e "${GREEN}✓ SUCCESS: Metadata was written after remap!${NC}"
    echo "   Magic bytes: 0x$MAGIC (DMR4)"
    
    # Check all 5 copies
    echo "   Checking all metadata copies..."
    for sector in 0 1024 2048 4096 8192; do
        COPY_MAGIC=$(dd if=/dev/loop21 bs=512 skip=$sector count=1 2>/dev/null | xxd -p -l 4)
        if [ "$COPY_MAGIC" = "34524d44" ]; then
            echo "   ✓ Copy at sector $sector: valid"
        else
            echo "   ✗ Copy at sector $sector: missing/invalid"
        fi
    done
else
    echo -e "${RED}✗ FAILED: No metadata found after remap${NC}"
    echo "   Magic bytes: 0x$MAGIC (expected: 0x34524d44)"
    echo ""
    echo "   This means remap-triggered metadata persistence is NOT working!"
fi

# Check kernel messages
echo ""
echo "[11] Checking kernel messages..."
sudo dmesg | tail -30 | grep -E "remap|metadata|I/O error" || echo "No relevant messages"

echo ""
echo "========================================="
echo "Test complete"
echo "========================================="
