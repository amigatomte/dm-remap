#!/bin/bash

# Week 9-10 Hotpath Optimization Test Suite
# Tests hotpath I/O optimization with cache-aligned structures, batch processing,
# and sysfs monitoring interface

set -e

# Test configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_LOG="${SCRIPT_DIR}/test_hotpath_optimization_week9.log"
LOOP0_SIZE=100M
LOOP1_SIZE=50M

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date '+%H:%M:%S')]${NC} $1" | tee -a "$TEST_LOG"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$TEST_LOG"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$TEST_LOG"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$TEST_LOG"
}

# Cleanup function
cleanup() {
    log "Cleaning up test environment..."
    
    # Remove dm-remap device
    if dmsetup info dm-remap-test >/dev/null 2>&1; then
        dmsetup remove dm-remap-test 2>/dev/null || true
    fi
    
    # Detach loop devices
    losetup -d /dev/loop0 2>/dev/null || true
    losetup -d /dev/loop1 2>/dev/null || true
    
    # Remove temporary files
    rm -f /tmp/main_device.img /tmp/spare_device.img
    
    log "Cleanup completed"
}

# Setup signal handlers
trap cleanup EXIT
trap cleanup INT
trap cleanup TERM

# Initialize test environment
init_test() {
    log "=========================================="
    log "Week 9-10 Hotpath Optimization Test Suite"
    log "=========================================="
    
    # Clear previous log
    > "$TEST_LOG"
    
    # Check if we're running as root
    if [[ $EUID -ne 0 ]]; then
        error "This test must be run as root"
        exit 1
    fi
    
    # Check if dm-remap module is loaded
    if ! lsmod | grep -q dm_remap; then
        log "Loading dm-remap module..."
        cd "${SCRIPT_DIR}/../src"
        if ! insmod dm_remap.ko; then
            error "Failed to load dm-remap module"
            exit 1
        fi
    fi
    
    # Create test device files
    log "Creating test device files..."
    dd if=/dev/zero of=/tmp/main_device.img bs=1M count=${LOOP0_SIZE%M} 2>/dev/null
    dd if=/dev/zero of=/tmp/spare_device.img bs=1M count=${LOOP1_SIZE%M} 2>/dev/null
    
    # Setup loop devices
    log "Setting up loop devices..."
    losetup /dev/loop0 /tmp/main_device.img
    losetup /dev/loop1 /tmp/spare_device.img
    
    success "Test environment initialized"
}

# Test hotpath optimization initialization
test_hotpath_init() {
    log "Testing hotpath optimization initialization..."
    
    # Create dm-remap device with hotpath optimization
    echo "0 $(blockdev --getsz /dev/loop0) remap /dev/loop0 /dev/loop1" | \
        dmsetup create dm-remap-test
    
    if ! dmsetup info dm-remap-test >/dev/null 2>&1; then
        error "Failed to create dm-remap device"
        return 1
    fi
    
    # Check for hotpath manager in dmesg
    if dmesg | tail -20 | grep -q "Hotpath optimization initialized"; then
        success "Hotpath optimization initialized successfully"
    else
        warning "Hotpath initialization message not found in dmesg"
    fi
    
    return 0
}

# Test sysfs interface creation
test_sysfs_interface() {
    log "Testing hotpath sysfs interface creation..."
    
    # Find the device mapper device number
    DM_NAME=$(dmsetup info dm-remap-test | grep "Name:" | awk '{print $2}')
    DM_MINOR=$(dmsetup info dm-remap-test | grep "Minor:" | awk '{print $2}')
    
    # Look for sysfs paths
    SYSFS_PATHS=(
        "/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_stats"
        "/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_efficiency"
        "/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_batch_size"
        "/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_prefetch_distance"
        "/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_reset"
    )
    
    local sysfs_found=0
    for path in "${SYSFS_PATHS[@]}"; do
        if [[ -r "$path" ]]; then
            success "Found sysfs interface: $path"
            sysfs_found=1
            
            # Try to read the interface
            log "Reading $path:"
            cat "$path" | head -10 | sed 's/^/  /' | tee -a "$TEST_LOG"
        else
            warning "Sysfs interface not found: $path"
        fi
    done
    
    if [[ $sysfs_found -eq 1 ]]; then
        success "Hotpath sysfs interface is accessible"
        return 0
    else
        error "No hotpath sysfs interfaces found"
        return 1
    fi
}

