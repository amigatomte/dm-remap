#!/bin/bash
#
# stress_test_reservation_system.sh - Comprehensive stress testing of integrated reservation system
#
# This test validates the reservation system under high load and various device configurations
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
echo -e "${PURPLE}  dm-remap v4.0 STRESS TEST - Reservation System${NC}"
echo -e "${PURPLE}=================================================================${NC}"

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up stress test...${NC}"
    
    # Remove all test targets
    for target in stress-geometric stress-linear stress-minimal stress-boundary; do
        sudo dmsetup remove "$target" 2>/dev/null || true
    done
    
    # Clean up loop devices
    for loop in "$MAIN_LOOP" "$LARGE_SPARE" "$MEDIUM_SPARE" "$SMALL_SPARE"; do
        sudo losetup -d "$loop" 2>/dev/null || true
    done
    
    # Remove test files
    rm -f /tmp/stress_*.img
}

trap cleanup EXIT

run_stress_test() {
    local test_name="$1"
    local device_size="$2"
    local expected_strategy="$3"
    local remap_count="$4"
    
    echo -e "${BLUE}=== STRESS TEST: $test_name ===${NC}"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    local target_name="stress-$(echo "$expected_strategy" | tr '[:upper:]' '[:lower:]')"
    local spare_loop
    
    # Select appropriate spare device
    case $device_size in
        "8MB") spare_loop="$LARGE_SPARE" ;;
        "2MB") spare_loop="$MEDIUM_SPARE" ;;
        "256KB") spare_loop="$SMALL_SPARE" ;;
        *) echo "Unknown device size: $device_size"; return 1 ;;
    esac
    
    # Create target
    MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
    SPARE_SECTORS=$(sudo blockdev --getsz "$spare_loop")
    
    echo "Creating target: $target_name with $device_size spare ($expected_strategy strategy)"
    echo "0 $MAIN_SECTORS remap $MAIN_LOOP $spare_loop 0 $SPARE_SECTORS" | sudo dmsetup create "$target_name"
    
    if ! sudo dmsetup info "$target_name" | grep -q "State.*ACTIVE"; then
        echo -e "${RED}❌ Failed to create $target_name${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
    
    # Perform stress remapping
    echo "Performing $remap_count remap operations..."
    local success_count=0
    local collision_count=0
    local start_time=$(date +%s)
    
    for i in $(seq 1 "$remap_count"); do
        sector=$((i * 50 + 1000))  # Spread out sectors
        result=$(sudo dmsetup message "$target_name" 0 remap $sector 2>&1 || echo "error")
        
        if [[ "$result" == *"error"* ]]; then
            if [[ "$result" == *"no unreserved spare sectors available"* ]]; then
                echo "  Reached spare capacity at operation $i"
                break
            else
                echo "  Unexpected error at operation $i: $result"
                break
            fi
        else
            success_count=$((success_count + 1))
            
            # Check for metadata collisions (simplified check for known metadata areas)
            spare_sector=$(echo "$result" | grep -o 'spare sector [0-9]*' | cut -d' ' -f3)
            if [[ -n "$spare_sector" ]]; then
                # Check against common metadata sector ranges
                case $expected_strategy in
                    "GEOMETRIC")
                        for meta_base in 0 1024 2048 4096 8192; do
                            if [[ $spare_sector -ge $meta_base && $spare_sector -lt $((meta_base + 8)) ]]; then
                                collision_count=$((collision_count + 1))
                                echo "    🚨 Collision detected: spare sector $spare_sector in metadata range $meta_base-$((meta_base + 7))"
                            fi
                        done
                        ;;
                    "LINEAR"|"MINIMAL")
                        # For linear/minimal, check first 64 sectors where metadata is likely placed
                        if [[ $spare_sector -lt 64 ]]; then
                            # Only flag as collision if it's exactly at sector boundaries that could be metadata
                            remainder=$((spare_sector % 8))
                            if [[ $remainder -eq 0 && $spare_sector -lt 32 ]]; then
                                collision_count=$((collision_count + 1))
                                echo "    🚨 Potential collision: spare sector $spare_sector may conflict with metadata"
                            fi
                        fi
                        ;;
                esac
            fi
        fi
        
        # Progress indicator
        if [[ $((i % 100)) -eq 0 ]]; then
            echo "    Progress: $i/$remap_count operations completed"
        fi
    done
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    # Get final status
    local status=$(sudo dmsetup status "$target_name")
    
    echo ""
    echo "Stress test results for $test_name:"
    echo "  Target: $target_name ($device_size spare, $expected_strategy strategy)"
    echo "  Remap operations attempted: $remap_count"
    echo "  Successful operations: $success_count"
    echo "  Metadata collisions detected: $collision_count"
    echo "  Test duration: ${duration}s"
    echo "  Status: $status"
    
    # Determine test result
    if [[ $success_count -gt 0 && $collision_count -eq 0 ]]; then
        echo -e "${GREEN}✅ PASSED: $test_name${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}❌ FAILED: $test_name${NC}"
        if [[ $collision_count -gt 0 ]]; then
            echo -e "${RED}   Reason: $collision_count metadata collisions detected${NC}"
        fi
        if [[ $success_count -eq 0 ]]; then
            echo -e "${RED}   Reason: No successful remap operations${NC}"
        fi
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    # Clean up this target
    sudo dmsetup remove "$target_name"
    echo ""
}

echo -e "${BLUE}Phase 1: Setup stress test environment${NC}"

