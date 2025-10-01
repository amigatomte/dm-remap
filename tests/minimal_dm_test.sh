#!/bin/bash
#
# minimal_dm_test.sh - Test with minimal device mapper setup
#

set -e

echo "=== MINIMAL DM TEST ==="

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    sudo dmsetup remove minimal-test 2>/dev/null || true
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    rm -f /tmp/minimal_main.img /tmp/minimal_data
}

trap cleanup EXIT

echo "Phase 1: Setup device"
dd if=/dev/zero of=/tmp/minimal_main.img bs=1M count=1 2>/dev/null
MAIN_LOOP=$(sudo losetup -f --show /tmp/minimal_main.img)
echo "Main: $MAIN_LOOP"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "Main sectors: $MAIN_SECTORS"

echo "Phase 2: Test with linear device mapper target (builtin)"
echo "0 $MAIN_SECTORS linear $MAIN_LOOP 0" | sudo dmsetup create minimal-test

echo "Phase 3: Write test data"
echo -n "MINIMAL" > /tmp/minimal_data

echo "Write directly to device..."
sudo dd if=/tmp/minimal_data of=$MAIN_LOOP bs=512 seek=0 count=1 conv=notrunc 2>/dev/null

echo "Read directly from device..."
sudo dd if=$MAIN_LOOP of=/tmp/minimal_data bs=512 skip=0 count=1 2>/dev/null
DIRECT_DATA=$(head -c 7 /tmp/minimal_data | tr -d '\0')
echo "Direct: '$DIRECT_DATA'"

echo "Phase 4: Test through device mapper linear target"
echo "Read through dm linear..."
sudo dd if=/dev/mapper/minimal-test of=/tmp/minimal_data bs=512 skip=0 count=1 2>/dev/null
LINEAR_DATA=$(head -c 7 /tmp/minimal_data | tr -d '\0')
echo "Linear: '$LINEAR_DATA'"

echo "Analysis:"
echo "Direct: '$DIRECT_DATA'"
echo "Linear: '$LINEAR_DATA'"

if [ "$DIRECT_DATA" = "$LINEAR_DATA" ] && [ ! -z "$LINEAR_DATA" ]; then
    echo "✅ Device mapper linear target works correctly"
    echo "This means the issue is specifically in dm-remap"
else
    echo "❌ Even device mapper linear target is broken"
    echo "This suggests a more fundamental issue"
fi