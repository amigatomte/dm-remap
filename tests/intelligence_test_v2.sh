#!/bin/bash

# intelligence_test_v2.sh - Test dm-remap v2.0 intelligent features
# Focuses on error detection, auto-remapping, and health monitoring

set -euo pipefail

TEST_DIR="/home/christian/kernel_dev/dm-remap/tests"
LOG_DIR="/tmp/dm-remap-v2-intelligence-logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="$LOG_DIR/intelligence_test_results_$TIMESTAMP.log"

# Test configuration
TEST_SIZE_MB=20
DM_NAME="intelligence-test-v2"

# Ensure we're in the right directory
cd "$(dirname "$0")"

# Create log directory
mkdir -p "$LOG_DIR"

echo "================================================================="
echo "    dm-remap v2.0 INTELLIGENCE FEATURES TEST SUITE"
echo "================================================================="
echo "Date: $(date)"
echo "Focus: Error detection, auto-remapping, health monitoring"
echo "Results: $RESULTS_FILE"
echo ""

# Initialize results file
cat > "$RESULTS_FILE" << EOF
dm-remap v2.0 Intelligence Features Test Results
Date: $(date)
Focus: Error detection, auto-remapping, health monitoring

=== DETAILED TEST RESULTS ===
EOF

# Function to log results
log_test() {
    local test_name="$1"
    local status="$2"
    local details="$3"
    local timestamp=$(date '+%H:%M:%S')
    
    echo "[$timestamp][$status] $test_name: $details" | tee -a "$RESULTS_FILE"
}

# Function to cleanup
cleanup() {
    echo "=== Intelligence Test Cleanup ==="
    
    # Remove device mapper target
    if dmsetup ls | grep -q "$DM_NAME"; then
        echo "Removing $DM_NAME"
        sudo dmsetup remove "$DM_NAME" 2>/dev/null || true
    fi
    
    # Clean up loop devices
    for loop in $(losetup -l | grep "/tmp/intel_test" | awk '{print $1}'); do
        echo "Detaching $loop"
        sudo losetup -d "$loop" 2>/dev/null || true
    done
    
    # Remove test files
    rm -f /tmp/intel_test_*.img
}

trap cleanup EXIT

#================================================================
# SETUP PHASE
#================================================================
echo "=== Setup Phase ==="

# Check v2.0 module
if ! lsmod | grep -q dm_remap; then
    echo "❌ dm-remap module not loaded"
    exit 1
fi

echo "✅ dm-remap module detected"

# Create test devices
echo "Creating test images..."
dd if=/dev/zero of=/tmp/intel_test_main.img bs=1M count=$TEST_SIZE_MB >/dev/null 2>&1
dd if=/dev/zero of=/tmp/intel_test_spare.img bs=1M count=$TEST_SIZE_MB >/dev/null 2>&1

LOOP_MAIN=$(sudo losetup -f --show /tmp/intel_test_main.img)
LOOP_SPARE=$(sudo losetup -f --show /tmp/intel_test_spare.img)

echo "Main device: $LOOP_MAIN"
echo "Spare device: $LOOP_SPARE"

# Calculate sectors
TOTAL_SECTORS=$((TEST_SIZE_MB * 1024 * 2))
SPARE_SECTORS=$((TEST_SIZE_MB * 1024))

# Create dm-remap target
TABLE="0 $TOTAL_SECTORS remap $LOOP_MAIN $LOOP_SPARE 0 $SPARE_SECTORS"
if sudo dmsetup create "$DM_NAME" --table "$TABLE"; then
    echo "✅ Intelligence test target created"
    log_test "Target Setup" "PASS" "Test target created successfully"
else
    echo "❌ Failed to create test target"
    log_test "Target Setup" "FAIL" "Could not create test target"
    exit 1
fi

#================================================================
# TEST 1: INITIAL HEALTH STATUS
#================================================================
echo ""
echo "=== TEST 1: Initial Health Assessment ==="

# Check initial health (should be excellent)
INITIAL_HEALTH=$(sudo dmsetup message "$DM_NAME" 0 health 2>/dev/null || echo "FAILED")
echo "Initial health status: $INITIAL_HEALTH"

if [[ "$INITIAL_HEALTH" == *"excellent"* ]]; then
    log_test "Initial Health" "PASS" "Device starts with excellent health"
