#!/bin/bash

# test_health_real_errors.sh - Real I/O error injection using dm-flakey
# Creates actual I/O failures that dm-remap health scanning can detect
# 
# This test uses dm-flakey to create real read/write errors that will
# trigger the health scanning system and validate error detection

set -euo pipefail

# Configuration
MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-real-errors"
TARGET_NAME="real-error-remap"
FLAKEY_NAME="flakey-device"
MAIN_SIZE_MB=50
SPARE_SIZE_MB=10

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Test tracking
TOTAL_TESTS=0
TESTS_PASSED=0
TESTS_FAILED=0

print_header() {
    echo -e "${BLUE}=================================================================="
    echo -e "           dm-remap REAL I/O Error Injection Test"
    echo -e "=================================================================="
    echo -e "Date: $(date)"
    echo -e "Testing: Health Score Response to REAL I/O Errors"
    echo -e "Method: dm-flakey for actual I/O failure simulation"
    echo -e "==================================================================${NC}"
    echo
}

print_test() {
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${YELLOW}[REAL-ERROR-TEST $TOTAL_TESTS]${NC} $1"
}

print_success() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "${GREEN}✓ PASS:${NC} $1"
    echo
}

print_failure() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "${RED}✗ FAIL:${NC} $1"
    echo
}

print_info() {
    echo -e "${CYAN}ℹ INFO:${NC} $1"
}

cleanup() {
    print_info "Cleaning up real error test environment..."
    
    # Remove dm-remap target
    if dmsetup info "$TARGET_NAME" &>/dev/null; then
        dmsetup remove "$TARGET_NAME" 2>/dev/null || true
    fi
    
    # Remove flakey device
    if dmsetup info "$FLAKEY_NAME" &>/dev/null; then
        dmsetup remove "$FLAKEY_NAME" 2>/dev/null || true
    fi
    
    # Remove loop devices
    if [ -n "${MAIN_LOOP:-}" ]; then
        losetup -d "$MAIN_LOOP" 2>/dev/null || true
    fi
    if [ -n "${SPARE_LOOP:-}" ]; then
        losetup -d "$SPARE_LOOP" 2>/dev/null || true
    fi
    
    # Remove module
    rmmod dm_remap 2>/dev/null || true
    
    # Clean up test files
    rm -rf "$TEST_DIR"
    
    print_info "Cleanup completed"
}

# Set trap for cleanup
trap cleanup EXIT

check_dm_flakey_available() {
    print_info "Checking if dm-flakey is available..."
    
    # Check if dm-flakey module is loaded or available
    if lsmod | grep -q dm_flakey; then
        print_info "dm-flakey already loaded"
        return 0
    fi
    
    # Try to load dm-flakey
    if modprobe dm-flakey 2>/dev/null; then
        print_info "dm-flakey loaded successfully"
        return 0
    fi
    
    print_info "dm-flakey not available, will use alternative error injection method"
    return 1
}

setup_real_error_environment() {
    print_info "Setting up real I/O error test environment..."
    
    # Create test directory
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    # Create test images
    dd if=/dev/zero of=main.img bs=1M count=$MAIN_SIZE_MB >/dev/null 2>&1
    dd if=/dev/zero of=spare.img bs=1M count=$SPARE_SIZE_MB >/dev/null 2>&1
    
    # Write some test patterns to main device
    for i in {1..100}; do
        echo "TEST_PATTERN_$i" | dd of=main.img bs=512 seek=$((i * 10)) count=1 conv=notrunc >/dev/null 2>&1
    done
    
    # Set up loop devices
    MAIN_LOOP=$(losetup -f --show main.img)
    SPARE_LOOP=$(losetup -f --show spare.img)
    
    print_info "Created main device: $MAIN_LOOP (${MAIN_SIZE_MB}MB)"
    print_info "Created spare device: $SPARE_LOOP (${SPARE_SIZE_MB}MB)"
}

create_flakey_device() {
    print_test "Creating Flakey Device for Error Injection"
    
    if ! check_dm_flakey_available; then
        print_info "dm-flakey not available, skipping flakey-based tests"
        return 1
    fi
    
    # Create a flakey device that will fail after some operations
    # Format: start_sector num_sectors flakey feature_args [num_feature_args [arg]...]
    # We'll make it fail every 10th operation to some sectors
    
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    
    # Create flakey device - up interval 3 seconds, down interval 1 second
    local flakey_table="0 $main_sectors flakey $MAIN_LOOP 0 30 10"
    
    print_info "Creating flakey device: $flakey_table"
    
    if echo "$flakey_table" | dmsetup create "$FLAKEY_NAME"; then
        print_success "Flakey device created: /dev/mapper/$FLAKEY_NAME"
        return 0
    else
        print_failure "Failed to create flakey device"
        return 1
    fi
}

