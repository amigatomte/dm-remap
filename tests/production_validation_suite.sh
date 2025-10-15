#!/bin/bash
#
# dm-remap v4.0 Phase 1 - Production Validation Suite
# Comprehensive testing after merge to main branch
#
# This script runs a full battery of tests to validate the v4.0 Phase 1 release
# including module loading, functional tests, stress tests, and performance validation.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results tracking
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Log file
LOG_DIR="tests/results"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/production_validation_$(date +%Y%m%d_%H%M%S).log"

# Function to print colored output
print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# Function to run a test and track results
run_test() {
    local test_name="$1"
    local test_command="$2"
    local optional="${3:-false}"
    
    print_info "Running: $test_name"
    
    if eval "$test_command" >> "$LOG_FILE" 2>&1; then
        print_success "$test_name PASSED"
        ((TESTS_PASSED++))
        return 0
    else
        if [ "$optional" = "true" ]; then
            print_warning "$test_name SKIPPED (optional)"
            ((TESTS_SKIPPED++))
            return 0
        else
            print_error "$test_name FAILED"
            ((TESTS_FAILED++))
            return 1
        fi
    fi
}

# Function to check if running as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        print_error "This script must be run as root (sudo)"
        exit 1
    fi
}

# Function to cleanup any existing dm devices
cleanup_devices() {
    print_info "Cleaning up any existing dm-remap devices..."
    
    # Remove any existing dm-remap devices
    for dev in $(dmsetup ls | grep remap | awk '{print $1}'); do
        dmsetup remove "$dev" 2>/dev/null || true
    done
    
    # Unload modules if loaded
    rmmod dm-remap-v4-setup-reassembly 2>/dev/null || true
    rmmod dm-remap-v4-spare-pool 2>/dev/null || true
    rmmod dm-remap-v4-metadata 2>/dev/null || true
    rmmod dm-remap-v4-real 2>/dev/null || true
    
    print_success "Cleanup complete"
}

