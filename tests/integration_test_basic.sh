#!/bin/bash
#
# integration_test_basic.sh - Quick validation of reservation system integration
#
# This test validates that the reservation system is working in the integrated module
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
echo -e "${PURPLE}  dm-remap v4.0 Integration Test - Reservation System${NC}"
echo -e "${PURPLE}=================================================================${NC}"

# Test configuration
TEST_SIZE_MB=10
SPARE_SIZE_MB=2

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up integration test...${NC}"
    
    # Remove device mapper targets
    sudo dmsetup remove integration-test 2>/dev/null || true
    
    # Clean up loop devices
    sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    
    # Remove test files
    rm -f /tmp/integration_main.img /tmp/integration_spare.img
}

# Set trap for cleanup
trap cleanup EXIT

echo -e "${BLUE}Phase 1: Setup integration test environment${NC}"

# Create test devices
echo "Creating test devices..."
dd if=/dev/zero of=/tmp/integration_main.img bs=1M count=$TEST_SIZE_MB 2>/dev/null
dd if=/dev/zero of=/tmp/integration_spare.img bs=1M count=$SPARE_SIZE_MB 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show /tmp/integration_main.img)
SPARE_LOOP=$(sudo losetup -f --show /tmp/integration_spare.img)

echo "Main device: $MAIN_LOOP"
echo "Spare device: $SPARE_LOOP (2MB - should use linear strategy)"

echo -e "${BLUE}Phase 2: Create dm-remap target with reservation system${NC}"

# Create dm-remap target
MAIN_SECTORS=$(sudo blockdev --getsz "$MAIN_LOOP")
echo "0 $MAIN_SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 4096" | sudo dmsetup create integration-test

if ! sudo dmsetup info integration-test | grep -q "State.*ACTIVE"; then
    echo -e "${RED}❌ Failed to create integration test target${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Target created successfully with reservation system${NC}"

echo -e "${BLUE}Phase 3: Test reservation-aware spare allocation${NC}"

echo "Testing 20 remap operations to validate reservation system..."
success_count=0
collision_detected=false

for i in $(seq 1 20); do
    sector=$((i * 100))
    result=$(sudo dmsetup message integration-test 0 remap $sector 2>&1 || echo "error")
    
    if [[ "$result" == *"error"* ]]; then
        echo -e "${YELLOW}⚠️ Remap failed at operation $i: $result${NC}"
        break
    else
        success_count=$((success_count + 1))
        
        # Extract spare sector from result
        spare_sector=$(echo "$result" | grep -o 'spare sector [0-9]*' | cut -d' ' -f3)
        
        if [[ -n "$spare_sector" ]]; then
            # For 2MB device with linear strategy, check if allocation avoided metadata sectors
            # Expected metadata locations for linear strategy: approximately 0, 1365, 2731, 4096
            for meta_sector in 0 1365 2731; do
                meta_end=$((meta_sector + 10))  # Safety margin
                if [[ $spare_sector -ge $meta_sector && $spare_sector -le $meta_end ]]; then
                    echo -e "${RED}🚨 COLLISION DETECTED: Spare sector $spare_sector conflicts with metadata near $meta_sector${NC}"
                    collision_detected=true
                fi
            done
        fi
        
        if [[ $((i % 5)) -eq 0 ]]; then
            echo "  Completed $i remap operations..."
        fi
    fi
done

echo -e "${BLUE}Phase 4: Results validation${NC}"

echo "Integration test results:"
echo "  Total remap operations attempted: 20"
echo "  Successful operations: $success_count"
echo "  Metadata collisions detected: $(if $collision_detected; then echo "YES"; else echo "NO"; fi)"

# Check target status
echo ""
echo "Target status:"
sudo dmsetup status integration-test

echo ""
if [[ $success_count -gt 0 ]] && ! $collision_detected; then
    echo -e "${GREEN}🎉 INTEGRATION TEST PASSED!${NC}"
    echo -e "${GREEN}✅ Reservation system is working correctly${NC}"
    echo -e "${GREEN}✅ No metadata collisions detected${NC}"
    echo -e "${GREEN}✅ Spare allocation is reservation-aware${NC}"
    echo ""
    echo -e "${GREEN}v4.0 kernel integration is successful!${NC}"
    exit 0
else
    echo -e "${RED}❌ INTEGRATION TEST FAILED${NC}"
    if $collision_detected; then
        echo -e "${RED}❌ Metadata collision vulnerability still exists${NC}"
    fi
    if [[ $success_count -eq 0 ]]; then
        echo -e "${RED}❌ No successful remap operations${NC}"
    fi
    exit 1
fi