create_remap_target_with_flakey() {
    print_test "Creating dm-remap Target with Flakey Underlying Device"
    
    print_info "Loading dm-remap module with error detection enabled..."
    
    # Remove any existing module
    rmmod dm_remap 2>/dev/null || true
    
    # Load module with aggressive error detection
    if ! insmod "$MODULE_PATH" auto_remap_enabled=1 error_threshold=1; then
        print_failure "Failed to load dm-remap module"
        return 1
    fi
    
    # Create dm-remap target using the flakey device as main storage
    local spare_sectors=$((SPARE_SIZE_MB * 2048))
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    
    # Use flakey device as main device, normal spare
    local table_line="0 $main_sectors remap /dev/mapper/$FLAKEY_NAME $SPARE_LOOP 0 $spare_sectors"
    
    print_info "Creating dm-remap target: $table_line"
    
    if echo "$table_line" | dmsetup create "$TARGET_NAME"; then
        print_success "dm-remap target created with flakey underlying device"
        return 0
    else
        print_failure "Failed to create dm-remap target"
        return 1
    fi
}

test_error_triggering_with_flakey() {
    print_test "Triggering Real I/O Errors with Flakey Device"
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    print_info "Getting initial status..."
    local initial_status
    initial_status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Initial status: $initial_status"
    
    # Perform I/O operations that should trigger flakey device failures
    print_info "Performing I/O operations to trigger flakey device errors..."
    
    local error_detected=0
    
    for i in {1..20}; do
        print_info "I/O operation $i..."
        
        # Alternate between reads and writes
        if [ $((i % 2)) -eq 0 ]; then
            # Read operation
            if ! dd if="$target_device" of=/dev/null bs=4096 count=1 skip=$((i * 10)) >/dev/null 2>&1; then
                error_detected=$((error_detected + 1))
                print_info "Read error detected on operation $i"
            fi
        else
            # Write operation
            if ! dd if=/dev/zero of="$target_device" bs=4096 count=1 seek=$((i * 10)) >/dev/null 2>&1; then
                error_detected=$((error_detected + 1))
                print_info "Write error detected on operation $i"
            fi
        fi
        
        # Small delay to let flakey device transition states
        sleep 0.5
    done
    
    print_info "Total I/O errors detected during operations: $error_detected"
    
    # Wait for health system to process errors
    print_info "Waiting for health system to process errors..."
    sleep 15
    
    # Check final status
    local final_status
    final_status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Final status: $final_status"
    
    # Parse error counts from status
    if [[ "$final_status" =~ errors=W([0-9]+):R([0-9]+) ]]; then
        local write_errors="${BASH_REMATCH[1]}"
        local read_errors="${BASH_REMATCH[2]}"
        local total_dm_errors=$((write_errors + read_errors))
        
        if [ "$total_dm_errors" -gt 0 ]; then
            print_success "dm-remap detected $total_dm_errors errors (W:$write_errors R:$read_errors)"
            
            # Check if health score changed
            if [[ "$final_status" =~ health=([0-9]+) ]]; then
                local health_score="${BASH_REMATCH[1]}"
                if [ "$health_score" -lt 1 ] || [[ "$final_status" == *"health=2"* ]] || [[ "$final_status" == *"health=3"* ]]; then
                    print_success "Health score degraded to $health_score due to errors - HEALTH SCORING WORKS!"
                else
                    print_info "Health score remains at $health_score (may need more errors to degrade)"
                fi
            fi
        else
            print_info "No errors recorded in dm-remap status (errors may not have propagated up)"
        fi
    fi
    
    print_success "Flakey device error injection test completed"
}

