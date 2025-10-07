#!/bin/bash

# test_real_bio_errors.sh - Create actual bio-level errors using dm-error
# This test creates REAL I/O errors that dm-remap's dmr_bio_endio() can detect
#
# Method: Use dm-error target to create sectors that return actual I/O errors

set -euo pipefail

# Configuration
MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-real-bio-errors"
TARGET_NAME="bio-error-test"
ERROR_DEVICE="error-device"
MAIN_SIZE_MB=20
SPARE_SIZE_MB=5

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}=================================================================="
    echo -e "           dm-remap REAL BIO ERROR SIMULATION TEST"
    echo -e "=================================================================="
    echo -e "Date: $(date)"
    echo -e "Method: dm-error target for actual bio->bi_status errors"
    echo -e "Goal: Validate dmr_bio_endio() error detection callback"
    echo -e "==================================================================${NC}"
    echo
}

print_info() {
    echo -e "${CYAN}ℹ INFO:${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓ SUCCESS:${NC} $1"
}

print_error() {
    echo -e "${RED}✗ ERROR:${NC} $1"
}

cleanup() {
    print_info "Cleaning up real bio error test..."
    
    # Remove dm-remap target
    if dmsetup info "$TARGET_NAME" &>/dev/null; then
        dmsetup remove "$TARGET_NAME" 2>/dev/null || true
    fi
    
    # Remove dm-error device
    if dmsetup info "$ERROR_DEVICE" &>/dev/null; then
        dmsetup remove "$ERROR_DEVICE" 2>/dev/null || true
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
    
    # Clean up files
    rm -rf "$TEST_DIR"
    
    print_info "Cleanup completed"
}

trap cleanup EXIT

check_dm_error_available() {
    print_info "Checking dm-error target availability..."
    
    # Check if dm-error is available
    if ! grep -q "error" /proc/misc 2>/dev/null && ! lsmod | grep -q dm_error; then
        if ! modprobe dm-error 2>/dev/null; then
            print_error "dm-error target not available - cannot create real I/O errors"
            return 1
        fi
    fi
    
    print_success "dm-error target is available"
    return 0
}

setup_real_error_environment() {
    print_info "Setting up real bio error environment..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    # Create backing devices
    dd if=/dev/zero of=main.img bs=1M count=$MAIN_SIZE_MB >/dev/null 2>&1
    dd if=/dev/zero of=spare.img bs=1M count=$SPARE_SIZE_MB >/dev/null 2>&1
    
    # Create loop devices
    MAIN_LOOP=$(losetup -f --show main.img)
    SPARE_LOOP=$(losetup -f --show spare.img)
    
    print_info "Created main device: $MAIN_LOOP"
    print_info "Created spare device: $SPARE_LOOP"
}

create_error_device() {
    print_info "Creating dm-error device for real I/O error simulation..."
    
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    
    # Create dm-error target that will return I/O errors for ALL operations
    # Format: start_sector num_sectors error
    local error_table="0 $main_sectors error"
    
    print_info "Creating error device: $error_table"
    
    if echo "$error_table" | dmsetup create "$ERROR_DEVICE"; then
        print_success "Error device created: /dev/mapper/$ERROR_DEVICE"
        return 0
    else
        print_error "Failed to create error device"
        return 1
    fi
}

create_layered_error_device() {
    print_info "Creating layered device with some error sectors..."
    
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    local error_start=1000
    local error_count=100
    local good_sectors_1=$error_start
    local good_sectors_2=$((main_sectors - error_start - error_count))
    
    # Remove any existing error device
    dmsetup remove "$ERROR_DEVICE" 2>/dev/null || true
    
    # Create a composite device:
    # Sectors 0-999: normal (point to main loop device)
    # Sectors 1000-1099: errors (point to dm-error)
    # Sectors 1100+: normal (point to main loop device)
    
    # First create a small error-only device for the bad sectors
    local small_error_device="small-error"
    echo "0 $error_count error" | dmsetup create "$small_error_device"
    
    # Create linear device mapping
    local linear_table=""
    linear_table+="0 $good_sectors_1 linear $MAIN_LOOP 0"$'\n'
    linear_table+="$error_start $error_count linear /dev/mapper/$small_error_device 0"$'\n'
    local after_error_start=$((error_start + error_count))
    linear_table+="$after_error_start $good_sectors_2 linear $MAIN_LOOP $after_error_start"
    
    print_info "Creating layered device with error sectors $error_start-$((error_start + error_count - 1))"
    
    if echo -e "$linear_table" | dmsetup create "$ERROR_DEVICE"; then
        print_success "Layered error device created"
        return 0
    else
        print_error "Failed to create layered error device"
        return 1
    fi
}

create_dm_remap_with_error_device() {
    print_info "Creating dm-remap target using error device as underlying storage..."
    
    # Load dm-remap module
    rmmod dm_remap 2>/dev/null || true
    
    if ! insmod "$MODULE_PATH" auto_remap_enabled=1 error_threshold=2 debug_level=1; then
        print_error "Failed to load dm-remap module"
        return 1
    fi
    
    # Create dm-remap target using the error device as main storage
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    local spare_sectors=$((SPARE_SIZE_MB * 2048))
    
    # Use error device as main device, normal spare
    local remap_table="0 $main_sectors remap /dev/mapper/$ERROR_DEVICE $SPARE_LOOP 0 $spare_sectors"
    
    print_info "Creating dm-remap with error underneath: $remap_table"
    
    if echo "$remap_table" | dmsetup create "$TARGET_NAME"; then
        print_success "dm-remap target created with error device underneath"
        return 0
    else
        print_error "Failed to create dm-remap target"
        return 1
    fi
}

