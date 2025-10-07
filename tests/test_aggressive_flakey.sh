#!/bin/bash

# test_aggressive_flakey.sh - More aggressive dm-flakey configuration
# Uses very short intervals and multiple attempts to trigger real errors

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-aggressive-flakey"
TARGET_NAME="aggressive-flakey-test"
FLAKEY_NAME="aggressive-flakey"
MAIN_SIZE_MB=10
SPARE_SIZE_MB=5

print_info() {
    echo -e "\033[0;36mℹ INFO:\033[0m $1"
}

print_success() {
    echo -e "\033[0;32m✓ SUCCESS:\033[0m $1"
}

cleanup() {
    dmsetup remove "$TARGET_NAME" 2>/dev/null || true
    dmsetup remove "$FLAKEY_NAME" 2>/dev/null || true
    [ -n "${MAIN_LOOP:-}" ] && losetup -d "$MAIN_LOOP" 2>/dev/null || true
    [ -n "${SPARE_LOOP:-}" ] && losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rmmod dm_remap 2>/dev/null || true
    rm -rf "$TEST_DIR" 
}

trap cleanup EXIT

main() {
    echo "=================================================================="
    echo "           AGGRESSIVE dm-flakey ERROR SIMULATION"
    echo "=================================================================="
    
    mkdir -p "$TEST_DIR" && cd "$TEST_DIR"
    
    # Create small test devices
    dd if=/dev/zero of=main.img bs=1M count=$MAIN_SIZE_MB >/dev/null 2>&1
    dd if=/dev/zero of=spare.img bs=1M count=$SPARE_SIZE_MB >/dev/null 2>&1
    
    MAIN_LOOP=$(losetup -f --show main.img)
    SPARE_LOOP=$(losetup -f --show spare.img)
    
    print_info "Created devices: $MAIN_LOOP, $SPARE_LOOP"
    
    # Create VERY aggressive flakey device
    # Up for 1 second, down for 2 seconds, drop all writes during down period
    local main_sectors=$((MAIN_SIZE_MB * 2048))
    local flakey_table="0 $main_sectors flakey $MAIN_LOOP 0 10 20 1 drop_writes"
    
    print_info "Creating aggressive flakey device: $flakey_table"
    
    if echo "$flakey_table" | dmsetup create "$FLAKEY_NAME"; then
        print_success "Aggressive flakey device created"
    else
        print_info "Flakey creation failed, trying simpler config..."
        # Simpler config: very short up/down times
        flakey_table="0 $main_sectors flakey $MAIN_LOOP 0 5 15"
        echo "$flakey_table" | dmsetup create "$FLAKEY_NAME" || exit 1
    fi
    
    # Load dm-remap
    rmmod dm_remap 2>/dev/null || true
    insmod "$MODULE_PATH" auto_remap_enabled=1 error_threshold=1 debug_level=2 || exit 1
    
    # Create dm-remap on top of flakey device
    local spare_sectors=$((SPARE_SIZE_MB * 2048))
    local remap_table="0 $main_sectors remap /dev/mapper/$FLAKEY_NAME $SPARE_LOOP 0 $spare_sectors"
    
    if echo "$remap_table" | dmsetup create "$TARGET_NAME"; then
        print_success "dm-remap created on aggressive flakey device"
    else
        exit 1
    fi
    
    local target_device="/dev/mapper/$TARGET_NAME"
    
    # Get initial counters
    local initial_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    local initial_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    
    print_info "Initial counters: W=$initial_write_errors, R=$initial_read_errors"
    
    # Perform MANY I/O operations over time to catch flakey down periods
    print_info "Performing 60 I/O operations over 60 seconds to catch flakey failures..."
    
    local operations=0
    local failures=0
    
    for i in {1..60}; do
        operations=$((operations + 1))
        
        # Try both read and write
        if ! timeout 3s dd if=/dev/zero of="$target_device" bs=512 count=1 seek=$((i * 2)) >/dev/null 2>&1; then
            failures=$((failures + 1))
            print_info "Write operation $i failed (good!)"
        fi
        
        if ! timeout 3s dd if="$target_device" of=/dev/null bs=512 count=1 skip=$((i * 2)) >/dev/null 2>&1; then
            failures=$((failures + 1))
            print_info "Read operation $i failed (good!)"
        fi
        
        # Brief pause to let flakey device change states
        sleep 1
    done
    
    print_info "Operations: $operations, failures: $failures"
    
    # Check final counters
    local final_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
    local final_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")
    
    print_info "Final counters: W=$final_write_errors, R=$final_read_errors"
    
    local write_increase=$((final_write_errors - initial_write_errors))
    local read_increase=$((final_read_errors - initial_read_errors))
    
    if [ "$write_increase" -gt 0 ] || [ "$read_increase" -gt 0 ]; then
        print_success "SUCCESS! Real bio errors detected: W=+$write_increase, R=+$read_increase"
        print_success "dm-remap error detection IS working with aggressive flakey setup!"
    else
        print_info "No bio errors detected in counters despite $failures operation failures"
        print_info "This confirms the gap between operation failures and bio-level error detection"
    fi
    
    # Check target status
    local status=$(dmsetup status "$TARGET_NAME" 2>/dev/null || echo "")
    print_info "Final status: $status"
    
    echo "=================================================================="
    echo "AGGRESSIVE FLAKEY TEST SUMMARY:"
    echo "  Operations attempted: $operations"
    echo "  Operation failures: $failures"
    echo "  Bio error increases: W=+$write_increase, R=+$read_increase"
    echo "=================================================================="
}

main