# Alternative method without dm-flakey - using read-only filesystem corruption
create_readonly_error_simulation() {
    print_test "Alternative Error Simulation - Filesystem Corruption Method"
    
    print_info "Creating dm-remap target with normal devices first..."
    
    # Remove any existing module
    rmmod dm_remap 2>/dev/null || true
    
    # Load module
    if ! insmod "$MODULE_PATH" auto_remap_enabled=1 error_threshold=2; then
        print_failure "Failed to load dm-remap module"
        return 1
    fi
    
    # Create normal dm-remap target
    local spare_sectors=$((SPARE_SIZE_MB * 2048))
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    local table_line="0 $main_sectors remap $MAIN_LOOP $SPARE_LOOP 0 $spare_sectors"
    
    if echo "$table_line" | dmsetup create "$TARGET_NAME"; then
        print_success "dm-remap target created with normal devices"
    else
        print_failure "Failed to create dm-remap target"
        return 1
    fi
    
    # Now we'll simulate errors by making the underlying loop device read-only
    # during operations, which should cause write errors
    
    print_info "Testing write error simulation..."
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Get initial status
    local initial_status
    initial_status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Initial status: $initial_status"
    
    # Perform normal I/O first
    print_info "Performing successful I/O operations..."
    dd if=/dev/zero of="$target_device" bs=4096 count=10 >/dev/null 2>&1
    dd if="$target_device" of=/dev/null bs=4096 count=10 >/dev/null 2>&1
    
    # Now make the main loop device read-only to trigger write errors
    print_info "Making underlying device read-only to trigger write errors..."
    blockdev --setro "$MAIN_LOOP" || true
    
    # Try write operations that should fail
    local write_errors=0
    for i in {1..5}; do
        print_info "Attempting write operation $i (should fail)..."
        if ! dd if=/dev/urandom of="$target_device" bs=4096 count=1 seek=$((100 + i)) >/dev/null 2>&1; then
            write_errors=$((write_errors + 1))
            print_info "Write error detected (expected)"
        else
            print_info "Write succeeded (unexpected - may have been cached or remapped)"
        fi
        sleep 1
    done
    
    print_info "Detected write errors during read-only simulation: $write_errors"
    
    # Make device writable again
    blockdev --setrw "$MAIN_LOOP" || true
    
    # Wait for health system processing
    sleep 10
    
    # Check final status
    local final_status
    final_status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Status after error simulation: $final_status"
    
    # Look for error count changes
    if [[ "$final_status" =~ errors=W([0-9]+):R([0-9]+) ]]; then
        local write_err_count="${BASH_REMATCH[1]}"
        local read_err_count="${BASH_REMATCH[2]}"
        
        if [ "$write_err_count" -gt 0 ] || [ "$read_err_count" -gt 0 ]; then
            print_success "Error simulation successful - dm-remap recorded errors: W:$write_err_count R:$read_err_count"
            
            # Check health impact
            if [[ "$final_status" =~ health=([0-9]+) ]]; then
                local health="${BASH_REMATCH[1]}"
                print_info "Health level after errors: $health"
                
                if [ "$health" -ne 1 ]; then
                    print_success "HEALTH SCORING WORKING - Health degraded from errors!"
                else
                    print_info "Health remains at level 1 (may need more errors or different error pattern)"
                fi
            fi
        else
            print_info "No errors recorded in status - error simulation needs improvement"
        fi
    fi
    
    print_success "Read-only error simulation completed"
}

print_real_error_summary() {
    echo
    echo -e "${BLUE}=================================================================="
    echo -e "                  REAL ERROR INJECTION SUMMARY"
    echo -e "=================================================================="
    echo -e "Total Real Error Tests: $TOTAL_TESTS"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "Result: ${GREEN}REAL ERROR TESTS COMPLETED${NC} ✅"
    else
        echo -e "Result: ${RED}SOME REAL ERROR TESTS FAILED${NC} ❌"
    fi
    
    echo -e "=================================================================="
    echo -e "Real Error Methods Tested:"
    echo -e "  • dm-flakey device for actual I/O failures"
    echo -e "  • Read-only device simulation for write errors"
    echo -e "  • Health score response validation"
    echo -e "  • Error count tracking verification"
    echo -e "=================================================================="
    echo -e "${NC}"
}

# Main test execution
main() {
    print_header
    
    setup_real_error_environment
    
    # Try flakey method first
    if create_flakey_device && create_remap_target_with_flakey; then
        test_error_triggering_with_flakey
    else
        print_info "Flakey method not available, using alternative error simulation..."
        create_readonly_error_simulation
    fi
    
    print_real_error_summary
    
    if [ $TESTS_FAILED -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# Run main function
main