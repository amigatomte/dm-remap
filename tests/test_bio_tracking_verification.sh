#!/bin/bash

# test_bio_tracking_verification.sh - Verify if bio tracking is actually working
# This test checks if dmr_bio_endio is being called at all

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-bio-verification"
TARGET_NAME="bio-verify-test"
MAIN_SIZE_MB=10
SPARE_SIZE_MB=5

print_info() { echo -e "\033[0;36mℹ INFO:\033[0m $1"; }
print_success() { echo -e "\033[0;32m✓ SUCCESS:\033[0m $1"; }
print_error() { echo -e "\033[0;31m✗ ERROR:\033[0m $1"; }
print_warning() { echo -e "\033[0;33m⚠ WARNING:\033[0m $1"; }

cleanup() {
    dmsetup remove "$TARGET_NAME" 2>/dev/null || true
    [ -n "${MAIN_LOOP:-}" ] && losetup -d "$MAIN_LOOP" 2>/dev/null || true
    [ -n "${SPARE_LOOP:-}" ] && losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rmmod dm_remap 2>/dev/null || true
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

echo "=================================================================="
echo "           BIO TRACKING VERIFICATION TEST"
echo "=================================================================="
echo "Purpose: Verify if dmr_bio_endio callback is actually being called"
echo "Date: $(date)"
echo

# Setup
mkdir -p "$TEST_DIR" && cd "$TEST_DIR"
dd if=/dev/zero of=main.img bs=1M count=$MAIN_SIZE_MB >/dev/null 2>&1
dd if=/dev/zero of=spare.img bs=1M count=$SPARE_SIZE_MB >/dev/null 2>&1

MAIN_LOOP=$(losetup -f --show main.img)
SPARE_LOOP=$(losetup -f --show spare.img)

print_info "Created devices: $MAIN_LOOP, $SPARE_LOOP"

# Load module with maximum debug output
rmmod dm_remap 2>/dev/null || true
if ! insmod "$MODULE_PATH" debug_level=3 auto_remap_enabled=1; then
    print_error "Failed to load dm-remap module"
    exit 1
fi

# Create target
main_sectors=$((MAIN_SIZE_MB * 2048))
spare_sectors=$((SPARE_SIZE_MB * 2048))
table_line="0 $main_sectors remap $MAIN_LOOP $SPARE_LOOP 0 $spare_sectors"

if echo "$table_line" | dmsetup create "$TARGET_NAME"; then
    print_success "Target created: /dev/mapper/$TARGET_NAME"
else
    print_error "Failed to create target"
    exit 1
fi

target_device="/dev/mapper/$TARGET_NAME"

print_info "Clearing kernel log to isolate our test messages..."
sudo dmesg -c >/dev/null

print_info "Testing WRITE operations (should have bio tracking)..."

# Perform write operations
for i in {1..3}; do
    print_info "Write operation $i..."
    dd if=/dev/zero of="$target_device" bs=4096 count=1 seek=$((100 + i)) >/dev/null 2>&1
    sleep 1
done

print_info "Testing READ operations (currently DISABLED for bio tracking)..."

# Perform read operations  
for i in {1..3}; do
    print_info "Read operation $i..."
    dd if="$target_device" of=/dev/null bs=4096 count=1 skip=$((100 + i)) >/dev/null 2>&1
    sleep 1
done

# Check kernel log for debug messages
print_info "Checking kernel log for bio tracking debug messages..."

bio_tracking_messages=$(sudo dmesg | grep -E "(Setup bio tracking|Skipping bio tracking|dmr_bio_endio)" || true)

echo
echo "BIO TRACKING DEBUG MESSAGES:"
echo "============================"
if [ -n "$bio_tracking_messages" ]; then
    echo "$bio_tracking_messages"
else
    print_warning "No bio tracking debug messages found"
fi

echo
echo "ANALYSIS:"
echo "========="

# Check if bio tracking setup messages appear
if echo "$bio_tracking_messages" | grep -q "Setup bio tracking"; then
    print_success "Bio tracking setup is being called"
else
    print_warning "No 'Setup bio tracking' messages found"
fi

# Check if read operations are being skipped
if echo "$bio_tracking_messages" | grep -q "Skipping bio tracking for read"; then
    print_error "READ OPERATIONS ARE BEING SKIPPED FOR BIO TRACKING!"
    print_error "This explains why we don't detect read errors!"
else
    print_info "No read skipping messages (may be good or debug_level too low)"
fi

# Check if bio completion callback is being called
if echo "$bio_tracking_messages" | grep -q "dmr_bio_endio"; then
    print_success "Bio completion callback is being called"
else
    print_warning "No bio completion callback messages found"
fi

# Check error counters
initial_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors 2>/dev/null || echo "0")
initial_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors 2>/dev/null || echo "0")

print_info "Error counters after test: Write=$initial_write_errors, Read=$initial_read_errors"

echo
echo "=================================================================="
echo "                    VERIFICATION SUMMARY"
echo "=================================================================="
echo "This test reveals why our error detection isn't working:"
echo
if echo "$bio_tracking_messages" | grep -q "Skipping bio tracking for read"; then
    print_error "CRITICAL ISSUE FOUND:"
    echo "  • Read operations are DISABLED for bio tracking"
    echo "  • This is a temporary debug setting in the code"
    echo "  • READ ERRORS CANNOT BE DETECTED with this setting"
    echo
    echo "Location: dm_remap_io.c, dmr_setup_bio_tracking() function"
    echo "Lines 225-228: Temporary debug code skips read tracking"
    echo
    print_warning "Fix required: Remove the temporary debug code that skips read operations"
else
    print_info "Read operation tracking appears to be enabled"
fi

echo "=================================================================="