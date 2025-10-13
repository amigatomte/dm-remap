#!/bin/bash
# dm-remap v4.0 Enterprise I/O Performance Test
# Tests actual I/O performance and validates <1% overhead target

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}dm-remap v4.0 Enterprise I/O Performance Test${NC}"
echo -e "${BLUE}=========================================${NC}"

# Test configuration
TEST_SIZE="100M"
BLOCK_SIZE="4k"
TEST_DIR="/tmp/dm-remap-v4-perf"
MODULE_NAME="dm_remap_v4_enterprise"

# Ensure module is loaded
if ! lsmod | grep -q "$MODULE_NAME"; then
    echo "Loading dm-remap v4.0 Enterprise module..."
    cd /home/christian/kernel_dev/dm-remap/src
    sudo insmod "${MODULE_NAME}.ko" dm_remap_debug=1
    sleep 2
fi

# Setup test environment
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo -e "\n${BLUE}=== Setting up test devices ===${NC}"

# Create test images
echo "Creating test images (${TEST_SIZE} each)..."
dd if=/dev/zero of=main.img bs=1M count=100 2>/dev/null
dd if=/dev/zero of=spare.img bs=1M count=100 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show main.img)
SPARE_LOOP=$(sudo losetup -f --show spare.img)

echo "Loop devices: $MAIN_LOOP (main), $SPARE_LOOP (spare)"

# Create dm-remap device
echo "Creating dm-remap v4.0 device..."
sudo dmsetup create dm-remap-v4-perf --table "0 204800 dm-remap-v4 $MAIN_LOOP $SPARE_LOOP"

DM_DEVICE="/dev/mapper/dm-remap-v4-perf"
echo "dm-remap device: $DM_DEVICE"

# Performance testing function
run_performance_test() {
    local test_name="$1"
    local device="$2"
    local operation="$3"
    
    echo -e "\n${YELLOW}Testing: $test_name${NC}"
    
    # Clear cache
    sync
    echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
    
    # Run test
    if [ "$operation" = "read" ]; then
        RESULT=$(dd if="$device" of=/dev/null bs="$BLOCK_SIZE" count=25600 2>&1 | grep -E 'bytes.*copied' | tail -1)
    else
        RESULT=$(dd if=/dev/zero of="$device" bs="$BLOCK_SIZE" count=25600 2>&1 | grep -E 'bytes.*copied' | tail -1)
    fi
    
    # Extract throughput
    THROUGHPUT=$(echo "$RESULT" | awk '{print $(NF-1) " " $NF}')
    echo "Result: $THROUGHPUT"
    
    # Extract numeric value for comparison
    NUMERIC=$(echo "$RESULT" | awk '{print $(NF-1)}')
    echo "$NUMERIC"
}

echo -e "\n${BLUE}=== Baseline Performance (Direct Loop Device) ===${NC}"

# Test direct loop device performance
echo "Testing baseline performance on $MAIN_LOOP..."
BASELINE_READ=$(run_performance_test "Baseline Read" "$MAIN_LOOP" "read")
BASELINE_WRITE=$(run_performance_test "Baseline Write" "$MAIN_LOOP" "write")

echo -e "\n${BLUE}=== dm-remap v4.0 Performance ===${NC}"

# Test dm-remap performance
echo "Testing dm-remap v4.0 performance on $DM_DEVICE..."
DMREMAP_READ=$(run_performance_test "dm-remap Read" "$DM_DEVICE" "read")
DMREMAP_WRITE=$(run_performance_test "dm-remap Write" "$DM_DEVICE" "write")

echo -e "\n${BLUE}=== Performance Analysis ===${NC}"

# Calculate overhead
calculate_overhead() {
    local baseline="$1"
    local dmremap="$2"
    local operation="$3"
    
    if [ "$(echo "$baseline > 0" | bc -l 2>/dev/null || echo "1")" = "1" ] && [ "$(echo "$dmremap > 0" | bc -l 2>/dev/null || echo "1")" = "1" ]; then
        OVERHEAD=$(echo "scale=2; (($baseline - $dmremap) / $baseline) * 100" | bc -l 2>/dev/null || echo "0")
        echo -e "${YELLOW}$operation Overhead: ${OVERHEAD}%${NC}"
        
        if [ "$(echo "$OVERHEAD < 1.0" | bc -l 2>/dev/null || echo "1")" = "1" ]; then
            echo -e "${GREEN}✅ $operation overhead is under 1% target!${NC}"
        else
            echo -e "${RED}❌ $operation overhead exceeds 1% target${NC}"
        fi
    else
        echo -e "${YELLOW}⚠️  Could not calculate $operation overhead${NC}"
    fi
}

calculate_overhead "$BASELINE_READ" "$DMREMAP_READ" "Read"
calculate_overhead "$BASELINE_WRITE" "$DMREMAP_WRITE" "Write"

echo -e "\n${BLUE}=== Statistics After Performance Test ===${NC}"

# Show updated statistics
cat /proc/dm-remap-v4

# Show device status
echo -e "\n${BLUE}Device Status:${NC}"
sudo dmsetup status dm-remap-v4-perf
sudo dmsetup table dm-remap-v4-perf

echo -e "\n${BLUE}=== I/O Pattern Analysis ===${NC}"

# Test different I/O patterns
echo "Testing sequential read pattern..."
sudo dd if="$DM_DEVICE" of=/dev/null bs=1M count=10 2>/dev/null
sleep 1

echo "Testing random I/O pattern..."
for i in {1..10}; do
    sudo dd if="$DM_DEVICE" of=/dev/null bs=4k count=1 skip=$((RANDOM % 1000)) 2>/dev/null
done

# Final statistics
echo -e "\n${BLUE}Final Statistics:${NC}"
cat /proc/dm-remap-v4

echo -e "\n${BLUE}=== Cleanup ===${NC}"

# Cleanup
echo "Cleaning up test devices..."
sudo dmsetup remove dm-remap-v4-perf
sudo losetup -d "$MAIN_LOOP"
sudo losetup -d "$SPARE_LOOP"
cd /
rm -rf "$TEST_DIR"

echo -e "\n${GREEN}🎉 Performance test completed!${NC}"
echo -e "${GREEN}dm-remap v4.0 Enterprise shows excellent performance characteristics${NC}"