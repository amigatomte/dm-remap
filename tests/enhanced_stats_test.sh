#!/bin/bash

# dm-remap v2.0 Enhanced Statistics Test
# Tests the improved statistics tracking functionality

set -e

echo "=== dm-remap v2.0 Enhanced Statistics Test ==="
echo

# Test 1: Initial clean state
echo "Test 1: Initial State Verification"
echo "----------------------------------"
STATUS=$(sudo dmsetup status test-remap-stats)
echo "Initial status: $STATUS"

if [[ $STATUS =~ manual_remaps=0 ]] && [[ $STATUS =~ auto_remaps=0 ]]; then
    echo "✓ Clean initial state confirmed"
else
    echo "✗ Initial state not clean"
    exit 1
fi
echo

# Test 2: Single manual remap
echo "Test 2: Single Manual Remap Statistics"
echo "--------------------------------------"
echo "Performing manual remap of sector 5000..."
sudo dmsetup message test-remap-stats 0 remap 5000

STATUS=$(sudo dmsetup status test-remap-stats)
echo "Status after 1 remap: $STATUS"

if [[ $STATUS =~ manual_remaps=1 ]] && [[ $STATUS =~ "1/100 0/100 1/100" ]]; then
    echo "✓ Single remap statistics accurate"
else
    echo "✗ Single remap statistics incorrect"
    exit 1
fi
echo

# Test 3: Multiple manual remaps
echo "Test 3: Multiple Manual Remap Statistics"
echo "----------------------------------------"
echo "Performing additional remaps..."
sudo dmsetup message test-remap-stats 0 remap 6000
sudo dmsetup message test-remap-stats 0 remap 7000
sudo dmsetup message test-remap-stats 0 remap 8000

STATUS=$(sudo dmsetup status test-remap-stats)
echo "Status after 4 total remaps: $STATUS"

if [[ $STATUS =~ manual_remaps=4 ]] && [[ $STATUS =~ "4/100 0/100 4/100" ]]; then
    echo "✓ Multiple remap statistics accurate"
else
    echo "✗ Multiple remap statistics incorrect"
    exit 1
fi
echo

# Test 4: Clear command statistics reset
echo "Test 4: Clear Command Statistics Reset"
echo "-------------------------------------"
echo "Testing clear command..."
sudo dmsetup message test-remap-stats 0 clear

STATUS=$(sudo dmsetup status test-remap-stats)
echo "Status after clear: $STATUS"

if [[ $STATUS =~ manual_remaps=0 ]] && [[ $STATUS =~ auto_remaps=0 ]] && [[ $STATUS =~ "0/100 0/100 0/100" ]]; then
    echo "✓ Clear command properly resets statistics"
else
    echo "✗ Clear command statistics reset failed"
    exit 1
fi
echo

# Test 5: Verify command functionality
echo "Test 5: Verify Command Functionality"
echo "------------------------------------"
echo "Testing remap and verify sequence..."

# Add a remap
sudo dmsetup message test-remap-stats 0 remap 9000

# Verify it exists (command should complete without error)
sudo dmsetup message test-remap-stats 0 verify 9000 2>/dev/null
echo "✓ Verify command executed successfully"

# Check statistics updated
STATUS=$(sudo dmsetup status test-remap-stats)
echo "Final status: $STATUS"

if [[ $STATUS =~ manual_remaps=1 ]]; then
    echo "✓ Statistics properly updated after verify test"
else
    echo "✗ Statistics not updated properly"
    exit 1
fi
echo

# Test 6: Statistics persistence across operations
echo "Test 6: Statistics Persistence Verification"
echo "-------------------------------------------"
echo "Testing statistics persistence across multiple operations..."

# Start fresh
sudo dmsetup message test-remap-stats 0 clear

# Do several operations
sudo dmsetup message test-remap-stats 0 remap 11000
sudo dmsetup message test-remap-stats 0 remap 12000
sudo dmsetup message test-remap-stats 0 verify 11000

STATUS=$(sudo dmsetup status test-remap-stats)
echo "Status after mixed operations: $STATUS"

if [[ $STATUS =~ manual_remaps=2 ]] && [[ $STATUS =~ "2/100 0/100 2/100" ]]; then
    echo "✓ Statistics properly maintained across operations"
else
    echo "✗ Statistics not maintained properly"
    exit 1
fi
echo

# Summary
echo "=== Enhanced Statistics Test Summary ==="
echo "✓ Initial state verification: Working"
echo "✓ Single manual remap tracking: Working" 
echo "✓ Multiple manual remap tracking: Working"
echo "✓ Clear command statistics reset: Working"
echo "✓ Verify command functionality: Working"
echo "✓ Statistics persistence verification: Working"
echo
echo "🎉 STATISTICS TRACKING ENHANCEMENT: COMPLETE!"
echo
echo "The v2.0 manual_remaps counter now provides accurate tracking"
echo "of user-initiated remapping operations, giving excellent"
echo "visibility into device mapper target activity."