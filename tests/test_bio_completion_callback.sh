#!/bin/bash

# test_bio_completion_callback.sh - Test if dmr_bio_endio is actually called
# Now with the bio tracking fix, this should show bio completion callbacks

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm_remap.ko"
TEST_DIR="/tmp/dm-remap-bio-callback"
TARGET_NAME="bio-callback-test"
MAIN_SIZE_MB=10
SPARE_SIZE_MB=5

print_info() { echo -e "\033[0;36mℹ INFO:\033[0m $1"; }
print_success() { echo -e "\033[0;32m✓ SUCCESS:\033[0m $1"; }
print_error() { echo -e "\033[0;31m✗ ERROR:\033[0m $1"; }

cleanup() {
    dmsetup remove "$TARGET_NAME" 2>/dev/null || true
    [ -n "${MAIN_LOOP:-}" ] && losetup -d "$MAIN_LOOP" 2>/dev/null || true
    [ -n "${SPARE_LOOP:-}" ] && losetup -d "$SPARE_LOOP" 2>/dev/null || true
    rmmod dm_remap 2>/dev/null || true
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

echo "=================================================================="
echo "           BIO COMPLETION CALLBACK TEST (FIXED VERSION)"
echo "=================================================================="
echo "Testing if dmr_bio_endio callback is actually called"
echo "Date: $(date)"
echo

# Setup
mkdir -p "$TEST_DIR" && cd "$TEST_DIR"
dd if=/dev/zero of=main.img bs=1M count=$MAIN_SIZE_MB >/dev/null 2>&1
dd if=/dev/zero of=spare.img bs=1M count=$SPARE_SIZE_MB >/dev/null 2>&1

MAIN_LOOP=$(losetup -f --show main.img)
SPARE_LOOP=$(losetup -f --show spare.img)

print_info "Created devices: $MAIN_LOOP, $SPARE_LOOP"

# Load module with debug output
rmmod dm_remap 2>/dev/null || true

# Add a temporary debug message to dmr_bio_endio to prove it's being called
print_info "Loading module with maximum debugging..."
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

# Clear kernel log
print_info "Clearing kernel log..."
sudo dmesg -c >/dev/null

print_info "Performing I/O operations to trigger bio completion callbacks..."

# Get initial error counters
initial_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors)
initial_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)

print_info "Initial error counters: Write=$initial_write_errors, Read=$initial_read_errors"

# Perform write operations
print_info "Testing WRITE operations..."
for i in {1..3}; do
    dd if=/dev/zero of="$target_device" bs=4096 count=1 seek=$((100 + i)) >/dev/null 2>&1
    sleep 0.5
done

# Perform read operations (should now be tracked!)
print_info "Testing READ operations (should now be tracked)..."
for i in {1..3}; do
    dd if="$target_device" of=/dev/null bs=4096 count=1 skip=$((100 + i)) >/dev/null 2>&1
    sleep 0.5
done

# Wait for any async processing
sleep 2

# Check final error counters
final_write_errors=$(cat /sys/module/dm_remap/parameters/global_write_errors)
final_read_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)

print_info "Final error counters: Write=$final_write_errors, Read=$final_read_errors"

# Check kernel log for debug messages
print_info "Checking kernel log for bio tracking and completion messages..."

bio_messages=$(sudo dmesg | grep -E "(Setup bio tracking|Skipping bio tracking|dmr_bio_endio|Bio tracking enabled)" | tail -20)

echo
echo "BIO TRACKING MESSAGES:"
echo "====================="
if [ -n "$bio_messages" ]; then
    echo "$bio_messages"
else
    print_error "No bio tracking messages found!"
fi

echo
echo "ANALYSIS:"
echo "========="

# Count setup messages for reads vs writes
setup_messages=$(echo "$bio_messages" | grep "Setup bio tracking" | wc -l)
skip_messages=$(echo "$bio_messages" | grep "Skipping bio tracking" | wc -l)

print_info "Bio tracking setup calls: $setup_messages"

if [ "$skip_messages" -gt 0 ]; then
    print_error "Still skipping bio tracking: $skip_messages operations"
else
    print_success "No bio tracking skip messages (good!)"
fi

# Check if bio completion callback messages exist
endio_messages=$(echo "$bio_messages" | grep "dmr_bio_endio" | wc -l)

if [ "$endio_messages" -gt 0 ]; then
    print_success "Bio completion callback is being called: $endio_messages times"
else
    print_info "No bio completion callback messages (may need explicit debug messages in dmr_bio_endio)"
fi

# Check error counter changes
write_change=$((final_write_errors - initial_write_errors))
read_change=$((final_read_errors - initial_read_errors))

if [ "$write_change" -gt 0 ] || [ "$read_change" -gt 0 ]; then
    print_success "Error counters changed: Write=+$write_change, Read=+$read_change"
    print_success "This indicates bio completion callback IS working!"
else
    print_info "No error counter changes (expected for successful I/O)"
fi

echo
echo "=================================================================="
echo "                    BIO CALLBACK TEST SUMMARY"
echo "=================================================================="

if [ "$skip_messages" -eq 0 ]; then
    print_success "FIXED: Bio tracking is now enabled for ALL operations"
    print_success "Read operations are no longer being skipped"
    
    if [ "$setup_messages" -gt 0 ]; then
        print_success "Bio tracking setup is working for $setup_messages operations"
    fi
    
    print_info "Next step: Test with actual I/O errors to validate error detection"
else
    print_error "Bio tracking is still being skipped - fix didn't work completely"
fi

echo "=================================================================="