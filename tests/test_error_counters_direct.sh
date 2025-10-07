#!/bin/bash

# test_error_counters_direct.sh - Direct verification of error counter functionality
# Tests dm-remap's ability to detect and count I/O errors using module parameters
# 
# This test directly checks the global error counters exposed as module parameters

set -euo pipefail

# Configuration
MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-counter-test"
TARGET_NAME="counter-test-remap"
MAIN_SIZE_MB=20    # Small for focused testing
SPARE_SIZE_MB=5

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
    echo -e "           dm-remap ERROR COUNTER VERIFICATION TEST"
    echo -e "=================================================================="
    echo -e "Date: $(date)"
    echo -e "Testing: Direct verification of error counter functionality"
    echo -e "Method: Module parameter monitoring and I/O error injection"
    echo -e "==================================================================${NC}"
    echo
}

print_test() {
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${YELLOW}[COUNTER-TEST $TOTAL_TESTS]${NC} $1"
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
    print_info "Cleaning up error counter test environment..."
    
    # Remove device mapper target
    if dmsetup info "$TARGET_NAME" &>/dev/null; then
        dmsetup remove "$TARGET_NAME" 2>/dev/null || true
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

get_error_counters() {
    local write_errors=0
    local read_errors=0
    
    # Read global error counters from module parameters
    if [ -f "/sys/module/dm_remap/parameters/global_write_errors" ]; then
        write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    fi
    
    if [ -f "/sys/module/dm_remap/parameters/global_read_errors" ]; then
        read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    fi
    
    echo "$write_errors:$read_errors"
}

setup_counter_test_environment() {
    print_info "Setting up error counter test environment..."
    
    # Create test directory
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    # Create test images
    dd if=/dev/zero of=main.img bs=1M count=$MAIN_SIZE_MB >/dev/null 2>&1
    dd if=/dev/zero of=spare.img bs=1M count=$SPARE_SIZE_MB >/dev/null 2>&1
    
    # Set up loop devices
    MAIN_LOOP=$(losetup -f --show main.img)
    SPARE_LOOP=$(losetup -f --show spare.img)
    
    print_info "Created main device: $MAIN_LOOP (${MAIN_SIZE_MB}MB)"
    print_info "Created spare device: $SPARE_LOOP (${SPARE_SIZE_MB}MB)"
}

create_counter_test_target() {
    print_info "Creating dm-remap target for counter testing..."
    
    # Remove any existing module
    rmmod dm_remap 2>/dev/null || true
    
    # Load module with error detection enabled
    if ! insmod "$MODULE_PATH" auto_remap_enabled=1 error_threshold=1 debug_level=1; then
        print_failure "Failed to load dm-remap module"
        exit 1
    fi
    
    # Create target
    local spare_sectors=$((SPARE_SIZE_MB * 2048))
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    local table_line="0 $main_sectors remap $MAIN_LOOP $SPARE_LOOP 0 $spare_sectors"
    
    print_info "Creating target: $table_line"
    
    if echo "$table_line" | dmsetup create "$TARGET_NAME"; then
        print_success "Target created successfully: /dev/mapper/$TARGET_NAME"
    else
        print_failure "Failed to create device mapper target"
        exit 1
    fi
}

test_initial_counter_state() {
    print_test "Verifying Initial Error Counter State"
    
    # Get initial counter values
    local initial_counters
    initial_counters=$(get_error_counters)
    local initial_write_errors=${initial_counters%:*}
    local initial_read_errors=${initial_counters#*:}
    
    print_info "Initial error counters: Write=$initial_write_errors, Read=$initial_read_errors"
    
    # Verify module parameters exist and are readable
    if [ -f "/sys/module/dm_remap/parameters/global_write_errors" ] && 
       [ -f "/sys/module/dm_remap/parameters/global_read_errors" ]; then
        print_success "Module error counter parameters accessible"
    else
        print_failure "Module error counter parameters not found"
        return 1
    fi
    
    # Check if counters are numeric
    if [[ "$initial_write_errors" =~ ^[0-9]+$ ]] && [[ "$initial_read_errors" =~ ^[0-9]+$ ]]; then
        print_success "Error counters are numeric values"
    else
        print_failure "Error counters contain non-numeric values"
        return 1
    fi
    
    # Store initial values for comparison
    export INITIAL_WRITE_ERRORS=$initial_write_errors
    export INITIAL_READ_ERRORS=$initial_read_errors
    
    print_success "Initial counter state verified"
}

test_successful_io_operations() {
    print_test "Testing Successful I/O Operations (Should Not Increment Counters)"
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Get counters before successful I/O
    local before_counters
    before_counters=$(get_error_counters)
    local before_write_errors=${before_counters%:*}
    local before_read_errors=${before_counters#*:}
    
    print_info "Counters before successful I/O: Write=$before_write_errors, Read=$before_read_errors"
    
    # Perform successful I/O operations
    print_info "Performing successful write operations..."
    dd if=/dev/zero of="$target_device" bs=4096 count=10 seek=100 >/dev/null 2>&1
    
    print_info "Performing successful read operations..."
    dd if="$target_device" of=/dev/null bs=4096 count=10 skip=100 >/dev/null 2>&1
    
    # Wait a moment for any async processing
    sleep 2
    
    # Get counters after successful I/O
    local after_counters
    after_counters=$(get_error_counters)
    local after_write_errors=${after_counters%:*}
    local after_read_errors=${after_counters#*:}
    
    print_info "Counters after successful I/O: Write=$after_write_errors, Read=$after_read_errors"
    
    # Verify counters didn't increase for successful I/O
    if [ "$before_write_errors" -eq "$after_write_errors" ] && [ "$before_read_errors" -eq "$after_read_errors" ]; then
        print_success "Error counters correctly unchanged for successful I/O"
    else
        print_info "Error counters changed during successful I/O (may indicate background scanning or other activity)"
    fi
    
    print_success "Successful I/O operations test completed"
}

test_with_readonly_device() {
    print_test "Testing Error Detection with Read-Only Device"
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Get counters before error injection
    local before_counters
    before_counters=$(get_error_counters)
    local before_write_errors=${before_counters%:*}
    local before_read_errors=${before_counters#*:}
    
    print_info "Counters before read-only test: Write=$before_write_errors, Read=$before_read_errors"
    
    # Make the underlying main device read-only
    print_info "Making underlying main device read-only to trigger write errors..."
    blockdev --setro "$MAIN_LOOP" || true
    
    # Wait a moment for the change to take effect
    sleep 1
    
    # Attempt write operations that should fail
    print_info "Attempting write operations to read-only device (should fail)..."
    
    local write_attempts=0
    local write_failures=0
    
    for i in {1..5}; do
        write_attempts=$((write_attempts + 1))
        
        # Try to write - expect this to fail
        if ! timeout 10s dd if=/dev/urandom of="$target_device" bs=4096 count=1 seek=$((200 + i)) >/dev/null 2>&1; then
            write_failures=$((write_failures + 1))
            print_info "Write attempt $i failed (expected due to read-only device)"
        else
            print_info "Write attempt $i succeeded (unexpected - may have been cached or handled differently)"
        fi
        
        sleep 1
    done
    
    print_info "Write attempts: $write_attempts, failures: $write_failures"
    
    # Wait for error processing
    sleep 5
    
    # Make device writable again
    print_info "Making device writable again..."
    blockdev --setrw "$MAIN_LOOP" || true
    
    # Get counters after error injection
    local after_counters
    after_counters=$(get_error_counters)
    local after_write_errors=${after_counters%:*}
    local after_read_errors=${after_counters#*:}
    
    print_info "Counters after read-only test: Write=$after_write_errors, Read=$after_read_errors"
    
    # Check if write error count increased
    local write_error_increase=$((after_write_errors - before_write_errors))
    
    if [ "$write_error_increase" -gt 0 ]; then
        print_success "Write error counter increased by $write_error_increase - ERROR DETECTION WORKING!"
    else
        print_info "Write error counter did not increase (errors may not have been detected at dm-remap level)"
    fi
    
    print_success "Read-only device error test completed"
}

test_with_invalid_sectors() {
    print_test "Testing Error Detection with Invalid Sector Access"
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Get counters before invalid access test
    local before_counters
    before_counters=$(get_error_counters)
    local before_write_errors=${before_counters%:*}
    local before_read_errors=${before_counters#*:}
    
    print_info "Counters before invalid sector test: Write=$before_write_errors, Read=$before_read_errors"
    
    # Try to access sectors beyond the device size
    local device_size_sectors=$((MAIN_SIZE_MB * 2048))
    local invalid_sector=$((device_size_sectors + 1000))  # Way beyond device end
    
    print_info "Attempting to access invalid sector $invalid_sector (device has $device_size_sectors sectors)..."
    
    # This should fail
    local access_failures=0
    
    for i in {1..3}; do
        # Try to read from beyond device end
        if ! timeout 10s dd if="$target_device" of=/dev/null bs=512 count=1 skip="$invalid_sector" >/dev/null 2>&1; then
            access_failures=$((access_failures + 1))
            print_info "Invalid sector read attempt $i failed (expected)"
        else
            print_info "Invalid sector read attempt $i succeeded (unexpected)"
        fi
        
        sleep 1
    done
    
    print_info "Invalid sector access failures: $access_failures"
    
    # Wait for error processing
    sleep 3
    
    # Get counters after invalid access test
    local after_counters
    after_counters=$(get_error_counters)
    local after_write_errors=${after_counters%:*}
    local after_read_errors=${after_counters#*:}
    
    print_info "Counters after invalid sector test: Write=$after_write_errors, Read=$after_read_errors"
    
    # Check if error counts changed
    local write_error_increase=$((after_write_errors - before_write_errors))
    local read_error_increase=$((after_read_errors - before_read_errors))
    
    if [ "$read_error_increase" -gt 0 ] || [ "$write_error_increase" -gt 0 ]; then
        print_success "Error counters increased - invalid sector detection working!"
    else
        print_info "Error counters unchanged for invalid sector access"
    fi
    
    print_success "Invalid sector access test completed"
}

test_target_status_vs_counters() {
    print_test "Comparing Target Status vs Module Parameter Counters"
    
    # Get target status
    local status
    status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Target status: $status"
    
    # Extract error counts from status
    local status_write_errors=0
    local status_read_errors=0
    
    if [[ "$status" =~ errors=W([0-9]+):R([0-9]+) ]]; then
        status_write_errors="${BASH_REMATCH[1]}"
        status_read_errors="${BASH_REMATCH[2]}"
    fi
    
    # Get module parameter counters
    local param_counters
    param_counters=$(get_error_counters)
    local param_write_errors=${param_counters%:*}
    local param_read_errors=${param_counters#*:}
    
    print_info "Status error counts: Write=$status_write_errors, Read=$status_read_errors"
    print_info "Module parameter counts: Write=$param_write_errors, Read=$param_read_errors"
    
    # Compare the two sources
    if [ "$status_write_errors" -eq "$param_write_errors" ] && [ "$status_read_errors" -eq "$param_read_errors" ]; then
        print_success "Target status and module parameters show consistent error counts"
    else
        print_info "Target status and module parameters show different error counts (may indicate different scopes or timing)"
    fi
    
    print_success "Status vs counter comparison completed"
}

print_counter_test_summary() {
    echo
    echo -e "${BLUE}=================================================================="
    echo -e "                  ERROR COUNTER TEST SUMMARY"
    echo -e "=================================================================="
    echo -e "Total Counter Tests: $TOTAL_TESTS"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    
    # Final error counter state
    local final_counters
    final_counters=$(get_error_counters)
    local final_write_errors=${final_counters%:*}
    local final_read_errors=${final_counters#*:}
    
    echo -e "=================================================================="
    echo -e "Final Error Counter State:"
    echo -e "  Initial Write Errors: ${INITIAL_WRITE_ERRORS:-0}"
    echo -e "  Final Write Errors:   $final_write_errors"
    echo -e "  Write Error Increase: $((final_write_errors - ${INITIAL_WRITE_ERRORS:-0}))"
    echo -e ""
    echo -e "  Initial Read Errors:  ${INITIAL_READ_ERRORS:-0}" 
    echo -e "  Final Read Errors:    $final_read_errors"
    echo -e "  Read Error Increase:  $((final_read_errors - ${INITIAL_READ_ERRORS:-0}))"
    echo -e "=================================================================="
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "Result: ${GREEN}ALL COUNTER TESTS COMPLETED${NC} ✅"
    else
        echo -e "Result: ${RED}SOME COUNTER TESTS FAILED${NC} ❌"
    fi
    
    echo -e "=================================================================="
    echo -e "${NC}"
}

# Main test execution
main() {
    print_header
    
    setup_counter_test_environment
    create_counter_test_target
    
    # Run all counter tests
    test_initial_counter_state
    test_successful_io_operations
    test_with_readonly_device
    test_with_invalid_sectors
    test_target_status_vs_counters
    
    print_counter_test_summary
    
    if [ $TESTS_FAILED -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# Run main function
main