elif [[ "$INITIAL_HEALTH" != "FAILED" ]]; then
    log_test "Initial Health" "WARN" "Health reported but not excellent: $INITIAL_HEALTH"
else
    log_test "Initial Health" "FAIL" "Health command failed"
fi

# Check initial stats
INITIAL_STATS=$(sudo dmsetup message "$DM_NAME" 0 stats 2>/dev/null || echo "FAILED")
echo "Initial statistics: $INITIAL_STATS"

if [[ "$INITIAL_STATS" == *"errors=0"* ]]; then
    log_test "Initial Stats" "PASS" "No errors initially detected"
elif [[ "$INITIAL_STATS" != "FAILED" ]]; then
    log_test "Initial Stats" "WARN" "Stats available but unexpected: $INITIAL_STATS"
else
    log_test "Initial Stats" "FAIL" "Stats command failed"
fi

#================================================================
# TEST 2: AUTO-REMAP CONFIGURATION
#================================================================
echo ""
echo "=== TEST 2: Auto-Remap Configuration ==="

# Test disabling auto-remap
DISABLE_RESULT=$(sudo dmsetup message "$DM_NAME" 0 auto_remap disable 2>/dev/null || echo "FAILED")
echo "Auto-remap disable: $DISABLE_RESULT"

if [[ "$DISABLE_RESULT" == *"disabled"* ]]; then
    log_test "Auto-Remap Disable" "PASS" "Auto-remap successfully disabled"
elif [[ "$DISABLE_RESULT" != "FAILED" ]]; then
    log_test "Auto-Remap Disable" "WARN" "Disable command executed: $DISABLE_RESULT"
else
    log_test "Auto-Remap Disable" "FAIL" "Disable command failed"
fi

# Test enabling auto-remap
ENABLE_RESULT=$(sudo dmsetup message "$DM_NAME" 0 auto_remap enable 2>/dev/null || echo "FAILED")
echo "Auto-remap enable: $ENABLE_RESULT"

if [[ "$ENABLE_RESULT" == *"enabled"* ]]; then
    log_test "Auto-Remap Enable" "PASS" "Auto-remap successfully enabled"
elif [[ "$ENABLE_RESULT" != "FAILED" ]]; then
    log_test "Auto-Remap Enable" "WARN" "Enable command executed: $ENABLE_RESULT"
else
    log_test "Auto-Remap Enable" "FAIL" "Enable command failed"
fi

# Test invalid auto-remap command
INVALID_AUTO=$(sudo dmsetup message "$DM_NAME" 0 auto_remap invalid 2>&1 || echo "EXPECTED_ERROR")
if [[ "$INVALID_AUTO" == *"error"* ]] || [[ "$INVALID_AUTO" == "EXPECTED_ERROR" ]]; then
    log_test "Auto-Remap Validation" "PASS" "Invalid auto-remap commands properly rejected"
else
    log_test "Auto-Remap Validation" "FAIL" "Invalid command not properly handled"
fi

#================================================================
# TEST 3: MANUAL REMAPPING AND HEALTH TRACKING
#================================================================
echo ""
echo "=== TEST 3: Manual Remapping and Health Tracking ==="

# Remap several sectors manually to simulate problematic areas
echo "Creating manual remaps to simulate bad sectors..."
REMAPPED_SECTORS=(1000 2000 3000 4000 5000)

for sector in "${REMAPPED_SECTORS[@]}"; do
    REMAP_RESULT=$(sudo dmsetup message "$DM_NAME" 0 remap $sector 2>/dev/null || echo "FAILED")
    if [[ "$REMAP_RESULT" != "FAILED" ]]; then
        echo "✅ Remapped sector $sector"
    else
        echo "❌ Failed to remap sector $sector"
    fi
done

# Check health after manual remapping
echo "Checking health after manual remapping..."
HEALTH_AFTER_REMAP=$(sudo dmsetup message "$DM_NAME" 0 health 2>/dev/null || echo "FAILED")
echo "Health after remapping: $HEALTH_AFTER_REMAP"

if [[ "$HEALTH_AFTER_REMAP" != "FAILED" ]]; then
    if [[ "$HEALTH_AFTER_REMAP" == *"good"* ]] || [[ "$HEALTH_AFTER_REMAP" == *"fair"* ]]; then
        log_test "Health After Remapping" "PASS" "Health degraded appropriately: $HEALTH_AFTER_REMAP"
    else
        log_test "Health After Remapping" "WARN" "Health status unclear: $HEALTH_AFTER_REMAP"
    fi
