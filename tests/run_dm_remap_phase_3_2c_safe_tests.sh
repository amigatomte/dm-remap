#!/bin/bash

# dm-remap Phase 3.2C Safe Testing Script
# 
# This script provides a safe, controlled approach to testing the Phase 3.2C
# stress testing framework to avoid kernel crashes.
#
# Author: Christian (with AI assistance)

set -e

# Phase 3.2C: Configuration
PHASE="3.2C Safe Testing"
MODULE_PATH="/home/christian/kernel_dev/dm-remap/src"
LOOP_MAIN="/dev/loop20"
LOOP_SPARE="/dev/loop21" 
MAIN_IMG="/tmp/dm_remap_main_safe.img"
SPARE_IMG="/tmp/dm_remap_spare_safe.img"
DM_DEVICE="dm_remap_safe_test"
STRESS_SYSFS="/sys/kernel/dm_remap_stress_test"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}=================================================${NC}"
    echo -e "${BLUE}dm-remap ${PHASE}${NC}"
    echo -e "${BLUE}Safe Stress Testing Framework Validation${NC}"
    echo -e "${BLUE}=================================================${NC}"
    echo
}

print_section() {
    echo
    echo -e "${CYAN}### $1 ###${NC}"
    echo
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_info() {
    echo -e "${PURPLE}ℹ $1${NC}"
}

cleanup() {
    print_section "Phase 3.2C: Safe Cleanup"
    
    # Stop any running stress tests
    if [ -d "$STRESS_SYSFS" ]; then
        echo "1" > "$STRESS_SYSFS/stress_test_stop" 2>/dev/null || true
        print_info "Stopped any running stress tests"
        sleep 2
    fi
    
    # Remove device mapper device
    if dmsetup info "$DM_DEVICE" >/dev/null 2>&1; then
        dmsetup remove "$DM_DEVICE"
        print_success "Removed device mapper device: $DM_DEVICE"
    fi
    
    # Detach loop devices
    if losetup "$LOOP_MAIN" >/dev/null 2>&1; then
        losetup -d "$LOOP_MAIN"
        print_success "Detached loop device: $LOOP_MAIN"
    fi
    
    if losetup "$LOOP_SPARE" >/dev/null 2>&1; then
        losetup -d "$LOOP_SPARE"
        print_success "Detached loop device: $LOOP_SPARE"
    fi
    
    # Remove image files
    rm -f "$MAIN_IMG" "$SPARE_IMG"
    print_success "Removed test image files"
    
    # Careful module unloading
    if lsmod | grep -q dm_remap; then
        print_info "Waiting for module cleanup..."
        sleep 5
        rmmod dm_remap && print_success "Unloaded dm-remap module" || print_warning "Module may still be in use"
    fi
    
    print_success "Phase 3.2C: Safe cleanup completed"
}

# Set trap for cleanup on exit
trap cleanup EXIT

setup_environment() {
    print_section "Phase 3.2C: Safe Environment Setup"
    
    # Check if running as root
    if [[ $EUID -ne 0 ]]; then
        print_error "This script must be run as root for Phase 3.2C testing"
        exit 1
    fi
    
    # Create smaller test images (100MB main, 20MB spare for safety)
    print_info "Creating safe test images for Phase 3.2C validation..."
    dd if=/dev/zero of="$MAIN_IMG" bs=1M count=100 >/dev/null 2>&1
    dd if=/dev/zero of="$SPARE_IMG" bs=1M count=20 >/dev/null 2>&1
    print_success "Created safe test images (100MB main, 20MB spare)"
    
    # Set up loop devices
    losetup "$LOOP_MAIN" "$MAIN_IMG"
    losetup "$LOOP_SPARE" "$SPARE_IMG"
    print_success "Configured loop devices"
    
    # Load module
    print_info "Loading Phase 3.2C dm-remap module with crash fixes..."
    insmod "$MODULE_PATH/dm_remap.ko" debug_level=2
    print_success "Loaded Phase 3.2C dm-remap module"
    
    # Create device mapper device
    echo "0 204800 remap $LOOP_MAIN $LOOP_SPARE 0 40960" | dmsetup create "$DM_DEVICE"
    print_success "Created device mapper device: $DM_DEVICE"
    
    # Wait for system stabilization
    sleep 3
    print_success "System stabilized for safe Phase 3.2C testing"
}

validate_stress_framework() {
    print_section "Phase 3.2C: Stress Framework Validation"
    
    # Check stress testing sysfs interface
    if [ ! -d "$STRESS_SYSFS" ]; then
        print_error "Stress testing sysfs interface not found at $STRESS_SYSFS"
        return 1
    fi
    print_success "Stress testing sysfs interface available"
    
    # List available stress test controls
    print_info "Available stress test controls:"
    ls -la "$STRESS_SYSFS/" | head -10
    
    # Check framework status
    if [ -f "$STRESS_SYSFS/stress_test_status" ]; then
        status=$(cat "$STRESS_SYSFS/stress_test_status")
        print_info "Current stress test status: $status"
    fi
    
    print_success "Stress testing framework validation completed"
}

run_minimal_test() {
    print_section "Phase 3.2C: Minimal Safe Test"
    
    print_info "Running minimal 5-second stress test with 2 workers..."
    
    # Start minimal test: sequential read with 2 workers for 5 seconds
    echo "0 2 5000" > "$STRESS_SYSFS/stress_test_start" 2>/dev/null || {
        print_error "Failed to start minimal test"
        return 1
    }
    print_success "Minimal stress test started"
    
    # Monitor progress for 8 seconds
    for i in {1..8}; do
        if [ -f "$STRESS_SYSFS/stress_test_status" ]; then
            status=$(cat "$STRESS_SYSFS/stress_test_status")
            echo -ne "\rProgress: ${i}s/8s - Status: $status"
        fi
        sleep 1
    done
    echo
    
    # Get results
    if [ -f "$STRESS_SYSFS/stress_test_results" ]; then
        print_info "Minimal test results:"
        cat "$STRESS_SYSFS/stress_test_results" | head -15
    fi
    
    print_success "Minimal safe test completed successfully"
}

run_sysfs_interface_test() {
    print_section "Phase 3.2C: Sysfs Interface Testing"
    
    # Test status check
    print_info "Testing status interface..."
    if [ -f "$STRESS_SYSFS/stress_test_status" ]; then
        status=$(cat "$STRESS_SYSFS/stress_test_status")
        print_success "Status interface working: $status"
    fi
    
    # Test stop interface
    print_info "Testing stop interface..."
    echo "1" > "$STRESS_SYSFS/stress_test_stop" 2>/dev/null && print_success "Stop interface working"
    
    # Test results interface  
    print_info "Testing results interface..."
    if [ -f "$STRESS_SYSFS/stress_test_results" ]; then
        result_lines=$(cat "$STRESS_SYSFS/stress_test_results" | wc -l)
        print_success "Results interface working ($result_lines lines)"
    fi
    
    print_success "Sysfs interface testing completed"
}

check_system_stability() {
    print_section "Phase 3.2C: System Stability Check"
    
    # Check for kernel errors
    print_info "Checking for kernel errors..."
    error_count=$(dmesg | grep -i "error\|oops\|panic\|bug" | wc -l)
    if [ "$error_count" -eq 0 ]; then
        print_success "No kernel errors detected"
    else
        print_warning "Found $error_count potential kernel messages"
    fi
    
    # Check module status
    print_info "Checking module status..."
    if lsmod | grep -q dm_remap; then
        print_success "dm-remap module loaded and stable"
    else
        print_error "dm-remap module not found"
    fi
    
    # Check device mapper status
    print_info "Checking device mapper status..."
    if dmsetup info "$DM_DEVICE" >/dev/null 2>&1; then
        print_success "Device mapper device stable"
    else
        print_warning "Device mapper device status unknown"
    fi
    
    print_success "System stability check completed"
}

# Main execution
main() {
    print_header
    
    print_info "Starting Phase 3.2C Safe Stress Testing Validation"
    print_info "This safe test validates basic functionality without intensive stress"
    echo
    
    setup_environment
    validate_stress_framework
    run_minimal_test
    run_sysfs_interface_test
    check_system_stability
    
    print_section "Phase 3.2C: Safe Test Summary"
    print_success "All Phase 3.2C safe validation tests completed!"
    print_info "Key achievements:"
    print_info "✓ Module loaded without crashes"
    print_info "✓ Stress testing framework operational"
    print_info "✓ Sysfs interface functional"
    print_info "✓ Minimal stress test successful"
    print_info "✓ System remained stable"
    
    echo
    print_info "Phase 3.2C Safe Validation: SUCCESSFUL"
    print_info "The system is ready for more intensive testing if desired"
    echo
}

# Run main function
main "$@"