# Main test execution
main() {
    print_header "dm-remap v4.0 Phase 1 - Production Validation Suite"
    echo "Started at: $(date)"
    echo "Log file: $LOG_FILE"
    
    # Check prerequisites
    check_root
    
    # Initial cleanup
    cleanup_devices
    
    print_header "Phase 1: Build Verification"
    
    # Verify all modules exist
    run_test "Check dm-remap-v4-real.ko exists" \
        "[ -f src/dm-remap-v4-real.ko ]"
    
    run_test "Check dm-remap-v4-metadata.ko exists" \
        "[ -f src/dm-remap-v4-metadata.ko ]"
    
    run_test "Check dm-remap-v4-spare-pool.ko exists" \
        "[ -f src/dm-remap-v4-spare-pool.ko ]"
    
    run_test "Check dm-remap-v4-setup-reassembly.ko exists" \
        "[ -f src/dm-remap-v4-setup-reassembly.ko ]"
    
    # Verify module info
    run_test "Verify dm-remap-v4-real module info" \
        "modinfo src/dm-remap-v4-real.ko | grep -q 'filename:'"
    
    print_header "Phase 2: Module Loading Tests"
    
    # Test basic module loading
    if [ -f tests/test_v4_module_loading.sh ]; then
        run_test "Module loading test suite" \
            "bash tests/test_v4_module_loading.sh"
    else
        print_warning "Module loading test script not found, skipping"
        ((TESTS_SKIPPED++))
    fi
    
    print_header "Phase 3: Priority 3 - Spare Pool Functional Tests"
    
    if [ -f tests/test_v4_spare_pool_functional.sh ]; then
        run_test "Spare Pool functional test suite" \
            "bash tests/test_v4_spare_pool_functional.sh"
    else
        print_warning "Spare pool test script not found, skipping"
        ((TESTS_SKIPPED++))
    fi
    
    print_header "Phase 4: Priority 6 - Setup Reassembly Tests"
    
    if [ -f tests/test_v4_setup_reassembly_module.sh ]; then
        run_test "Setup Reassembly module test suite" \
            "bash tests/test_v4_setup_reassembly_module.sh"
    else
        print_warning "Setup reassembly test script not found, skipping"
        ((TESTS_SKIPPED++))
    fi
    
    print_header "Phase 5: Kernel Symbol Verification"
    
    # Load real module for symbol verification
    run_test "Load dm-remap-v4-real module" \
        "insmod src/dm-remap-v4-real.ko"
    
    # Verify exported symbols
    run_test "Verify dm_remap_v4_calculate_confidence_score export" \
        "grep -q 'dm_remap_v4_calculate_confidence_score' /proc/kallsyms"
    
    run_test "Verify dm_remap_v4_update_metadata_version export" \
        "grep -q 'dm_remap_v4_update_metadata_version' /proc/kallsyms"
    
    run_test "Verify dm_remap_v4_verify_metadata_integrity export" \
        "grep -q 'dm_remap_v4_verify_metadata_integrity' /proc/kallsyms"
    
    # Unload for next tests
    run_test "Unload dm-remap-v4-real module" \
        "rmmod dm-remap-v4-real"
    
    print_header "Phase 6: Device Creation and I/O Tests"
    
    # Create test images
    print_info "Creating test device images..."
    dd if=/dev/zero of=/tmp/dm_remap_main_test.img bs=1M count=100 2>/dev/null
    dd if=/dev/zero of=/tmp/dm_remap_spare_test.img bs=1M count=100 2>/dev/null
    
    # Setup loop devices
    MAIN_LOOP=$(losetup -f)
    SPARE_LOOP=$(losetup -f --show /tmp/dm_remap_spare_test.img)
    losetup "$MAIN_LOOP" /tmp/dm_remap_main_test.img
    
    # Load module
    run_test "Load dm-remap-v4-real for I/O test" \
        "insmod src/dm-remap-v4-real.ko"
    
    # Create dm device
    SECTORS=$(blockdev --getsz "$MAIN_LOOP")
    TABLE="0 $SECTORS remap $MAIN_LOOP 0"
    
    run_test "Create dm-remap device" \
        "echo \"$TABLE\" | dmsetup create remap-test"
    
    # Basic I/O test
    run_test "Write test data to dm-remap device" \
        "dd if=/dev/urandom of=/dev/mapper/remap-test bs=4K count=100 2>/dev/null"
    
    run_test "Read test data from dm-remap device" \
        "dd if=/dev/mapper/remap-test of=/dev/null bs=4K count=100 2>/dev/null"
    
    # Cleanup I/O test
    run_test "Remove dm-remap test device" \
        "dmsetup remove remap-test"
    
    run_test "Unload dm-remap-v4-real after I/O test" \
        "rmmod dm-remap-v4-real"
    
    # Cleanup loop devices
    losetup -d "$MAIN_LOOP" 2>/dev/null || true
    losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rm -f /tmp/dm_remap_main_test.img /tmp/dm_remap_spare_test.img
    
    print_header "Phase 7: Module Dependency Verification"
    
    # Test loading modules in correct order
    run_test "Load dm-remap-v4-metadata module" \
        "insmod src/dm-remap-v4-metadata.ko"
    
    run_test "Load dm-remap-v4-spare-pool module (depends on metadata)" \
        "insmod src/dm-remap-v4-spare-pool.ko"
    
    run_test "Verify spare pool module loaded" \
        "lsmod | grep -q dm_remap_v4_spare_pool"
    
    # Unload in reverse order
    run_test "Unload dm-remap-v4-spare-pool module" \
        "rmmod dm-remap-v4-spare-pool"
    
    run_test "Unload dm-remap-v4-metadata module" \
        "rmmod dm-remap-v4-metadata"
    
    print_header "Phase 8: Kernel Compatibility Verification"
    
    # Check kernel version
    KERNEL_VERSION=$(uname -r)
    print_info "Testing on kernel version: $KERNEL_VERSION"
    
    # Verify kernel is 6.x (required for v4.0)
    run_test "Verify kernel version 6.x or higher" \
        "uname -r | grep -q '^6\.'"
    
    # Check for required kernel symbols
    run_test "Verify bdev_file_open_by_path available" \
        "grep -q 'bdev_file_open_by_path' /proc/kallsyms || grep -q 'blkdev_get_by_path' /proc/kallsyms"
    
    print_header "Phase 9: Stress Test (Optional)"
    
    # Quick stress test - load/unload cycles
    print_info "Running module load/unload stress test (10 cycles)..."
    
    STRESS_PASSED=true
    for i in {1..10}; do
        if ! insmod src/dm-remap-v4-real.ko 2>/dev/null; then
            print_error "Stress test failed at cycle $i (load)"
            STRESS_PASSED=false
            break
        fi
        
        if ! rmmod dm-remap-v4-real 2>/dev/null; then
            print_error "Stress test failed at cycle $i (unload)"
            STRESS_PASSED=false
            break
        fi
    done
    
    if $STRESS_PASSED; then
        print_success "Stress test PASSED (10 load/unload cycles)"
        ((TESTS_PASSED++))
    else
        print_error "Stress test FAILED"
        ((TESTS_FAILED++))
        # Cleanup
        rmmod dm-remap-v4-real 2>/dev/null || true
    fi
    
    print_header "Phase 10: Documentation Verification"
    
    run_test "Verify README.md exists" \
        "[ -f README.md ]"
    
    run_test "Verify V4_ROADMAP.md exists" \
        "[ -f docs/development/V4_ROADMAP.md ]"
    
    run_test "Verify build documentation exists" \
        "[ -f docs/development/V4_BUILD_SUCCESS.md ]"
    
    # Final cleanup
    cleanup_devices
    
    # Print summary
    print_header "Test Summary"
    
    TOTAL_TESTS=$((TESTS_PASSED + TESTS_FAILED + TESTS_SKIPPED))
    
    echo "Total Tests Run:    $TOTAL_TESTS"
    print_success "Tests Passed:       $TESTS_PASSED"
    print_error "Tests Failed:       $TESTS_FAILED"
    print_warning "Tests Skipped:      $TESTS_SKIPPED"
    
    echo ""
    echo "Detailed log: $LOG_FILE"
    echo "Completed at: $(date)"
    
    # Exit with appropriate code
    if [ $TESTS_FAILED -eq 0 ]; then
        print_header "✓ Production Validation PASSED"
        echo -e "\n${GREEN}dm-remap v4.0 Phase 1 is production-ready!${NC}\n"
        exit 0
    else
        print_header "✗ Production Validation FAILED"
        echo -e "\n${RED}Please review failures in $LOG_FILE${NC}\n"
        exit 1
    fi
}

# Run main function
main "$@"
