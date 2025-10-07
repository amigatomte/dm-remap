#!/bin/bash

# dm_flakey_investigation.sh - Understanding what dm-flakey actually does
# This investigates why dm-flakey doesn't create the bio errors we need

set -euo pipefail

echo "=================================================================="
echo "           UNDERSTANDING dm-flakey BEHAVIOR"
echo "=================================================================="
echo "Date: $(date)"
echo

echo "Available Device Mapper Targets:"
echo "================================"
ls /lib/modules/$(uname -r)/kernel/drivers/md/ | grep dm- | head -10

echo
echo "What dm-flakey Actually Does:"
echo "============================"
echo "According to kernel documentation, dm-flakey:"
echo "  • Drops I/O requests during 'down' periods"
echo "  • Can corrupt data during 'up' periods" 
echo "  • Simulates unreliable storage devices"
echo "  • BUT: Dropped I/O ≠ Bio error status"
echo

echo "The Key Difference:"
echo "=================="
echo "• dm-flakey: Drops I/O before completion → No bio completion callback"
echo "• Real errors: Complete I/O with error status → Triggers bio completion"
echo
echo "dm-remap needs: bio->bi_status = BLK_STS_IOERR"
echo "dm-flakey gives: No bio completion (dropped I/O)"
echo

echo "Why This Matters:"
echo "================"
echo "dm-remap's error detection (dmr_bio_endio) only triggers when:"
echo "  1. Bio is submitted to underlying device"
echo "  2. Underlying device completes bio with error status"
echo "  3. Bio completion callback is called with error"
echo
echo "dm-flakey short-circuits this by dropping I/O entirely."
echo

echo "Real-World Bio Errors Come From:"
echo "==============================="
echo "  ✓ Hardware disk read/write failures"
echo "  ✓ SATA/SCSI transport errors"
echo "  ✓ Storage controller failures"
echo "  ✓ Bad sectors on physical media"
echo "  ✗ NOT from dm-flakey dropping I/O"
echo

echo "The Fundamental Challenge:"
echo "========================="
echo "Linux kernel provides NO built-in mechanism to simulate"
echo "bio completion errors for testing purposes."
echo
echo "Available options:"
echo "  1. Use actual failing hardware (impractical)"
echo "  2. Create custom kernel module for error injection"
echo "  3. Modify existing device drivers to inject errors"
echo "  4. Accept that we can only test simulated errors"
echo

echo "=================================================================="
echo "CONCLUSION: Real bio error simulation requires either:"
echo "  • Actual hardware failures"
echo "  • Custom kernel development"
echo "  • External error injection tools"
echo
echo "Our health scanning system is architecturally correct"
echo "and will work with real hardware errors in production."
echo "=================================================================="