# Test hotpath performance with I/O operations
test_hotpath_performance() {
    log "Testing hotpath I/O performance optimization..."
    
    local dm_device="/dev/mapper/dm-remap-test"
    if [[ ! -b "$dm_device" ]]; then
        error "dm-remap device not found: $dm_device"
        return 1
    fi
    
    # Get initial stats
    DM_MINOR=$(dmsetup info dm-remap-test | grep "Minor:" | awk '{print $2}')
    STATS_FILE="/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_stats"
    
    if [[ -r "$STATS_FILE" ]]; then
        log "Initial hotpath statistics:"
        cat "$STATS_FILE" | sed 's/^/  /' | tee -a "$TEST_LOG"
    fi
    
    # Perform sequential I/O operations (should trigger fast path)
    log "Performing sequential read operations..."
    dd if="$dm_device" of=/dev/null bs=4k count=1000 2>/dev/null &
    dd if="$dm_device" of=/dev/null bs=4k count=1000 skip=1000 2>/dev/null &
    dd if="$dm_device" of=/dev/null bs=4k count=1000 skip=2000 2>/dev/null &
    wait
    
    # Perform write operations
    log "Performing sequential write operations..."
    dd if=/dev/zero of="$dm_device" bs=4k count=500 2>/dev/null &
    dd if=/dev/zero of="$dm_device" bs=4k count=500 seek=1000 2>/dev/null &
    wait
    
    # Check final stats
    if [[ -r "$STATS_FILE" ]]; then
        log "Final hotpath statistics:"
        cat "$STATS_FILE" | sed 's/^/  /' | tee -a "$TEST_LOG"
        
        # Check if we have I/O activity
        if grep -q "Total I/Os: [1-9]" "$STATS_FILE"; then
            success "Hotpath I/O processing detected"
            
            # Check efficiency
            if grep -q "Fast-path efficiency:" "$STATS_FILE"; then
                local efficiency=$(grep "Fast-path efficiency:" "$STATS_FILE" | awk '{print $3}' | tr -d '%')
                if [[ -n "$efficiency" && "$efficiency" -gt 0 ]]; then
                    success "Fast-path efficiency: ${efficiency}%"
                else
                    warning "Fast-path efficiency is 0%"
                fi
            fi
            
            return 0
        else
            warning "No I/O activity detected in hotpath statistics"
            return 1
        fi
    else
        error "Cannot read hotpath statistics"
        return 1
    fi
}

# Test cache-aligned structure performance
test_cache_alignment() {
    log "Testing cache-aligned structure performance..."
    
    # The cache alignment is internal to the kernel module
    # We can only verify through successful operation and dmesg logs
    
    log "Performing cache-stress I/O pattern..."
    local dm_device="/dev/mapper/dm-remap-test"
    
    # Random I/O pattern to stress cache alignment
    for i in {1..10}; do
        local offset=$((RANDOM % 1000))
        dd if=/dev/zero of="$dm_device" bs=64 count=1 seek=$offset 2>/dev/null &
    done
    wait
    
    # Check for cache-related messages in dmesg
    if dmesg | tail -50 | grep -i "cache\|hotpath\|fast" | grep -q "dm-remap"; then
        success "Cache-aligned processing detected in kernel logs"
    else
        log "No specific cache alignment messages found (this is normal)"
    fi
    
    return 0
}