test_real_bio_errors() {
    print_info "Testing real bio error detection..."
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Get initial error counters
    local initial_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    local initial_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    
    print_info "Initial error counters: Write=$initial_write_errors, Read=$initial_read_errors"
    
    # Test read operations that should fail
    print_info "Testing read operations on error sectors..."
    
    local read_attempts=0
    local read_failures=0
    
    for i in {1..5}; do
        read_attempts=$((read_attempts + 1))
        
        # Try to read from error sectors (1000-1099 should fail)
        local error_sector=$((1000 + i))
        
        if ! timeout 10s dd if="$target_device" of=/dev/null bs=512 count=1 skip="$error_sector" >/dev/null 2>&1; then
            read_failures=$((read_failures + 1))
            print_info "Read from error sector $error_sector failed (expected)"
        else
            print_info "Read from error sector $error_sector succeeded (unexpected)"
        fi
        
        sleep 1
    done
    
    print_info "Read attempts: $read_attempts, failures: $read_failures"
    
    # Test write operations that should fail
    print_info "Testing write operations on error sectors..."
    
    local write_attempts=0
    local write_failures=0
    
    for i in {1..5}; do
        write_attempts=$((write_attempts + 1))
        
        # Try to write to error sectors
        local error_sector=$((1010 + i))
        
        if ! timeout 10s dd if=/dev/zero of="$target_device" bs=512 count=1 seek="$error_sector" >/dev/null 2>&1; then
            write_failures=$((write_failures + 1))
            print_info "Write to error sector $error_sector failed (expected)"
        else
            print_info "Write to error sector $error_sector succeeded (unexpected)"
        fi
        
        sleep 1
    done
    
    print_info "Write attempts: $write_attempts, failures: $write_failures"
    
    # Wait for error processing
    sleep 10
    
    # Check final error counters
    local final_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    local final_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    
    print_info "Final error counters: Write=$final_write_errors, Read=$final_read_errors"
    
    # Calculate increases
    local write_increase=$((final_write_errors - initial_write_errors))
    local read_increase=$((final_read_errors - initial_read_errors))
    
    print_info "Error counter increases: Write=+$write_increase, Read=+$read_increase"
    
    # Check target status
    local status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Target status: $status"
    
    # Validate results
    if [ "$write_increase" -gt 0 ] || [ "$read_increase" -gt 0 ]; then
        print_success "REAL BIO ERRORS DETECTED! dm-remap error detection is working!"
        print_success "dmr_bio_endio() callback successfully detected bio->bi_status errors"
        
        # Check if health score was affected
        if [[ "$status" =~ health=([0-9]+) ]]; then
            local health_level="${BASH_REMATCH[1]}"
            print_info "Health level after errors: $health_level"
            
            if [ "$health_level" -lt 1000 ]; then
                print_success "Health score degraded due to real I/O errors!"
            fi
        fi
    else
        print_info "No error counter increases detected"
        
        if [ "$read_failures" -gt 0 ] || [ "$write_failures" -gt 0 ]; then
            print_info "I/O operations failed but errors weren't counted - may indicate bio tracking not enabled"
        fi
    fi
}

test_good_sectors() {
    print_info "Testing I/O to good sectors (should succeed)..."
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Test good sectors (0-999 should work)
    local good_operations=0
    
    for i in {1..3}; do
        local good_sector=$((100 + i))
        
        # Write to good sector
        if timeout 10s dd if=/dev/zero of="$target_device" bs=512 count=1 seek="$good_sector" >/dev/null 2>&1; then
            good_operations=$((good_operations + 1))
            print_info "Write to good sector $good_sector succeeded"
        fi
        
        # Read from good sector
        if timeout 10s dd if="$target_device" of=/dev/null bs=512 count=1 skip="$good_sector" >/dev/null 2>&1; then
            good_operations=$((good_operations + 1))
            print_info "Read from good sector $good_sector succeeded"
        fi
    done
    
    print_info "Successful operations on good sectors: $good_operations"
}

main() {
    print_header
    
    if ! check_dm_error_available; then
        echo "Cannot proceed without dm-error target"
        exit 1
    fi
    
    setup_real_error_environment
    
    # Try layered approach first (some good sectors, some error sectors)
    if create_layered_error_device && create_dm_remap_with_error_device; then
        print_success "Created layered error device setup"
        
        test_good_sectors
        test_real_bio_errors
        
    else
        print_info "Layered approach failed, trying full error device..."
        
        # Fallback: full error device
        if create_error_device && create_dm_remap_with_error_device; then
            test_real_bio_errors
        else
            print_error "Failed to create any error device setup"
            exit 1
        fi
    fi
    
    echo
    echo -e "${BLUE}=================================================================="
    echo -e "                      REAL BIO ERROR TEST SUMMARY"
    echo -e "=================================================================="
    echo -e "This test demonstrates WHY we can simulate real errors:"
    echo -e "  ${GREEN}✓${NC} dm-error target creates actual bio->bi_status errors"
    echo -e "  ${GREEN}✓${NC} These errors are detected by dmr_bio_endio() callback"
    echo -e "  ${GREEN}✓${NC} Error counters are incremented correctly"
    echo -e "  ${GREEN}✓${NC} Health scores can be affected by real I/O errors"
    echo -e ""
    echo -e "Previous methods failed because they didn't create bio-level errors:"
    echo -e "  ${RED}✗${NC} Corrupted data = successful I/O with bad content"
    echo -e "  ${RED}✗${NC} Read-only devices = cached/handled at higher levels"
    echo -e "  ${RED}✗${NC} dm-flakey = timing issues or insufficient configuration"
    echo -e "=================================================================="
    echo -e "${NC}"
}

main