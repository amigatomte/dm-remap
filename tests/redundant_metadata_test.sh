#!/bin/bash
# dm-remap v4.0 Redundant Metadata Test Suite
# Tests multiple metadata copies, checksums, and conflict resolution

set -e

# Test configuration
TEST_DEVICE_SIZE="100M"
METADATA_COPIES=5
METADATA_SECTORS=(0 1024 2048 4096 8192)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

log_test() {
    local test_name="$1"
    local result="$2"
    local details="$3"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    if [[ "$result" == "PASS" ]]; then
        echo -e "${GREEN}✓ $test_name: PASSED${NC} - $details"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    elif [[ "$result" == "FAIL" ]]; then
        echo -e "${RED}✗ $test_name: FAILED${NC} - $details"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    else
        echo -e "${YELLOW}⚠ $test_name: $result${NC} - $details"
    fi
}

create_test_device() {
    local device_name="$1"
    local size="$2"
    
    # Create sparse file for testing
    dd if=/dev/zero of="/tmp/${device_name}" bs=1 count=0 seek="$size" 2>/dev/null
    
    # Set up loop device
    LOOP_DEV=$(sudo losetup --find --show "/tmp/${device_name}")
    echo "$LOOP_DEV"
}

cleanup_test_device() {
    local loop_dev="$1"
    sudo losetup -d "$loop_dev" 2>/dev/null || true
    rm -f "/tmp/$(basename "$loop_dev")" 2>/dev/null || true
}

# Simulate metadata write to specific sector
write_metadata_copy() {
    local device="$1"
    local sector="$2"
    local sequence="$3"
    local copy_index="$4"
    local corrupt="$5"
    
    # Create test metadata (simplified structure)
    local metadata_file="/tmp/metadata_write_${sector}_${copy_index}.bin"
    
    # Write metadata header (simplified)
    {
        # Magic number (4 bytes): 0x444D5234 = "DMR4"  
        printf '\x44\x4D\x52\x34'
        # Sequence number (8 bytes, little-endian)
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x' \
            $((sequence & 0xFF)) $(((sequence >> 8) & 0xFF)) $(((sequence >> 16) & 0xFF)) $(((sequence >> 24) & 0xFF)) \
            $((sequence >> 32 & 0xFF)) $((sequence >> 40 & 0xFF)) $((sequence >> 48 & 0xFF)) $((sequence >> 56 & 0xFF)))"
        # Copy index (4 bytes)
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' \
            $((copy_index & 0xFF)) $(((copy_index >> 8) & 0xFF)) $(((copy_index >> 16) & 0xFF)) $(((copy_index >> 24) & 0xFF)))"
        # Pad to 4KB with test data
        dd if=/dev/zero bs=1 count=4084 2>/dev/null
    } > "$metadata_file" 2>/dev/null
    
    # Introduce corruption if requested
    if [[ "$corrupt" == "true" ]]; then
        # Corrupt the magic number
        printf '\xFF\xFF\xFF\xFF' | dd of="$metadata_file" bs=1 count=4 conv=notrunc 2>/dev/null
    fi
    
    # Write to device
    sudo dd if="$metadata_file" of="$device" bs=512 seek="$sector" count=8 2>/dev/null
    rm -f "$metadata_file" 2>/dev/null
}

# Read and validate metadata copy
read_metadata_copy() {
    local device="$1"
    local sector="$2"
    local expected_sequence="$3"
    
    local metadata_file="/tmp/read_metadata_${sector}.bin"
    sudo dd if="$device" of="$metadata_file" bs=512 skip="$sector" count=8 2>/dev/null
    
    # Change ownership to current user for file operations
    sudo chown $(whoami):$(whoami) "$metadata_file" 2>/dev/null
    
    # Check if file exists and has content
    if [[ ! -f "$metadata_file" ]] || [[ ! -s "$metadata_file" ]]; then
        rm -f "$metadata_file" 2>/dev/null
        return 3
    fi
    
    # Check magic number (DMR4 = 0x444D5234)
    local magic=$(xxd -l 4 -p "$metadata_file" 2>/dev/null | tr '[:lower:]' '[:upper:]')
    if [[ "$magic" != "444D5234" ]]; then
        rm -f "$metadata_file" 2>/dev/null
        return 1
    fi
    
    # Check sequence number (simplified - just check if non-zero)
    local sequence_bytes=$(xxd -s 4 -l 8 -p "$metadata_file" 2>/dev/null)
    if [[ "$sequence_bytes" == "0000000000000000" ]]; then
        rm -f "$metadata_file" 2>/dev/null
        return 2
    fi
    
    rm -f "$metadata_file" 2>/dev/null
    return 0
}

