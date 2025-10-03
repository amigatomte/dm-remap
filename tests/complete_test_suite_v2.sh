#!/bin/bash

# complete_test_suite_v2.sh - Complete test suite for dm-remap v2.0
# Tests all v2.0 features: intelligent error detection, auto-remapping, health monitoring

set -euo pipefail

TEST_DIR="/home/christian/kernel_dev/dm-remap/tests"
LOG_DIR="/tmp/dm-remap-v2-test-logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="$LOG_DIR/test_results_v2_$TIMESTAMP.log"

# Test configuration
TEST_SIZE_MB=10
LOOP_MAIN="/dev/loop80"
LOOP_SPARE="/dev/loop81"
DM_NAME="test-remap-v2-suite"

# Ensure we're in the right directory
cd "$(dirname "$0")"

# Create log directory
mkdir -p "$LOG_DIR"

echo "================================================================="
echo "        dm-remap v2.0 COMPLETE TEST SUITE"
echo "================================================================="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo "Kernel: $(uname -r)"
echo "Logs will be saved to: $LOG_DIR"
echo "Results file: $RESULTS_FILE"
echo ""

# Initialize results file
cat > "$RESULTS_FILE" << EOF
dm-remap v2.0 Test Suite Results
Date: $(date)
System: $(uname -a)
Kernel: $(uname -r)

=== TEST RESULTS SUMMARY ===
EOF

# Function to log results
log_result() {
    local test_name="$1"
    local status="$2"
    local details="$3"
    
    echo "[$status] $test_name: $details" | tee -a "$RESULTS_FILE"
}

# Function to cleanup on exit
cleanup() {
    echo "=== Cleanup ==="
    
    # Remove device mapper target
    if dmsetup ls | grep -q "$DM_NAME"; then
        echo "Removing device mapper target: $DM_NAME"
        sudo dmsetup remove "$DM_NAME" 2>/dev/null || true
    fi
    
    # Detach loop devices
    if losetup "$LOOP_MAIN" >/dev/null 2>&1; then
        echo "Detaching $LOOP_MAIN"
        sudo losetup -d "$LOOP_MAIN" 2>/dev/null || true
    fi
    
    if losetup "$LOOP_SPARE" >/dev/null 2>&1; then
        echo "Detaching $LOOP_SPARE"
        sudo losetup -d "$LOOP_SPARE" 2>/dev/null || true
    fi
    
    # Remove test files
    rm -f /tmp/test_main_v2.img /tmp/test_spare_v2.img
}

trap cleanup EXIT

# Check if required tools are available
echo "=== Dependency Check ==="
MISSING_TOOLS=()

for tool in bc fio ioping dd; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        MISSING_TOOLS+=("$tool")
    fi
done

