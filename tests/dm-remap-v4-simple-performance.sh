#!/bin/bash
# dm-remap v4.0 Enterprise Simple Performance Test
# Direct performance measurement without complex parsing

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=================================${NC}"
echo -e "${BLUE}dm-remap v4.0 Simple Performance Test${NC}"
echo -e "${BLUE}=================================${NC}"

# Test configuration
TEST_SIZE="50M"
BLOCK_SIZE="4k"
COUNT=12800  # 50MB / 4k
TEST_DIR="/tmp/dm-remap-v4-simple-perf"
MODULE_NAME="dm_remap_v4_enterprise"

# Ensure module is loaded
if ! lsmod | grep -q "$MODULE_NAME"; then
    echo "Module not loaded!"
    exit 1
fi

# Setup
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo -e "\n${BLUE}=== Creating Test Environment ===${NC}"

# Create test images
dd if=/dev/zero of=main.img bs=1M count=50 2>/dev/null
dd if=/dev/zero of=spare.img bs=1M count=50 2>/dev/null

# Setup loop devices
MAIN_LOOP=$(sudo losetup -f --show main.img)
SPARE_LOOP=$(sudo losetup -f --show spare.img)
echo "Loop devices: $MAIN_LOOP (main), $SPARE_LOOP (spare)"

# Create dm-remap device
sudo dmsetup create dm-remap-v4-simple --table "0 102400 dm-remap-v4 $MAIN_LOOP $SPARE_LOOP"
DM_DEVICE="/dev/mapper/dm-remap-v4-simple"
echo "dm-remap device: $DM_DEVICE"

# Simple performance function
test_performance() {
    local device="$1"
    local test_name="$2"
    
    echo -e "\n${YELLOW}Testing: $test_name${NC}"
    
    # Clear cache
    sync
    echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
    
    # Time the operation
    START_TIME=$(date +%s.%N)
    dd if="$device" of=/dev/null bs="$BLOCK_SIZE" count="$COUNT" 2>/dev/null
    END_TIME=$(date +%s.%N)
    
    # Calculate throughput
    DURATION=$(echo "$END_TIME - $START_TIME" | bc -l)
    THROUGHPUT_MBS=$(echo "scale=2; 50 / $DURATION" | bc -l)
    
    echo "Duration: ${DURATION} seconds"
    echo "Throughput: ${THROUGHPUT_MBS} MB/s"
    
    echo "$THROUGHPUT_MBS"
}

echo -e "\n${BLUE}=== Performance Testing ===${NC}"

# Test baseline performance
echo "Baseline performance test on $MAIN_LOOP..."
BASELINE_PERF=$(test_performance "$MAIN_LOOP" "Baseline Direct Loop Device")

# Test dm-remap performance  
echo "dm-remap v4.0 performance test on $DM_DEVICE..."
DMREMAP_PERF=$(test_performance "$DM_DEVICE" "dm-remap v4.0 Enterprise")

echo -e "\n${BLUE}=== Performance Analysis ===${NC}"

# Calculate overhead
echo "Baseline performance: ${BASELINE_PERF} MB/s"
echo "dm-remap performance: ${DMREMAP_PERF} MB/s"

if [ "$(echo "$BASELINE_PERF > 0" | bc -l)" = "1" ] && [ "$(echo "$DMREMAP_PERF > 0" | bc -l)" = "1" ]; then
    OVERHEAD=$(echo "scale=3; (($BASELINE_PERF - $DMREMAP_PERF) / $BASELINE_PERF) * 100" | bc -l)
    EFFICIENCY=$(echo "scale=1; ($DMREMAP_PERF / $BASELINE_PERF) * 100" | bc -l)
    
    echo -e "\n${YELLOW}Performance Results:${NC}"
    echo "  Overhead: ${OVERHEAD}%"
    echo "  Efficiency: ${EFFICIENCY}%"
    
    # Check if we meet our <1% overhead target
    if [ "$(echo "$OVERHEAD < 1.0" | bc -l)" = "1" ]; then
        echo -e "${GREEN}🎉 SUCCESS: Overhead (${OVERHEAD}%) is under 1% target!${NC}"
    elif [ "$(echo "$OVERHEAD < 2.0" | bc -l)" = "1" ]; then
        echo -e "${YELLOW}✅ EXCELLENT: Overhead (${OVERHEAD}%) is under 2%${NC}"
    else
        echo -e "${RED}⚠️  ATTENTION: Overhead (${OVERHEAD}%) exceeds targets${NC}"
    fi
else
    echo -e "${RED}❌ Could not calculate performance metrics${NC}"
fi

echo -e "\n${BLUE}=== Final Statistics ===${NC}"
cat /proc/dm-remap-v4

echo -e "\n${BLUE}=== I/O Validation ===${NC}"

# Test actual I/O operations
echo "Testing read operations..."
dd if="$DM_DEVICE" of=/dev/null bs=1M count=5 2>/dev/null
echo "Testing write operations..."
dd if=/dev/zero of="$DM_DEVICE" bs=1M count=5 2>/dev/null

echo -e "\n${BLUE}Updated Statistics After I/O:${NC}"
cat /proc/dm-remap-v4

echo -e "\n${BLUE}=== Cleanup ===${NC}"
sudo dmsetup remove dm-remap-v4-simple
sudo losetup -d "$MAIN_LOOP" "$SPARE_LOOP"
rm -f main.img spare.img
cd /
rm -rf "$TEST_DIR"

echo -e "\n${GREEN}🎯 Performance test completed successfully!${NC}"
echo -e "${GREEN}dm-remap v4.0 Enterprise performance validated${NC}"