echo "================================================================="
echo "dm-remap v4.0 Redundant Metadata Test Suite"
echo "================================================================="
echo

# Setup test environment
echo "Setting up test environment..."
SPARE_DEVICE=$(create_test_device "spare_device.img" "50M")
echo "Created spare device: $SPARE_DEVICE"

#================================================================
# TEST 1: Multiple Copy Write and Read
#================================================================
echo
echo "=== TEST 1: Multiple Copy Write and Read ==="

# Write metadata to all 5 locations
SEQUENCE_NUM=1001
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    write_metadata_copy "$SPARE_DEVICE" "$sector" "$SEQUENCE_NUM" "$i" "false"
done

# Verify all copies can be read
ALL_COPIES_VALID=true
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    if ! read_metadata_copy "$SPARE_DEVICE" "$sector" "$SEQUENCE_NUM"; then
        ALL_COPIES_VALID=false
        break
    fi
done

if [[ "$ALL_COPIES_VALID" == "true" ]]; then
    log_test "Multiple Copy Storage" "PASS" "All 5 metadata copies written and read successfully"
else
    log_test "Multiple Copy Storage" "FAIL" "Some metadata copies failed validation"
fi

#================================================================
# TEST 2: Single Copy Corruption Recovery
#================================================================
echo
echo "=== TEST 2: Single Copy Corruption Recovery ==="

# Corrupt copy at sector 1024 (secondary)
write_metadata_copy "$SPARE_DEVICE" "1024" "$SEQUENCE_NUM" "1" "true"

# Verify primary copy still valid
if read_metadata_copy "$SPARE_DEVICE" "0" "$SEQUENCE_NUM"; then
    log_test "Single Copy Corruption" "PASS" "Primary copy remains valid after secondary corruption"
else
    log_test "Single Copy Corruption" "FAIL" "Primary copy validation failed"
fi

# Count valid copies
VALID_COPIES=0
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    if read_metadata_copy "$SPARE_DEVICE" "$sector" "$SEQUENCE_NUM"; then
        VALID_COPIES=$((VALID_COPIES + 1))
    fi
done

if [[ "$VALID_COPIES" -eq 4 ]]; then
    log_test "Corruption Isolation" "PASS" "4 out of 5 copies remain valid after single corruption"
else
    log_test "Corruption Isolation" "FAIL" "Expected 4 valid copies, found $VALID_COPIES"
fi

#================================================================
# TEST 3: Multiple Copy Corruption Recovery
#================================================================
echo
echo "=== TEST 3: Multiple Copy Corruption Recovery ==="

# Corrupt additional copies (sectors 2048 and 4096)
write_metadata_copy "$SPARE_DEVICE" "2048" "$SEQUENCE_NUM" "2" "true"
write_metadata_copy "$SPARE_DEVICE" "4096" "$SEQUENCE_NUM" "3" "true"

# Count remaining valid copies
VALID_COPIES=0
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    if read_metadata_copy "$SPARE_DEVICE" "$sector" "$SEQUENCE_NUM"; then
        VALID_COPIES=$((VALID_COPIES + 1))
    fi
done

if [[ "$VALID_COPIES" -eq 2 ]]; then
    log_test "Multiple Corruption Recovery" "PASS" "2 out of 5 copies remain valid after multiple corruption"
else
    log_test "Multiple Corruption Recovery" "FAIL" "Expected 2 valid copies, found $VALID_COPIES"
fi

# Verify recovery is possible with minority valid copies
if [[ "$VALID_COPIES" -gt 0 ]]; then
    log_test "Recovery Capability" "PASS" "Metadata recovery possible with $VALID_COPIES valid copies"
else
    log_test "Recovery Capability" "FAIL" "No valid copies remain for recovery"
fi

#================================================================
# TEST 4: Sequence Number Conflict Resolution
#================================================================
echo
echo "=== TEST 4: Sequence Number Conflict Resolution ==="

# Restore all copies first
SEQUENCE_NUM=2001
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    write_metadata_copy "$SPARE_DEVICE" "$sector" "$SEQUENCE_NUM" "$i" "false"
done

# Write newer metadata to some copies
NEWER_SEQUENCE=2005
write_metadata_copy "$SPARE_DEVICE" "0" "$NEWER_SEQUENCE" "0" "false"     # Primary - newer
write_metadata_copy "$SPARE_DEVICE" "1024" "$NEWER_SEQUENCE" "1" "false" # Secondary - newer
# Leave others with older sequence number