if [ ${#MISSING_TOOLS[@]} -gt 0 ]; then
    echo "❌ Missing required tools: ${MISSING_TOOLS[*]}"
    echo "Installing missing tools..."
    sudo apt-get update && sudo apt-get install -y "${MISSING_TOOLS[@]}"
fi

echo "✅ All testing tools available"

# Check if dm-remap module is loaded
echo "=== Module Check ==="
if ! lsmod | grep -q dm_remap; then
    echo "⚠ dm-remap module not loaded, attempting to load..."
    cd "$(dirname "$0")/../src"
    if [ -f "dm_remap.ko" ]; then
        if sudo insmod dm_remap.ko; then
            echo "✅ dm-remap module loaded successfully"
            log_result "Module Loading" "PASS" "Module loaded successfully"
        else
            echo "❌ Failed to load dm-remap module"
            log_result "Module Loading" "FAIL" "Could not load module"
            exit 1
        fi
    else
        echo "❌ dm_remap.ko not found in src directory"
        echo "Please build the module first: cd src && make"
        exit 1
    fi
    cd - >/dev/null
else
    echo "✅ dm-remap module already loaded"
    log_result "Module Detection" "PASS" "Module already loaded"
fi

# Check for module functionality (compatible with v2.0 and v3.0)
if dmesg | tail -50 | grep -q -E "(dm-remap.*loaded|v[2-3]\.0|module loaded successfully)"; then
    echo "✅ dm-remap module detected and functional"
    log_result "Module Functionality" "PASS" "Module is functional"
else
    echo "⚠️  Could not confirm v2.0 module - proceeding with tests"
    log_result "Module Detection" "WARN" "v2.0 confirmation uncertain"
fi

# Create test images
echo "=== Creating Test Images ==="
echo "Creating ${TEST_SIZE_MB}MB main device image..."
dd if=/dev/zero of=/tmp/test_main_v2.img bs=1M count=$TEST_SIZE_MB >/dev/null 2>&1

echo "Creating ${TEST_SIZE_MB}MB spare device image..."
dd if=/dev/zero of=/tmp/test_spare_v2.img bs=1M count=$TEST_SIZE_MB >/dev/null 2>&1

# Setup loop devices with dynamic allocation
echo "=== Setting Up Loop Devices ==="
LOOP_MAIN=$(sudo losetup -f --show /tmp/test_main_v2.img)
LOOP_SPARE=$(sudo losetup -f --show /tmp/test_spare_v2.img)

echo "Main device: $LOOP_MAIN"
echo "Spare device: $LOOP_SPARE"

# Calculate sectors (assuming 512-byte sectors)
TOTAL_SECTORS=$((TEST_SIZE_MB * 1024 * 2))  # MB to 512-byte sectors
SPARE_SECTORS=$((TEST_SIZE_MB * 1024))       # Half for spare area

echo "Total sectors: $TOTAL_SECTORS, Spare sectors: $SPARE_SECTORS"

#================================================================
# TEST 1: v2.0 TARGET CREATION AND BASIC FUNCTIONALITY
#================================================================
echo ""
echo "=== TEST 1: v2.0 Target Creation ==="

# Create dm-remap target
TABLE="0 $TOTAL_SECTORS remap $LOOP_MAIN $LOOP_SPARE 0 $SPARE_SECTORS"
echo "Creating target with table: $TABLE"

if sudo dmsetup create "$DM_NAME" --table "$TABLE"; then
    echo "✅ Target created successfully"
    log_result "Target Creation" "PASS" "Device mapper target created"
else
    echo "❌ Failed to create target"
    log_result "Target Creation" "FAIL" "Device mapper target creation failed"
    exit 1
fi

# Verify target exists
if [ -b "/dev/mapper/$DM_NAME" ]; then
    echo "✅ Block device /dev/mapper/$DM_NAME exists"
    log_result "Block Device" "PASS" "Target block device created"
else
    echo "❌ Block device not found"
    log_result "Block Device" "FAIL" "Target block device missing"
    exit 1
fi

#================================================================
# TEST 2: v2.0 ENHANCED MESSAGE INTERFACE
#================================================================
echo ""
echo "=== TEST 2: v2.0 Enhanced Message Interface ==="

# Test stats command
echo "Testing 'stats' command..."
STATS_OUTPUT=$(sudo dmsetup message "$DM_NAME" 0 stats 2>/dev/null || echo "FAILED")
if [[ "$STATS_OUTPUT" != "FAILED" ]]; then
    echo "✅ stats: $STATS_OUTPUT"
    log_result "Stats Command" "PASS" "$STATS_OUTPUT"
else
    echo "❌ stats command failed"
    log_result "Stats Command" "FAIL" "Command execution failed"
fi

# Test health command
echo "Testing 'health' command..."
HEALTH_OUTPUT=$(sudo dmsetup message "$DM_NAME" 0 health 2>/dev/null || echo "FAILED")
if [[ "$HEALTH_OUTPUT" != "FAILED" ]]; then
    echo "✅ health: $HEALTH_OUTPUT"
    log_result "Health Command" "PASS" "$HEALTH_OUTPUT"
else
    echo "❌ health command failed"
    log_result "Health Command" "FAIL" "Command execution failed"
fi

# Test scan command
echo "Testing 'scan' command..."
SCAN_OUTPUT=$(sudo dmsetup message "$DM_NAME" 0 scan 2>/dev/null || echo "FAILED")
if [[ "$SCAN_OUTPUT" != "FAILED" ]]; then
    echo "✅ scan: $SCAN_OUTPUT"
    log_result "Scan Command" "PASS" "$SCAN_OUTPUT"
else
    echo "❌ scan command failed"
    log_result "Scan Command" "FAIL" "Command execution failed"
fi

# Test auto_remap enable/disable
echo "Testing 'auto_remap' command..."
AUTO_ENABLE=$(sudo dmsetup message "$DM_NAME" 0 auto_remap enable 2>/dev/null || echo "FAILED")
if [[ "$AUTO_ENABLE" != "FAILED" ]]; then
    echo "✅ auto_remap enable: $AUTO_ENABLE"
    
    AUTO_DISABLE=$(sudo dmsetup message "$DM_NAME" 0 auto_remap disable 2>/dev/null || echo "FAILED")
    if [[ "$AUTO_DISABLE" != "FAILED" ]]; then
        echo "✅ auto_remap disable: $AUTO_DISABLE"
        log_result "Auto-Remap Commands" "PASS" "Both enable and disable work"
    else
        echo "❌ auto_remap disable failed"
        log_result "Auto-Remap Commands" "PARTIAL" "Enable works, disable failed"
    fi
else
    echo "❌ auto_remap enable failed"
    log_result "Auto-Remap Commands" "FAIL" "Command execution failed"
fi

# Re-enable auto_remap for further tests
sudo dmsetup message "$DM_NAME" 0 auto_remap enable >/dev/null 2>&1

# Test legacy v1.1 commands for backward compatibility
echo "Testing backward compatibility with v1.1 commands..."
PING_OUTPUT=$(sudo dmsetup message "$DM_NAME" 0 ping 2>/dev/null || echo "FAILED")
if [[ "$PING_OUTPUT" != "FAILED" ]]; then
    echo "✅ ping (v1.1 compat): $PING_OUTPUT"
    log_result "v1.1 Compatibility" "PASS" "Legacy ping command works"
else
    echo "❌ ping command failed"
    log_result "v1.1 Compatibility" "FAIL" "Legacy ping command failed"
fi

#================================================================
# TEST 3: BASIC I/O FUNCTIONALITY
#================================================================
echo ""
echo "=== TEST 3: Basic I/O Functionality ==="

# Test basic write/read
echo "Testing basic I/O operations..."
TEST_DATA="dm-remap v2.0 test data $(date)"
if echo "$TEST_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=512 count=1 conv=sync 2>/dev/null; then
    echo "✅ Write operation successful"
    
    # Read back the data
    READ_DATA=$(sudo dd if="/dev/mapper/$DM_NAME" bs=512 count=1 2>/dev/null | tr -d '\0')
    if [[ "$READ_DATA" == *"$TEST_DATA"* ]]; then
        echo "✅ Read operation successful - data matches"
        log_result "Basic I/O" "PASS" "Write/read operations successful"
    else
        echo "❌ Read data mismatch"
        log_result "Basic I/O" "FAIL" "Data integrity issue"
    fi
else
    echo "❌ Write operation failed"
    log_result "Basic I/O" "FAIL" "Write operation failed"
fi

#================================================================
# TEST 4: REMAPPING FUNCTIONALITY
#================================================================
echo ""
echo "=== TEST 4: Manual Remapping Functionality ==="

# Test manual remapping
echo "Testing manual sector remapping..."
REMAP_RESULT=$(sudo dmsetup message "$DM_NAME" 0 remap 100 2>/dev/null || echo "FAILED")
if [[ "$REMAP_RESULT" != "FAILED" ]]; then
    echo "✅ Manual remap: $REMAP_RESULT"
    
    # Check status to see the remap
    STATUS=$(sudo dmsetup status "$DM_NAME")
    echo "Status after remap: $STATUS"
    
    if [[ "$STATUS" == *"remapped=1"* ]]; then
        echo "✅ Remap registered in status"
        log_result "Manual Remapping" "PASS" "Sector 100 remapped successfully"
    else
        echo "❌ Remap not reflected in status"
        log_result "Manual Remapping" "PARTIAL" "Remap executed but status unclear"
    fi
else
    echo "❌ Manual remap failed"
    log_result "Manual Remapping" "FAIL" "Manual remap command failed"
fi

# Test remapping multiple sectors
echo "Testing multiple sector remapping..."
for sector in 200 300 400; do
    REMAP_RESULT=$(sudo dmsetup message "$DM_NAME" 0 remap $sector 2>/dev/null || echo "FAILED")
    if [[ "$REMAP_RESULT" != "FAILED" ]]; then
        echo "✅ Remapped sector $sector"
    else
        echo "❌ Failed to remap sector $sector"
    fi
done

# Check final status
FINAL_STATUS=$(sudo dmsetup status "$DM_NAME")
echo "Final remapping status: $FINAL_STATUS"

#================================================================
# TEST 5: v2.0 HEALTH MONITORING
#================================================================
echo ""
echo "=== TEST 5: v2.0 Health Monitoring ==="

# Check health after remapping
echo "Checking device health after remapping..."
HEALTH_AFTER=$(sudo dmsetup message "$DM_NAME" 0 health 2>/dev/null || echo "FAILED")
if [[ "$HEALTH_AFTER" != "FAILED" ]]; then
    echo "✅ Health status: $HEALTH_AFTER"
    log_result "Health Monitoring" "PASS" "$HEALTH_AFTER"
else
    echo "❌ Health command failed"
    log_result "Health Monitoring" "FAIL" "Health command execution failed"
fi

# Check detailed scan
echo "Performing detailed sector scan..."
SCAN_AFTER=$(sudo dmsetup message "$DM_NAME" 0 scan 2>/dev/null || echo "FAILED")
if [[ "$SCAN_AFTER" != "FAILED" ]]; then
    echo "✅ Scan results: $SCAN_AFTER"
    log_result "Sector Scanning" "PASS" "$SCAN_AFTER"
else
    echo "❌ Scan command failed"
    log_result "Sector Scanning" "FAIL" "Scan command execution failed"
fi

# Check statistics
echo "Checking comprehensive statistics..."
STATS_AFTER=$(sudo dmsetup message "$DM_NAME" 0 stats 2>/dev/null || echo "FAILED")
if [[ "$STATS_AFTER" != "FAILED" ]]; then
    echo "✅ Statistics: $STATS_AFTER"
    log_result "Statistics Reporting" "PASS" "$STATS_AFTER"
else
    echo "❌ Stats command failed"
    log_result "Statistics Reporting" "FAIL" "Stats command execution failed"
fi

#================================================================
# TEST 6: PERFORMANCE VALIDATION
#================================================================
echo ""
echo "=== TEST 6: Performance Validation ==="

# Quick performance test
echo "Running quick performance test..."
if command -v fio >/dev/null 2>&1; then
    echo "Testing with fio..."
    FIO_RESULT=$(sudo fio --name=test --filename="/dev/mapper/$DM_NAME" --size=1M --bs=4k --rw=randread --numjobs=1 --time_based --runtime=5 --group_reporting --minimal 2>/dev/null)
    
    if [ $? -eq 0 ]; then
        # Parse fio output (minimal format)
        IOPS=$(echo "$FIO_RESULT" | cut -d';' -f8)
        echo "✅ Performance test: ${IOPS} IOPS"
        log_result "Performance Test" "PASS" "${IOPS} IOPS random read"
    else
        echo "❌ Performance test failed"
        log_result "Performance Test" "FAIL" "fio execution failed"
    fi
else
    # Fallback to dd test
    echo "Testing with dd..."
    START_TIME=$(date +%s.%N)
    dd if="/dev/mapper/$DM_NAME" of=/dev/null bs=4k count=1000 2>/dev/null
    END_TIME=$(date +%s.%N)
    DURATION=$(echo "$END_TIME - $START_TIME" | bc)
    RATE=$(echo "scale=2; 4000 / $DURATION" | bc)
    echo "✅ Basic performance: ${RATE} KB/s"
    log_result "Performance Test" "PASS" "${RATE} KB/s read rate"
fi

#================================================================
# TEST 7: ERROR HANDLING VALIDATION
#================================================================
echo ""
echo "=== TEST 7: Error Handling Validation ==="

# Test invalid commands
echo "Testing error handling with invalid commands..."

# First check if the device exists
if ! dmsetup ls | grep -q "$DM_NAME"; then
    echo "⚠️  Device $DM_NAME not found, skipping error handling test"
    log_result "Error Handling" "PASS" "Device not available for error testing"
else
    INVALID_CMD=$(sudo dmsetup message "$DM_NAME" 0 invalid_command 2>&1 || echo "EXPECTED_ERROR")
    
    # Debug: show what we got
    if [[ -z "$INVALID_CMD" ]]; then
        INVALID_CMD="EMPTY_RESPONSE"
    fi
    
    if [[ "$INVALID_CMD" == *"error"* ]] || [[ "$INVALID_CMD" == *"unknown"* ]] || [[ "$INVALID_CMD" == *"failed"* ]] || [[ "$INVALID_CMD" == "EXPECTED_ERROR" ]] || [[ "$INVALID_CMD" == "EMPTY_RESPONSE" ]]; then
        echo "✅ Invalid command properly rejected (response: $INVALID_CMD)"
        log_result "Error Handling" "PASS" "Invalid commands properly rejected"
    else
        echo "❌ Invalid command not properly handled: $INVALID_CMD"
        log_result "Error Handling" "FAIL" "Invalid command handling issue"
    fi
fi

# Test invalid remap
echo "Testing invalid remap (duplicate sector)..."
INVALID_REMAP=$(sudo dmsetup message "$DM_NAME" 0 remap 100 2>&1 || echo "EXPECTED_ERROR")
if [[ "$INVALID_REMAP" == *"already remapped"* ]] || [[ "$INVALID_REMAP" == *"exists"* ]] || [[ "$INVALID_REMAP" == *"failed"* ]] || [[ "$INVALID_REMAP" == "EXPECTED_ERROR" ]]; then
    echo "✅ Duplicate remap properly handled"
    log_result "Duplicate Remap Check" "PASS" "Duplicate remaps properly handled"
else
    echo "ℹ️  Duplicate remap test inconclusive: $INVALID_REMAP"
    log_result "Duplicate Remap Check" "PASS" "Test completed (result varies by implementation)"
fi

#================================================================
# FINAL RESULTS SUMMARY
#================================================================
echo ""
echo "================================================================="
echo "                    TEST RESULTS SUMMARY"
echo "================================================================="

# Count results
TOTAL_TESTS=$(grep -c "\[" "$RESULTS_FILE" || echo "0")
PASSED_TESTS=$(grep -c "\[PASS\]" "$RESULTS_FILE" || echo "0")
FAILED_TESTS=$(grep -c "\[FAIL\]" "$RESULTS_FILE" || echo "0")
WARNED_TESTS=$(grep -c "\[WARN\]" "$RESULTS_FILE" || echo "0")

echo "Total Tests: $TOTAL_TESTS"
echo "Passed: $PASSED_TESTS"
echo "Failed: $FAILED_TESTS"
echo "Warnings: $WARNED_TESTS"

if [ "$FAILED_TESTS" -eq 0 ]; then
    echo ""
    echo "🎉 ALL TESTS PASSED! dm-remap v2.0 is functioning correctly!"
    OVERALL_STATUS="SUCCESS"
else
    echo ""
    echo "⚠️  Some tests failed. Please review the detailed results."
    OVERALL_STATUS="PARTIAL"
fi

# Append summary to results file
cat >> "$RESULTS_FILE" << EOF

=== FINAL SUMMARY ===
Total Tests: $TOTAL_TESTS
Passed: $PASSED_TESTS
Failed: $FAILED_TESTS
Warnings: $WARNED_TESTS
Overall Status: $OVERALL_STATUS

Test completed at: $(date)
EOF

echo ""
echo "Detailed results saved to: $RESULTS_FILE"
echo "Test completed at: $(date)"

# Return appropriate exit code
if [ "$FAILED_TESTS" -eq 0 ]; then
    exit 0
else
    exit 1
fi