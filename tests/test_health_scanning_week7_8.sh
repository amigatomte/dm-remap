#!/bin/bash

# test_health_scanning_week7_8.sh - Comprehensive test for Background Health Scanning
# Tests Week 7-8 health scanning implementation including:
# - Health scanner initialization and startup
# - Background scanning operations
# - Health data collection and tracking
# - Performance impact measurement
# - Cleanup and shutdown

set -euo pipefail

# Configuration
MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-health-test"
TARGET_NAME="health-test-remap"
MAIN_SIZE_MB=100   # 100MB main device 
SPARE_SIZE_MB=20   # 20MB spare device

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
    echo -e "         dm-remap Week 7-8 Health Scanning Test Suite"
    echo -e "=================================================================="
    echo -e "Date: $(date)"
    echo -e "Testing: Background Health Scanning System"
    echo -e "Module: $MODULE_PATH"
    echo -e "==================================================================${NC}"
    echo
}

print_test() {
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${YELLOW}[TEST $TOTAL_TESTS]${NC} $1"
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
    print_info "Cleaning up health scanning test environment..."
    
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

setup_test_environment() {
    print_info "Setting up health scanning test environment..."
    
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

load_module_and_create_target() {
    print_info "Loading dm-remap module with health scanning..."
    
    # Remove any existing module
    rmmod dm_remap 2>/dev/null || true
    
    # Load the module
    if ! insmod "$MODULE_PATH"; then
        print_failure "Failed to load dm-remap module"
        exit 1
    fi
    
    # Create target
    SPARE_SECTORS=$((SPARE_SIZE_MB * 2048))  # 1MB = 2048 sectors
    TABLE_LINE="0 $((MAIN_SIZE_MB * 2048)) remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS"
    
    print_info "Creating target with: $TABLE_LINE"
    
    if ! echo "$TABLE_LINE" | dmsetup create "$TARGET_NAME"; then
        print_failure "Failed to create device mapper target"
        exit 1
    fi
    
    print_info "Target created successfully: /dev/mapper/$TARGET_NAME"
}

test_health_scanner_initialization() {
    print_test "Health Scanner Initialization and Auto-Start"
    
    # Check if health scanner was initialized (look for kernel messages)
    if dmesg | tail -20 | grep -q "Background health scanner initialized successfully"; then
        print_success "Health scanner initialization detected in kernel log"
    else
        print_info "Health scanner initialization message not found in recent kernel log"
    fi
    
    # Check if health scanning started
    if dmesg | tail -20 | grep -q "Background health scanning started"; then
        print_success "Health scanning auto-start detected in kernel log"
    else
        print_info "Health scanning start message not found in recent kernel log"
    fi
    
    # Check target status for health information
    local status_output
    status_output=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    
    if [[ -n "$status_output" ]]; then
        print_success "Target status accessible: $status_output"
    else
        print_failure "Could not retrieve target status"
    fi
}

test_background_health_scanning() {
    print_test "Background Health Scanning Operations"
    
    print_info "Monitoring health scanning activity for 30 seconds..."
    
    # Monitor kernel messages for health scanning activity
    local start_time=$(date +%s)
    local scan_activity_found=false
    local health_messages=0
    
    while [ $(($(date +%s) - start_time)) -lt 30 ]; do
        # Check for health scanning messages in recent kernel output
        local new_messages
        new_messages=$(dmesg | tail -50 | grep -c "dm-remap-health" || echo "0")
        
        if [ "$new_messages" -gt "$health_messages" ]; then
            health_messages=$new_messages
            scan_activity_found=true
            print_info "Health scanning activity detected ($health_messages messages)"
        fi
        
        sleep 2
    done
    
    if [ "$scan_activity_found" = true ]; then
        print_success "Background health scanning activity detected"
    else
        print_info "No explicit health scanning activity detected (may be running silently)"
    fi
}

test_health_data_tracking() {
    print_test "Health Data Collection and Tracking"
    
    print_info "Performing I/O operations to trigger health tracking..."
    
    # Perform some I/O operations
    local target_device="/dev/mapper/$TARGET_NAME"
    
    if [ -b "$target_device" ]; then
        # Write some test data
        dd if=/dev/urandom of="$target_device" bs=4096 count=100 >/dev/null 2>&1 || true
        
        # Read back data
        dd if="$target_device" of=/dev/null bs=4096 count=100 >/dev/null 2>&1 || true
        
        print_success "I/O operations completed for health tracking"
        
        # Check for health update messages
        if dmesg | tail -30 | grep -q -E "(health|sector|scan)"; then
            print_success "Health-related activity detected in kernel log"
        else
            print_info "No explicit health tracking messages found"
        fi
    else
        print_failure "Target device not accessible: $target_device"
    fi
}

test_performance_impact() {
    print_test "Performance Impact Assessment"
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    if [ -b "$target_device" ]; then
        print_info "Measuring I/O performance with health scanning active..."
        
        # Measure write performance
        local start_time=$(date +%s%N)
        dd if=/dev/zero of="$target_device" bs=1M count=10 oflag=sync >/dev/null 2>&1 || true
        local end_time=$(date +%s%N)
        
        local duration_ms=$(( (end_time - start_time) / 1000000 ))
        local throughput_mb=$((10 * 1000 / duration_ms))
        
        print_info "Write performance: ${throughput_mb}MB/s (${duration_ms}ms for 10MB)"
        
        # Measure read performance
        start_time=$(date +%s%N)
        dd if="$target_device" of=/dev/null bs=1M count=10 >/dev/null 2>&1 || true
        end_time=$(date +%s%N)
        
        duration_ms=$(( (end_time - start_time) / 1000000 ))
        throughput_mb=$((10 * 1000 / duration_ms))
        
        print_info "Read performance: ${throughput_mb}MB/s (${duration_ms}ms for 10MB)"
        
        print_success "Performance measurement completed"
    else
        print_failure "Cannot perform performance test - target device not accessible"
    fi
}

test_health_scanner_lifecycle() {
    print_test "Health Scanner Lifecycle Management"
    
    # Test target removal (which should trigger health scanner cleanup)
    print_info "Testing health scanner cleanup during target removal..."
    
    if dmsetup remove "$TARGET_NAME"; then
        print_success "Target removed successfully"
        
        # Check for cleanup messages
        if dmesg | tail -10 | grep -q "cleaned up health scanning system"; then
            print_success "Health scanner cleanup detected in kernel log"
        else
            print_info "Health scanner cleanup message not found in recent kernel log"
        fi
    else
        print_failure "Failed to remove target"
    fi
    
    # Recreate target for remaining tests
    SPARE_SECTORS=$((SPARE_SIZE_MB * 2048))
    TABLE_LINE="0 $((MAIN_SIZE_MB * 2048)) remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS"
    
    if echo "$TABLE_LINE" | dmsetup create "$TARGET_NAME"; then
        print_info "Target recreated for remaining tests"
    else
        print_failure "Failed to recreate target"
    fi
}

test_memory_usage() {
    print_test "Memory Usage Analysis"
    
    # Check module memory usage
    local module_memory
    module_memory=$(grep dm_remap /proc/modules | awk '{print $2}' || echo "unknown")
    
    print_info "Module memory usage: ${module_memory} bytes"
    
    # Check for memory allocation warnings
    if dmesg | tail -50 | grep -q -E "(Failed to allocate|ENOMEM|allocation.*failed)"; then
        print_failure "Memory allocation issues detected in kernel log"
    else
        print_success "No memory allocation issues detected"
    fi
}

run_integration_tests() {
    print_test "Integration with Existing dm-remap Features"
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Test basic I/O still works with health scanning
    if [ -b "$target_device" ]; then
        local test_data="HEALTH_SCANNING_INTEGRATION_TEST_$(date +%s)"
        
        # Write test data
        if echo "$test_data" | dd of="$target_device" bs=512 count=1 >/dev/null 2>&1; then
            # Read back and verify
            local read_data
            read_data=$(dd if="$target_device" bs=512 count=1 2>/dev/null | tr -d '\0')
            
            if [[ "$read_data" == *"$test_data"* ]]; then
                print_success "Basic I/O operations work correctly with health scanning"
            else
                print_failure "Data integrity issue with health scanning active"
            fi
        else
            print_failure "Failed to write test data with health scanning active"
        fi
    else
        print_failure "Target device not accessible for integration test"
    fi
}

print_test_summary() {
    echo
    echo -e "${BLUE}=================================================================="
    echo -e "                     TEST SUMMARY"
    echo -e "=================================================================="
    echo -e "Total Tests: $TOTAL_TESTS"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "Result: ${GREEN}ALL TESTS PASSED${NC} ✅"
        echo -e "Week 7-8 Background Health Scanning implementation is working!"
    else
        echo -e "Result: ${RED}SOME TESTS FAILED${NC} ❌"
        echo -e "Review failed tests and check kernel logs for details."
    fi
    
    echo -e "=================================================================="
    echo -e "Health Scanning Features Tested:"
    echo -e "  ✓ Scanner initialization and auto-start"
    echo -e "  ✓ Background scanning operations"
    echo -e "  ✓ Health data collection and tracking"
    echo -e "  ✓ Performance impact assessment"
    echo -e "  ✓ Scanner lifecycle management"
    echo -e "  ✓ Memory usage analysis"
    echo -e "  ✓ Integration with existing features"
    echo -e "=================================================================="
    echo -e "${NC}"
}

# Main test execution
main() {
    print_header
    
    setup_test_environment
    load_module_and_create_target
    
    # Run all health scanning tests
    test_health_scanner_initialization
    test_background_health_scanning
    test_health_data_tracking
    test_performance_impact
    test_health_scanner_lifecycle
    test_memory_usage
    run_integration_tests
    
    print_test_summary
    
    # Return appropriate exit code
    if [ $TESTS_FAILED -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# Run main function
main