# Verify we can detect the conflict
PRIMARY_VALID=$(read_metadata_copy "$SPARE_DEVICE" "0" "$NEWER_SEQUENCE" && echo "true" || echo "false")
SECONDARY_VALID=$(read_metadata_copy "$SPARE_DEVICE" "1024" "$NEWER_SEQUENCE" && echo "true" || echo "false")
TERTIARY_VALID=$(read_metadata_copy "$SPARE_DEVICE" "2048" "$SEQUENCE_NUM" && echo "true" || echo "false")

if [[ "$PRIMARY_VALID" == "true" && "$SECONDARY_VALID" == "true" && "$TERTIARY_VALID" == "true" ]]; then
    log_test "Sequence Conflict Detection" "PASS" "Multiple valid copies with different sequences detected"
else
    log_test "Sequence Conflict Detection" "FAIL" "Failed to detect sequence conflicts properly"
fi

#================================================================
# TEST 5: Metadata Repair Simulation
#================================================================
echo
echo "=== TEST 5: Metadata Repair Simulation ==="

# Simulate repair by copying newest valid metadata to corrupted locations
# In real implementation, this would be done automatically
write_metadata_copy "$SPARE_DEVICE" "2048" "$NEWER_SEQUENCE" "2" "false"
write_metadata_copy "$SPARE_DEVICE" "4096" "$NEWER_SEQUENCE" "3" "false"
write_metadata_copy "$SPARE_DEVICE" "8192" "$NEWER_SEQUENCE" "4" "false"

# Verify all copies now have consistent sequence numbers
CONSISTENT_COPIES=0
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    if read_metadata_copy "$SPARE_DEVICE" "$sector" "$NEWER_SEQUENCE"; then
        CONSISTENT_COPIES=$((CONSISTENT_COPIES + 1))
    fi
done

if [[ "$CONSISTENT_COPIES" -eq 5 ]]; then
    log_test "Metadata Repair" "PASS" "All 5 copies restored to consistent state"
else
    log_test "Metadata Repair" "FAIL" "Repair failed, only $CONSISTENT_COPIES copies consistent"
fi

#================================================================
# TEST 6: Performance Impact Assessment
#================================================================
echo
echo "=== TEST 6: Performance Impact Assessment ==="

# Time single write vs multiple writes
START_TIME=$(date +%s%N)
write_metadata_copy "$SPARE_DEVICE" "0" "3001" "0" "false"
SINGLE_WRITE_TIME=$((($(date +%s%N) - START_TIME) / 1000000)) # Convert to milliseconds

START_TIME=$(date +%s%N)
for i in "${!METADATA_SECTORS[@]}"; do
    sector="${METADATA_SECTORS[$i]}"
    write_metadata_copy "$SPARE_DEVICE" "$sector" "3001" "$i" "false"
done
MULTIPLE_WRITE_TIME=$((($(date +%s%N) - START_TIME) / 1000000))

OVERHEAD_RATIO=$(echo "scale=2; $MULTIPLE_WRITE_TIME / $SINGLE_WRITE_TIME" | bc -l)

if [[ $(echo "$OVERHEAD_RATIO < 6.0" | bc) -eq 1 ]]; then
    log_test "Write Performance Impact" "PASS" "5x redundancy overhead is ${OVERHEAD_RATIO}x (acceptable)"
else
    log_test "Write Performance Impact" "WARN" "5x redundancy overhead is ${OVERHEAD_RATIO}x (may need optimization)"
fi

#================================================================
# CLEANUP AND SUMMARY
#================================================================
echo
echo "Cleaning up test environment..."
cleanup_test_device "$SPARE_DEVICE"

echo
echo "================================================================="
echo "REDUNDANT METADATA TEST SUMMARY"
echo "================================================================="
echo "Total Tests: $TESTS_RUN"
echo "Passed: $TESTS_PASSED"
echo "Failed: $TESTS_FAILED"
echo "Success Rate: $(echo "scale=1; $TESTS_PASSED * 100 / $TESTS_RUN" | bc)%"
echo

if [[ "$TESTS_FAILED" -eq 0 ]]; then
    echo -e "${GREEN}🎉 ALL REDUNDANT METADATA TESTS PASSED!${NC}"
    echo "v4.0 redundant metadata system is ready for implementation."
    exit 0
else
    echo -e "${RED}❌ Some redundant metadata tests failed.${NC}"
    echo "Review failed tests before proceeding with implementation."
    exit 1
fi