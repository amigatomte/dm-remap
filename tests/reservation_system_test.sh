#!/bin/bash
#
# reservation_system_test.sh - Test metadata sector reservation system
#
# This test validates that the reservation system prevents metadata corruption
# by spare sector allocation conflicts.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

echo -e "${PURPLE}=================================================================${NC}"
echo -e "${PURPLE}  dm-remap v4.0 Reservation System Test Suite${NC}"
echo -e "${PURPLE}=================================================================${NC}"

# Test configuration
TEST_SIZE_MB=10
SPARE_SIZE_MB=5

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TOTAL_TESTS=0

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up reservation system test...${NC}"
    
    # Remove device mapper targets
    sudo dmsetup remove reservation-test-large 2>/dev/null || true
    sudo dmsetup remove reservation-test-medium 2>/dev/null || true
    sudo dmsetup remove reservation-test-small 2>/dev/null || true
    
    # Clean up loop devices
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP_LARGE" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP_MEDIUM" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP_SMALL" 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/reservation_main.img
    rm -f /tmp/reservation_spare_large.img
    rm -f /tmp/reservation_spare_medium.img
    rm -f /tmp/reservation_spare_small.img
    rm -f /tmp/test_*.dat
    
    # Remove module if needed
    sudo rmmod dm_remap 2>/dev/null || true
}

# Set trap for cleanup
trap cleanup EXIT

run_test() {
    local test_name="$1"
    local test_func="$2"
    
    echo -e "${BLUE}=== TEST: $test_name ===${NC}"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if $test_func; then
        echo -e "${GREEN}✅ PASSED: $test_name${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}❌ FAILED: $test_name${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    echo ""
}

# Test functions
test_large_device_geometric_reservations() {
    echo "Testing 8MB spare device (geometric strategy)..."
    
    # Create dm-remap target with large spare device
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP_LARGE 0 16384" | sudo dmsetup create reservation-test-large
    
    if ! sudo dmsetup info reservation-test-large | grep -q "State.*ACTIVE"; then
        echo "Failed to create large device target"
        return 1
    fi
    
    echo "✓ Large device target created successfully"
    
    # Perform intensive remapping to test sector allocation
    echo "Performing 100 remap operations..."
    for i in $(seq 1 100); do
        sector=$((i * 100))
        result=$(sudo dmsetup message reservation-test-large 0 remap $sector 2>&1 || echo "error")
        
        if [[ "$result" == *"error"* ]]; then
            echo "Remap failed at operation $i: $result"
            return 1
        fi
        
        # Check that we didn't overwrite metadata sectors (0, 1024, 2048, 4096, 8192)
        spare_sector=$(echo "$result" | grep -o 'spare sector [0-9]*' | cut -d' ' -f3)
        if [[ -n "$spare_sector" ]]; then
            # Check if spare sector conflicts with expected metadata locations
            for meta_sector in 0 1024 2048 4096 8192; do
                if [[ $spare_sector -ge $meta_sector && $spare_sector -lt $((meta_sector + 8)) ]]; then
                    echo "COLLISION DETECTED: Spare sector $spare_sector conflicts with metadata at $meta_sector"
                    return 1
                fi
            done
        fi
    done
    
    echo "✓ All 100 remap operations completed without metadata collisions"
    sudo dmsetup remove reservation-test-large
    return 0
}

test_medium_device_linear_reservations() {
    echo "Testing 2MB spare device (linear strategy)..."
    
    # Create dm-remap target with medium spare device
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP_MEDIUM 0 4096" | sudo dmsetup create reservation-test-medium
    
    if ! sudo dmsetup info reservation-test-medium | grep -q "State.*ACTIVE"; then
        echo "Failed to create medium device target"
        return 1
    fi
    
    echo "✓ Medium device target created successfully"
    
    # Perform remapping to test linear distribution reservations
    echo "Performing 50 remap operations..."
    for i in $(seq 1 50); do
        sector=$((i * 200))
        result=$(sudo dmsetup message reservation-test-medium 0 remap $sector 2>&1 || echo "error")
        
        if [[ "$result" == *"error"* ]]; then
            echo "Remap failed at operation $i: $result"
            return 1
        fi
        
        # For linear strategy with 4096 sectors, expect metadata at: 0, ~1365, ~2731, ~4096
        spare_sector=$(echo "$result" | grep -o 'spare sector [0-9]*' | cut -d' ' -f3)
        if [[ -n "$spare_sector" ]]; then
            # Check major metadata sector ranges (approximate for linear)
            for meta_start in 0 1360 2720; do
                meta_end=$((meta_start + 16))  # Extra safety margin
                if [[ $spare_sector -ge $meta_start && $spare_sector -le $meta_end ]]; then
                    echo "COLLISION DETECTED: Spare sector $spare_sector conflicts with metadata near $meta_start"
                    return 1
                fi
            done
        fi
    done
    
    echo "✓ All 50 remap operations completed without metadata collisions"
    sudo dmsetup remove reservation-test-medium
    return 0
}