# Test batch processing functionality
test_batch_processing() {
    log "Testing batch processing functionality..."
    
    local dm_device="/dev/mapper/dm-remap-test"
    DM_MINOR=$(dmsetup info dm-remap-test | grep "Minor:" | awk '{print $2}')
    STATS_FILE="/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_stats"
    
    # Reset statistics first
    RESET_FILE="/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_reset"
    if [[ -w "$RESET_FILE" ]]; then
        echo "1" > "$RESET_FILE" 2>/dev/null
        log "Reset hotpath statistics"
    fi
    
    # Perform many small I/Os to trigger batch processing
    log "Performing batch I/O operations..."
    for i in {1..50}; do
        dd if=/dev/zero of="$dm_device" bs=512 count=1 seek=$i 2>/dev/null &
        if [[ $((i % 10)) -eq 0 ]]; then
            wait  # Let batches accumulate
        fi
    done
    wait
    
    # Check batch processing statistics
    if [[ -r "$STATS_FILE" ]]; then
        log "Batch processing statistics:"
        cat "$STATS_FILE" | grep -i batch | sed 's/^/  /' | tee -a "$TEST_LOG"
        
        if grep -q "Batch processed: [1-9]" "$STATS_FILE"; then
            success "Batch processing is working"
            return 0
        else
            warning "No batch processing detected"
            return 1
        fi
    else
        error "Cannot read batch processing statistics"
        return 1
    fi
}

# Test statistics reset functionality
test_stats_reset() {
    log "Testing statistics reset functionality..."
    
    DM_MINOR=$(dmsetup info dm-remap-test | grep "Minor:" | awk '{print $2}')
    STATS_FILE="/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_stats"
    RESET_FILE="/sys/block/dm-${DM_MINOR}/dm-remap/hotpath/hotpath_reset"
    
    if [[ ! -w "$RESET_FILE" ]]; then
        error "Cannot write to reset file: $RESET_FILE"
        return 1
    fi
    
    # Perform some I/O to generate statistics
    dd if=/dev/zero of="/dev/mapper/dm-remap-test" bs=4k count=100 2>/dev/null
    
    # Check we have non-zero statistics
    if [[ -r "$STATS_FILE" ]]; then
        local total_before=$(grep "Total I/Os:" "$STATS_FILE" | awk '{print $3}')
        log "Total I/Os before reset: $total_before"
        
        # Reset statistics
        echo "1" > "$RESET_FILE"
        sleep 1
        
        # Check statistics are reset
        local total_after=$(grep "Total I/Os:" "$STATS_FILE" | awk '{print $3}')
        log "Total I/Os after reset: $total_after"
        
        if [[ "$total_after" -eq 0 ]]; then
            success "Statistics reset successfully"
            return 0
        else
            error "Statistics not properly reset"
            return 1
        fi
    else
        error "Cannot read statistics file"
        return 1
    fi
}

# Run all tests
run_all_tests() {
    local failed_tests=0
    local total_tests=6
    
    init_test
    
    # Run individual tests
    test_hotpath_init || ((failed_tests++))
    test_sysfs_interface || ((failed_tests++))
    test_hotpath_performance || ((failed_tests++))
    test_cache_alignment || ((failed_tests++))
    test_batch_processing || ((failed_tests++))
    test_stats_reset || ((failed_tests++))
    
    # Final summary
    log "=========================================="
    log "Test Summary"
    log "=========================================="
    log "Total tests: $total_tests"
    log "Passed: $((total_tests - failed_tests))"
    log "Failed: $failed_tests"
    
    if [[ $failed_tests -eq 0 ]]; then
        success "All hotpath optimization tests PASSED!"
        log "Week 9-10 Hotpath Optimization is fully functional"
        return 0
    else
        error "$failed_tests test(s) FAILED"
        return 1
    fi
}

# Main execution
main() {
    if run_all_tests; then
        success "Week 9-10 Hotpath Optimization Test Suite completed successfully"
        exit 0
    else
        error "Week 9-10 Hotpath Optimization Test Suite completed with failures"
        exit 1
    fi
}

# Execute main function
main "$@"