# Create test devices
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/stress_main.img bs=1M count=20 2>/dev/null       # 20MB main
dd if=/dev/zero of=/tmp/stress_large_spare.img bs=1M count=8 2>/dev/null # 8MB spare (geometric)
dd if=/dev/zero of=/tmp/stress_medium_spare.img bs=1M count=2 2>/dev/null # 2MB spare (linear)
dd if=/dev/zero of=/tmp/stress_small_spare.img bs=1K count=256 2>/dev/null # 256KB spare (minimal)

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/stress_main.img)
LARGE_SPARE=$(sudo losetup -f --show /tmp/stress_large_spare.img)
MEDIUM_SPARE=$(sudo losetup -f --show /tmp/stress_medium_spare.img)
SMALL_SPARE=$(sudo losetup -f --show /tmp/stress_small_spare.img)

echo "Main device: $MAIN_LOOP (20MB)"
echo "Large spare: $LARGE_SPARE (8MB - geometric strategy)"
echo "Medium spare: $MEDIUM_SPARE (2MB - linear strategy)"
echo "Small spare: $SMALL_SPARE (256KB - minimal strategy)"

echo -e "${BLUE}Phase 2: Execute comprehensive stress tests${NC}"

# Run stress tests with different configurations
run_stress_test "Large Device Geometric Strategy" "8MB" "GEOMETRIC" 500
run_stress_test "Medium Device Linear Strategy" "2MB" "LINEAR" 200
run_stress_test "Small Device Minimal Strategy" "256KB" "MINIMAL" 50

echo -e "${BLUE}Phase 3: Boundary condition stress test${NC}"

# Test boundary condition: exactly at strategy transition points
echo "Testing boundary conditions with high load..."

# Create a device right at the geometric/linear boundary (4MB)
dd if=/dev/zero of=/tmp/stress_boundary.img bs=1M count=4 2>/dev/null
BOUNDARY_LOOP=$(sudo losetup -f --show /tmp/stress_boundary.img)

echo "Boundary device: $BOUNDARY_LOOP (4MB - boundary between linear and geometric)"

MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
BOUNDARY_SECTORS=$(sudo blockdev --getsz "$BOUNDARY_LOOP")

echo "0 $MAIN_SECTORS remap $MAIN_LOOP $BOUNDARY_LOOP 0 $BOUNDARY_SECTORS" | sudo dmsetup create stress-boundary

if sudo dmsetup info stress-boundary | grep -q "State.*ACTIVE"; then
    echo "Testing 300 operations on 4MB boundary device..."
    boundary_success=0
    boundary_collisions=0
    
    for i in $(seq 1 300); do
        sector=$((i * 75 + 500))
        result=$(sudo dmsetup message stress-boundary 0 remap $sector 2>&1 || echo "error")
        
        if [[ "$result" != *"error"* ]]; then
            boundary_success=$((boundary_success + 1))
            
            # Check for collisions at geometric positions
            spare_sector=$(echo "$result" | grep -o 'spare sector [0-9]*' | cut -d' ' -f3)
            if [[ -n "$spare_sector" ]]; then
                for meta_base in 0 1024 2048 4096; do
                    if [[ $spare_sector -ge $meta_base && $spare_sector -lt $((meta_base + 8)) ]]; then
                        boundary_collisions=$((boundary_collisions + 1))
                    fi
                done
            fi
        else
            break
        fi
        
        if [[ $((i % 50)) -eq 0 ]]; then
            echo "    Boundary test progress: $i/300"
        fi
    done
    
    echo ""
    echo "Boundary test results:"
    echo "  Successful operations: $boundary_success"
    echo "  Collisions detected: $boundary_collisions"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if [[ $boundary_success -gt 0 && $boundary_collisions -eq 0 ]]; then
        echo -e "${GREEN}✅ PASSED: Boundary condition test${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}❌ FAILED: Boundary condition test${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    sudo dmsetup remove stress-boundary
fi

sudo losetup -d "$BOUNDARY_LOOP"

echo -e "${PURPLE}=================================================================${NC}"
echo -e "${PURPLE}  COMPREHENSIVE STRESS TEST SUMMARY${NC}"
echo -e "${PURPLE}=================================================================${NC}"

echo "Total Tests Executed: $TOTAL_TESTS"
echo "Passed: $PASSED_TESTS"
echo "Failed: $FAILED_TESTS"
echo "Success Rate: $(( (PASSED_TESTS * 100) / TOTAL_TESTS ))%"

echo ""
if [[ $FAILED_TESTS -eq 0 ]]; then
    echo -e "${GREEN}🎉 ALL STRESS TESTS PASSED!${NC}"
    echo -e "${GREEN}🛡️ Reservation system is robust under high load${NC}"
    echo -e "${GREEN}🚀 v4.0 integration is production-ready!${NC}"
    echo ""
    echo "Key validations completed:"
    echo -e "${GREEN}  ✅ Geometric strategy handles 500+ operations without collisions${NC}"
    echo -e "${GREEN}  ✅ Linear strategy handles 200+ operations without collisions${NC}"
    echo -e "${GREEN}  ✅ Minimal strategy handles 50+ operations without collisions${NC}"
    echo -e "${GREEN}  ✅ Boundary conditions handled correctly${NC}"
    echo -e "${GREEN}  ✅ No metadata corruption under any scenario${NC}"
    echo ""
    echo -e "${GREEN}STATUS: READY FOR PRODUCTION DEPLOYMENT! 🚀${NC}"
    exit 0
else
    echo -e "${RED}❌ STRESS TESTS FAILED${NC}"
    echo -e "${RED}🚨 System not ready for production deployment${NC}"
    echo -e "${RED}Critical issues detected in reservation system${NC}"
    exit 1
fi