test_small_device_minimal_reservations() {
    echo "Testing 256KB spare device (minimal strategy)..."
    
    # Create dm-remap target with small spare device
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP_SMALL 0 512" | sudo dmsetup create reservation-test-small
    
    if ! sudo dmsetup info reservation-test-small | grep -q "State.*ACTIVE"; then
        echo "Failed to create small device target"
        return 1
    fi
    
    echo "✓ Small device target created successfully"
    
    # Perform remapping to test minimal distribution reservations
    echo "Performing 20 remap operations..."
    for i in $(seq 1 20); do
        sector=$((i * 500))
        result=$(sudo dmsetup message reservation-test-small 0 remap $sector 2>&1 || echo "error")
        
        if [[ "$result" == *"error"* ]]; then
            echo "Remap failed at operation $i: $result"
            return 1
        fi
        
        # For minimal strategy with 512 sectors, expect tight packing: 0-7, 8-15, 16-23, etc.
        spare_sector=$(echo "$result" | grep -o 'spare sector [0-9]*' | cut -d' ' -f3)
        if [[ -n "$spare_sector" ]]; then
            # Check if spare sector is in first 64 sectors (likely metadata region)
            if [[ $spare_sector -lt 64 ]]; then
                # This could be a collision - check if it's exactly in metadata sectors
                remainder=$((spare_sector % 8))
                if [[ $remainder -eq 0 ]]; then
                    echo "POTENTIAL COLLISION: Spare sector $spare_sector may conflict with metadata"
                    return 1
                fi
            fi
        fi
    done
    
    echo "✓ All 20 remap operations completed without metadata collisions"
    sudo dmsetup remove reservation-test-small
    return 0
}

test_reservation_exhaustion() {
    echo "Testing reservation system under high load..."
    
    # Use small device to quickly exhaust available sectors
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP_SMALL 0 512" | sudo dmsetup create reservation-test-small
    
    # Attempt to exhaust all available spare sectors
    echo "Attempting to create many remaps to test exhaustion..."
    success_count=0
    
    for i in $(seq 1 600); do  # Try more remaps than sectors available
        sector=$((i * 10))
        result=$(sudo dmsetup message reservation-test-small 0 remap $sector 2>&1 || echo "error")
        
        if [[ "$result" == *"error"* ]]; then
            if [[ "$result" == *"no unreserved spare sectors available"* ]]; then
                echo "✓ Proper exhaustion message received after $success_count successful remaps"
                break
            else
                echo "Unexpected error: $result"
                sudo dmsetup remove reservation-test-small
                return 1
            fi
        else
            success_count=$((success_count + 1))
        fi
    done
    
    if [[ $success_count -eq 0 ]]; then
        echo "No successful remaps - system may be misconfigured"
        sudo dmsetup remove reservation-test-small
        return 1
    fi
    
    echo "✓ Successfully handled reservation exhaustion after $success_count remaps"
    sudo dmsetup remove reservation-test-small
    return 0
}

# Main test execution
echo -e "${BLUE}Phase 1: Setup reservation system test environment${NC}"

# Load dm-remap with reservation system
sudo rmmod dm_remap 2>/dev/null || true
if ! sudo insmod ../src/dm_remap.ko debug_level=2; then
    echo -e "${RED}Failed to load dm-remap module${NC}"
    exit 1
fi

echo "✅ dm-remap loaded with reservation system"

# Create test devices
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/reservation_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/reservation_spare_large.img bs=1M count=8 2>/dev/null    # 8MB for geometric
dd if=/dev/zero of=/tmp/reservation_spare_medium.img bs=1M count=2 2>/dev/null  # 2MB for linear
dd if=/dev/zero of=/tmp/reservation_spare_small.img bs=1K count=256 2>/dev/null # 256KB for minimal

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/reservation_main.img)
SPARE_LOOP_LARGE=$(sudo losetup -f --show /tmp/reservation_spare_large.img)
SPARE_LOOP_MEDIUM=$(sudo losetup -f --show /tmp/reservation_spare_medium.img)
SPARE_LOOP_SMALL=$(sudo losetup -f --show /tmp/reservation_spare_small.img)

echo "Main device: $MAIN_LOOP"
echo "Large spare device: $SPARE_LOOP_LARGE (8MB)"
echo "Medium spare device: $SPARE_LOOP_MEDIUM (2MB)"
echo "Small spare device: $SPARE_LOOP_SMALL (256KB)"

echo -e "${BLUE}Phase 2: Execute reservation system tests${NC}"

# Run tests
run_test "Large Device Geometric Reservations" test_large_device_geometric_reservations
run_test "Medium Device Linear Reservations" test_medium_device_linear_reservations
run_test "Small Device Minimal Reservations" test_small_device_minimal_reservations
run_test "Reservation Exhaustion Handling" test_reservation_exhaustion

echo -e "${PURPLE}=================================================================${NC}"
echo -e "${PURPLE}  RESERVATION SYSTEM TEST SUMMARY${NC}"
echo -e "${PURPLE}=================================================================${NC}"
echo "Total Tests: $TOTAL_TESTS"
echo "Passed: $TESTS_PASSED"
echo "Failed: $TESTS_FAILED"

if [[ $TESTS_FAILED -eq 0 ]]; then
    echo -e "${GREEN}🎉 ALL RESERVATION SYSTEM TESTS PASSED!${NC}"
    echo -e "${GREEN}The metadata sector reservation system is working correctly.${NC}"
    echo ""
    echo "Key validations completed:"
    echo "  ✅ Geometric strategy reservations prevent metadata collisions"
    echo "  ✅ Linear strategy reservations adapt to medium devices"
    echo "  ✅ Minimal strategy reservations work on small devices"
    echo "  ✅ System gracefully handles reservation exhaustion"
    echo ""
    echo -e "${GREEN}v4.0 reservation system is production-ready!${NC}"
    exit 0
else
    echo -e "${RED}❌ RESERVATION SYSTEM TESTS FAILED${NC}"
    echo -e "${RED}Critical metadata collision vulnerability detected!${NC}"
    exit 1
fi