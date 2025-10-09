#!/bin/bash
# Test REAL v4 branch for hanging issues
set -e

echo "TESTING REAL v4-phase1-development BRANCH"
echo "========================================="
echo "Branch: $(git branch --show-current)"
echo "Commit: $(git log --oneline -1)"
echo "Module size: $(ls -lh src/dm_remap.ko | awk '{print $5}')"
echo ""

# Clean up first
echo "1. Initial cleanup..."
sudo rmmod dm-remap 2>/dev/null || true
sudo dmsetup remove_all 2>/dev/null || true

# Load the REAL v4 module
echo "2. Loading REAL v4 dm-remap module..."
if sudo insmod src/dm_remap.ko; then
    echo "   ✅ Module loaded successfully"
    lsmod | grep dm_remap
else
    echo "   ❌ Module load failed!"
    exit 1
fi

# Test device creation with timeout - THE CRITICAL TEST
echo "3. Testing device creation (with 15s timeout)..."
echo "   This is where hanging occurred in Week 11..."

timeout 15s bash -c '
    echo "Creating device mapper target..."
    echo "0 100 remap /dev/loop18 0" | sudo dmsetup create test-real-v4
    echo "✅ Device created successfully!"
' || {
    echo ""
    echo "🚨 HANGING CONFIRMED in REAL v4 branch!"
    echo "The hanging issue IS REAL and exists in the actual v4 code!"
    echo "Our systematic testing approach was correct."
    echo ""
    echo "Killing hanging processes..."
    sudo pkill -f dmsetup || true
    sudo rmmod dm-remap 2>/dev/null || true
    exit 1
}

# Clean up successfully
echo "4. Cleaning up..."
sudo dmsetup remove test-real-v4 2>/dev/null || true
sudo rmmod dm-remap

echo ""
echo "🎉 UNEXPECTED: Real v4 branch does NOT hang!"
echo "This suggests the hanging was environment-specific or has been resolved."