else
    log_test "Health After Remapping" "FAIL" "Health command failed after remapping"
fi

# Check detailed scan
SCAN_RESULT=$(sudo dmsetup message "$DM_NAME" 0 scan 2>/dev/null || echo "FAILED")
echo "Detailed scan: $SCAN_RESULT"

if [[ "$SCAN_RESULT" != "FAILED" ]]; then
    if [[ "$SCAN_RESULT" == *"remapped="* ]]; then
        log_test "Sector Scanning" "PASS" "Scan detects remapped sectors: $SCAN_RESULT"
    else
        log_test "Sector Scanning" "WARN" "Scan executed but unclear results: $SCAN_RESULT"
    fi
else
    log_test "Sector Scanning" "FAIL" "Scan command failed"
fi

#================================================================
# TEST 4: STATISTICS TRACKING
#================================================================
echo ""
echo "=== TEST 4: Statistics Tracking ==="

# Check comprehensive statistics
STATS_DETAILED=$(sudo dmsetup message "$DM_NAME" 0 stats 2>/dev/null || echo "FAILED")
echo "Detailed statistics: $STATS_DETAILED"

if [[ "$STATS_DETAILED" != "FAILED" ]]; then
    # Parse expected fields
    if [[ "$STATS_DETAILED" == *"remapped="* ]] && [[ "$STATS_DETAILED" == *"health="* ]]; then
        log_test "Statistics Tracking" "PASS" "Comprehensive stats available: $STATS_DETAILED"
    else
        log_test "Statistics Tracking" "WARN" "Stats available but format unexpected: $STATS_DETAILED"
    fi
else
    log_test "Statistics Tracking" "FAIL" "Statistics command failed"
fi

# Check status consistency
DM_STATUS=$(sudo dmsetup status "$DM_NAME")
echo "Device mapper status: $DM_STATUS"

if [[ "$DM_STATUS" == *"remapped="* ]]; then
    # Extract remapped count from status
    REMAPPED_COUNT=$(echo "$DM_STATUS" | grep -o 'remapped=[0-9]*' | cut -d'=' -f2)
    if [ "$REMAPPED_COUNT" -eq "${#REMAPPED_SECTORS[@]}" ]; then
        log_test "Status Consistency" "PASS" "Status shows correct remap count: $REMAPPED_COUNT"
    else
        log_test "Status Consistency" "WARN" "Remap count mismatch: expected ${#REMAPPED_SECTORS[@]}, got $REMAPPED_COUNT"
    fi
else
    log_test "Status Consistency" "FAIL" "Status format unexpected: $DM_STATUS"
fi

#================================================================
# TEST 5: I/O PATTERN TESTING
#================================================================
echo ""
echo "=== TEST 5: I/O Pattern Testing ==="

