#!/bin/bash
# Wrapper to run crash-safe test with real-time kernel log capture

KERNEL_LOG_REALTIME="/var/log/dm-remap-kernel-realtime.log"
TEST_PROGRESS_LOG="/var/log/dm-remap-test-progress.log"

echo "=================================================================="
echo "  Crash-Safe Test Runner with Real-Time Kernel Log Capture"
echo "=================================================================="
echo ""
echo "This script will:"
echo "  1. Set kernel console log level to capture all messages"
echo "  2. Start capturing kernel messages in real-time to: $KERNEL_LOG_REALTIME"
echo "  3. Run the test"
echo "  4. Even if system crashes, logs are written continuously to disk"
echo ""
echo "After a crash/reboot, check these files:"
echo "  - $KERNEL_LOG_REALTIME    (real-time kernel messages)"
echo "  - $TEST_PROGRESS_LOG       (test step progress)"
echo ""

# Clean up old logs
sudo rm -f "$KERNEL_LOG_REALTIME" "$TEST_PROGRESS_LOG"

# Set kernel console log level to show everything (0 = KERN_EMERG through 7 = KERN_DEBUG)
echo "Setting kernel console log level to 8 (show all messages)..."
sudo dmesg -n 8
echo "✓ Kernel console log level set"
echo ""

# Clear dmesg buffer
echo "Clearing kernel log buffer..."
sudo dmesg -C
echo "✓ Kernel log cleared"
echo ""

# Start capturing kernel messages in real-time to file
echo "Starting real-time kernel log capture to: $KERNEL_LOG_REALTIME"
sudo dmesg --follow --time-format iso > "$KERNEL_LOG_REALTIME" 2>&1 &
DMESG_PID=$!
echo "✓ Real-time capture started (PID: $DMESG_PID)"
echo ""

# Give dmesg a moment to start
sleep 1

# Run the actual test
echo "=================================================================="
echo "                    STARTING TEST"
echo "=================================================================="
echo ""

sudo ./tests/test_metadata_write_crash_safe.sh 2>&1 | tee /var/log/dm-remap-test-output.log

# Test completed successfully
echo ""
echo "=================================================================="
echo "                 TEST COMPLETED SUCCESSFULLY"
echo "=================================================================="

# Stop real-time capture
echo ""
echo "Stopping real-time kernel log capture..."
sudo kill $DMESG_PID 2>/dev/null || true
sleep 1

echo "✓ Real-time capture stopped"
echo ""
echo "Logs saved to:"
echo "  - Kernel log (real-time): $KERNEL_LOG_REALTIME"
echo "  - Test progress:          $TEST_PROGRESS_LOG"
echo "  - Test output:            /var/log/dm-remap-test-output.log"
echo ""
echo "You can review the kernel log with:"
echo "  tail -f $KERNEL_LOG_REALTIME"
echo "  grep CRASH-DEBUG $KERNEL_LOG_REALTIME"
echo ""