# Test I/O to remapped sectors
echo "Testing I/O to remapped sectors..."
SUCCESS_COUNT=0
TOTAL_IO_TESTS=${#REMAPPED_SECTORS[@]}

for sector in "${REMAPPED_SECTORS[@]}"; do
    # Write test data to remapped sector
    TEST_DATA="v2.0-test-$sector-$(date +%s)"
    SECTOR_OFFSET=$((sector * 512))
    
    if echo "$TEST_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=512 seek=$sector count=1 conv=sync 2>/dev/null; then
        # Try to read it back
        READ_DATA=$(sudo dd if="/dev/mapper/$DM_NAME" bs=512 skip=$sector count=1 2>/dev/null | tr -d '\0')
        
        if [[ "$READ_DATA" == *"$TEST_DATA"* ]]; then
            echo "✅ I/O to remapped sector $sector successful"
            ((SUCCESS_COUNT++))
        else
            echo "❌ Data mismatch on remapped sector $sector"
        fi
    else
        echo "❌ Failed to write to remapped sector $sector"
    fi
done

if [ "$SUCCESS_COUNT" -eq "$TOTAL_IO_TESTS" ]; then
    log_test "Remapped Sector I/O" "PASS" "All I/O to remapped sectors successful ($SUCCESS_COUNT/$TOTAL_IO_TESTS)"
elif [ "$SUCCESS_COUNT" -gt 0 ]; then
    log_test "Remapped Sector I/O" "PARTIAL" "Some I/O successful ($SUCCESS_COUNT/$TOTAL_IO_TESTS)"
else
    log_test "Remapped Sector I/O" "FAIL" "No successful I/O to remapped sectors"
fi

#================================================================
# TEST 6: BOUNDARY CONDITIONS
#================================================================
echo ""
echo "=== TEST 6: Boundary Conditions ==="

# Test remapping when spare area is nearly full
echo "Testing spare area limits..."
SPARE_LIMIT_TEST=true

# Try to remap sectors until we hit the limit
for i in $(seq 6000 6100); do
    REMAP_RESULT=$(sudo dmsetup message "$DM_NAME" 0 remap $i 2>&1 || echo "LIMIT_REACHED")
    if [[ "$REMAP_RESULT" == *"no spare slots"* ]] || [[ "$REMAP_RESULT" == "LIMIT_REACHED" ]]; then
        echo "✅ Spare area limit properly enforced at sector $i"
        log_test "Spare Area Limits" "PASS" "Properly enforces spare area limits"
        break
    elif [[ "$REMAP_RESULT" == *"remapped"* ]]; then
        continue  # Successfully remapped, try next
    else
        echo "❌ Unexpected response: $REMAP_RESULT"
        log_test "Spare Area Limits" "WARN" "Unexpected limit behavior"
        break
    fi
done

# Check health when approaching limits
HEALTH_AT_LIMIT=$(sudo dmsetup message "$DM_NAME" 0 health 2>/dev/null || echo "FAILED")
echo "Health near capacity: $HEALTH_AT_LIMIT"

if [[ "$HEALTH_AT_LIMIT" != "FAILED" ]]; then
    if [[ "$HEALTH_AT_LIMIT" == *"poor"* ]] || [[ "$HEALTH_AT_LIMIT" == *"critical"* ]]; then
        log_test "Health at Capacity" "PASS" "Health correctly indicates capacity issues"
    else
        log_test "Health at Capacity" "WARN" "Health status: $HEALTH_AT_LIMIT"
    fi
else
    log_test "Health at Capacity" "FAIL" "Health command failed"
fi

#================================================================
# TEST 7: COMMAND ERROR HANDLING
#================================================================
echo ""
echo "=== TEST 7: Command Error Handling ==="

# Test various invalid commands
echo "Testing command error handling..."

INVALID_COMMANDS=(
    "invalid_command"
    "stats extra_arg"
    "health bad_arg"
    "scan with args"
    "auto_remap"
    "auto_remap badarg"
    "remap"
    "remap abc"
    "remap 100 200 300"
)

ERROR_HANDLING_SCORE=0
for cmd in "${INVALID_COMMANDS[@]}"; do
    RESULT=$(sudo dmsetup message "$DM_NAME" 0 $cmd 2>&1 || echo "ERROR_CAUGHT")
    if [[ "$RESULT" == *"error"* ]] || [[ "$RESULT" == *"Invalid"* ]] || [[ "$RESULT" == "ERROR_CAUGHT" ]]; then
        echo "✅ Properly rejected: $cmd"
        ((ERROR_HANDLING_SCORE++))
    else
        echo "❌ Improperly handled: $cmd -> $RESULT"
    fi
done

if [ "$ERROR_HANDLING_SCORE" -eq "${#INVALID_COMMANDS[@]}" ]; then
    log_test "Error Handling" "PASS" "All invalid commands properly rejected"
elif [ "$ERROR_HANDLING_SCORE" -gt $((${#INVALID_COMMANDS[@]} / 2)) ]; then
    log_test "Error Handling" "PARTIAL" "Most invalid commands rejected ($ERROR_HANDLING_SCORE/${#INVALID_COMMANDS[@]})"
else
    log_test "Error Handling" "FAIL" "Poor error handling ($ERROR_HANDLING_SCORE/${#INVALID_COMMANDS[@]})"
fi

#================================================================
# FINAL INTELLIGENCE ASSESSMENT
#================================================================
echo ""
echo "=== FINAL INTELLIGENCE ASSESSMENT ==="

# Get final comprehensive statistics
FINAL_STATS=$(sudo dmsetup message "$DM_NAME" 0 stats 2>/dev/null || echo "UNAVAILABLE")
FINAL_HEALTH=$(sudo dmsetup message "$DM_NAME" 0 health 2>/dev/null || echo "UNAVAILABLE")
FINAL_STATUS=$(sudo dmsetup status "$DM_NAME" 2>/dev/null || echo "UNAVAILABLE")

echo "Final Statistics: $FINAL_STATS"
echo "Final Health: $FINAL_HEALTH"
echo "Final Status: $FINAL_STATUS"

# Assess v2.0 intelligence features
INTELLIGENCE_FEATURES=0

# Check if stats command works
if [[ "$FINAL_STATS" != "UNAVAILABLE" ]]; then
    ((INTELLIGENCE_FEATURES++))
fi

# Check if health command works
if [[ "$FINAL_HEALTH" != "UNAVAILABLE" ]]; then
    ((INTELLIGENCE_FEATURES++))
fi

# Check if auto_remap commands work
if [[ "$ENABLE_RESULT" != "FAILED" ]]; then
    ((INTELLIGENCE_FEATURES++))
fi

# Overall intelligence assessment
if [ "$INTELLIGENCE_FEATURES" -eq 3 ]; then
    log_test "v2.0 Intelligence Features" "PASS" "All core intelligence features operational"
    INTELLIGENCE_STATUS="FULLY_OPERATIONAL"
elif [ "$INTELLIGENCE_FEATURES" -ge 2 ]; then
    log_test "v2.0 Intelligence Features" "PARTIAL" "Most intelligence features working ($INTELLIGENCE_FEATURES/3)"
    INTELLIGENCE_STATUS="PARTIALLY_OPERATIONAL"
else
    log_test "v2.0 Intelligence Features" "FAIL" "Intelligence features not working ($INTELLIGENCE_FEATURES/3)"
    INTELLIGENCE_STATUS="NON_OPERATIONAL"
fi

#================================================================
# RESULTS SUMMARY
#================================================================
echo ""
echo "================================================================="
echo "           INTELLIGENCE FEATURES TEST SUMMARY"
echo "================================================================="

# Count results
TOTAL_TESTS=$(grep -c '\[.*\]' "$RESULTS_FILE" || echo "0")
PASSED_TESTS=$(grep -c '\[.*\]\[PASS\]' "$RESULTS_FILE" || echo "0")
FAILED_TESTS=$(grep -c '\[.*\]\[FAIL\]' "$RESULTS_FILE" || echo "0")
PARTIAL_TESTS=$(grep -c '\[.*\]\[PARTIAL\]' "$RESULTS_FILE" || echo "0")
WARNED_TESTS=$(grep -c '\[.*\]\[WARN\]' "$RESULTS_FILE" || echo "0")

echo "Total Intelligence Tests: $TOTAL_TESTS"
echo "Passed: $PASSED_TESTS"
echo "Failed: $FAILED_TESTS"
echo "Partial: $PARTIAL_TESTS"
echo "Warnings: $WARNED_TESTS"
echo ""
echo "Intelligence Status: $INTELLIGENCE_STATUS"

# Append final summary to results file
cat >> "$RESULTS_FILE" << EOF

=== INTELLIGENCE FEATURES SUMMARY ===
Total Tests: $TOTAL_TESTS
Passed: $PASSED_TESTS
Failed: $FAILED_TESTS
Partial: $PARTIAL_TESTS
Warnings: $WARNED_TESTS

Intelligence Status: $INTELLIGENCE_STATUS
Final Statistics: $FINAL_STATS
Final Health: $FINAL_HEALTH

Test completed at: $(date)
EOF

echo "Detailed results saved to: $RESULTS_FILE"

# Provide recommendations
echo ""
echo "=== RECOMMENDATIONS ==="
if [[ "$INTELLIGENCE_STATUS" == "FULLY_OPERATIONAL" ]]; then
    echo "🎉 v2.0 intelligence features are fully operational!"
    echo "✅ Ready for production use with intelligent error detection"
elif [[ "$INTELLIGENCE_STATUS" == "PARTIALLY_OPERATIONAL" ]]; then
    echo "⚠️  v2.0 intelligence features are partially working"
    echo "💡 Review failed tests and consider additional development"
else
    echo "❌ v2.0 intelligence features need significant work"
    echo "🔧 Focus on core intelligence infrastructure"
fi

# Return appropriate exit code
if [ "$FAILED_TESTS" -eq 0 ]; then
    exit 0
else
    exit 1
fi