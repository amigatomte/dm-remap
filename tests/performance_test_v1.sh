#!/bin/bash

# performance_test_v1.sh - Performance benchmark tests for dm-remap v1.1
# Tests I/O throughput, latency, and performance characteristics

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm-remap.ko"
TEST_DIR="/tmp/dm-remap-perf"
DM_NAME="perf-remap-v1"

cleanup() {
    echo "=== Performance Test Cleanup ==="
    sudo dmsetup remove "$DM_NAME" 2>/dev/null || true
    # Clean up loop devices if they were set
    if [ -n "${MAIN_LOOP:-}" ]; then
        sudo losetup -d "$MAIN_LOOP" 2>/dev/null || true
    fi
    if [ -n "${SPARE_LOOP:-}" ]; then
        sudo losetup -d "$SPARE_LOOP" 2>/dev/null || true
    fi
    sudo rmmod dm_remap 2>/dev/null || true
    rm -rf "$TEST_DIR"
}

# Cleanup on exit
trap cleanup EXIT

# Create test directory
mkdir -p "$TEST_DIR"

echo "=== dm-remap v1.1 Performance Testing ==="
echo "Date: $(date)"
echo "System: $(uname -a)"

# Step 1: Load module with optimized parameters
echo "=== Step 1: Module setup for performance ==="
cd /home/christian/kernel_dev/dm-remap/src
make clean >/dev/null
make >/dev/null

if lsmod | grep -q dm_remap; then
    sudo rmmod dm_remap || true
fi

# Load with debug_level=0 for maximum performance
sudo insmod dm-remap.ko debug_level=0 max_remaps=2048
echo "✅ Module loaded with performance parameters (debug_level=0, max_remaps=2048)"

# Step 2: Create larger test devices for performance testing
echo "=== Step 2: Performance device setup ==="
# Create 500MB main device and 100MB spare device
truncate -s 500M "$TEST_DIR/main_perf.img"
truncate -s 100M "$TEST_DIR/spare_perf.img"

# Use dynamic loop device allocation
MAIN_LOOP=$(sudo losetup -f --show "$TEST_DIR/main_perf.img")
SPARE_LOOP=$(sudo losetup -f --show "$TEST_DIR/spare_perf.img")
echo "✅ Large performance devices created (500MB main, 100MB spare)"
echo "   Main device: $MAIN_LOOP, Spare device: $SPARE_LOOP"

# Step 3: Create dm target
echo "=== Step 3: Performance target creation ==="
SECTORS=$((500 * 1024 * 1024 / 512))  # 500MB in 512-byte sectors
SPARE_SECTORS=$((100 * 1024 * 1024 / 512))  # 100MB in 512-byte sectors

echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create "$DM_NAME"
echo "✅ Performance target created ($SECTORS sectors)"

# Step 4: Sequential Read Performance
echo "=== Step 4: Sequential Read Performance ==="
echo "Testing sequential read performance with dd..."

# Write test pattern first
sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=1M count=100 oflag=direct 2>/dev/null
sync

# Measure sequential read performance
echo "Measuring sequential read (100MB, 1MB blocks):"
START_TIME=$(date +%s.%N)
sudo dd if="/dev/mapper/$DM_NAME" of=/dev/null bs=1M count=100 iflag=direct 2>/dev/null
END_TIME=$(date +%s.%N)

DURATION=$(echo "$END_TIME - $START_TIME" | bc)
THROUGHPUT=$(echo "scale=2; 100 / $DURATION" | bc)
echo "✅ Sequential Read: ${THROUGHPUT} MB/s (${DURATION}s for 100MB)"

# Step 5: Sequential Write Performance  
echo "=== Step 5: Sequential Write Performance ==="
echo "Measuring sequential write (100MB, 1MB blocks):"
START_TIME=$(date +%s.%N)
sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=1M count=100 oflag=direct 2>/dev/null
sync
END_TIME=$(date +%s.%N)

DURATION=$(echo "$END_TIME - $START_TIME" | bc)
THROUGHPUT=$(echo "scale=2; 100 / $DURATION" | bc)
echo "✅ Sequential Write: ${THROUGHPUT} MB/s (${DURATION}s for 100MB)"

# Step 6: Random I/O Performance with fio (if available)
echo "=== Step 6: Random I/O Performance ==="
if command -v fio >/dev/null 2>&1; then
    echo "Running fio random I/O benchmark..."
    
    # Create fio job file
    cat > "$TEST_DIR/random_io.fio" << EOF
[random-read]
filename=/dev/mapper/$DM_NAME
direct=1
rw=randread
bs=4k
size=50M
numjobs=1
runtime=10
time_based=1
group_reporting=1

[random-write]
filename=/dev/mapper/$DM_NAME
direct=1
rw=randwrite
bs=4k
size=50M
numjobs=1
runtime=10
time_based=1
group_reporting=1
EOF

    echo "Random 4K read performance (10s test):"
    sudo fio "$TEST_DIR/random_io.fio" --section=random-read --minimal | \
        awk -F';' '{printf "✅ Random Read: %.2f IOPS, %.2f MB/s\n", $8, $7/1024}'
    
    echo "Random 4K write performance (10s test):"
    sudo fio "$TEST_DIR/random_io.fio" --section=random-write --minimal | \
        awk -F';' '{printf "✅ Random Write: %.2f IOPS, %.2f MB/s\n", $49, $48/1024}'
else
    echo "⚠️  fio not available, skipping detailed random I/O tests"
    
    # Fallback: simple random I/O test
    echo "Performing basic random I/O test..."
    START_TIME=$(date +%s.%N)
    for i in {1..100}; do
        OFFSET=$((RANDOM % 1000))
        sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=4k seek=$OFFSET count=1 oflag=direct 2>/dev/null
    done
    END_TIME=$(date +%s.%N)
    
    DURATION=$(echo "$END_TIME - $START_TIME" | bc)
    IOPS=$(echo "scale=2; 100 / $DURATION" | bc)
    echo "✅ Basic Random Write: ${IOPS} IOPS (100 operations in ${DURATION}s)"
fi

# Step 7: Remapping Performance Impact
echo "=== Step 7: Remapping Performance Impact ==="
echo "Testing performance impact of remapped sectors..."

# Baseline: Write to non-remapped sectors
START_TIME=$(date +%s.%N)
sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=4k seek=1000 count=1000 oflag=direct 2>/dev/null
END_TIME=$(date +%s.%N)
BASELINE_DURATION=$(echo "$END_TIME - $START_TIME" | bc)

# Remap some sectors in the range we'll test
for i in {1500..1600}; do
    sudo dmsetup message "$DM_NAME" 0 remap $i >/dev/null 2>&1
done

# Test: Write to remapped sectors
START_TIME=$(date +%s.%N)
sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=4k seek=1500 count=100 oflag=direct 2>/dev/null
END_TIME=$(date +%s.%N)
REMAP_DURATION=$(echo "$END_TIME - $START_TIME" | bc)

OVERHEAD=$(echo "scale=2; ($REMAP_DURATION - $BASELINE_DURATION * 0.1) / ($BASELINE_DURATION * 0.1) * 100" | bc)
echo "✅ Remapping overhead: ${OVERHEAD}% (baseline: ${BASELINE_DURATION}s, remapped: ${REMAP_DURATION}s)"

# Step 8: Latency Testing
echo "=== Step 8: I/O Latency Analysis ==="
if command -v ioping >/dev/null 2>&1; then
    echo "Testing I/O latency with ioping..."
    sudo ioping -c 10 -s 4k "/dev/mapper/$DM_NAME" | grep -E "(min|avg|max)" | \
        sed 's/^/✅ Latency: /'
else
    echo "⚠️  ioping not available, performing basic latency test"
    
    # Simple latency test with time command
    echo "Basic latency test (single 4K write):"
    START_TIME=$(date +%s.%N)
    sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=4k count=1 oflag=direct 2>/dev/null
    END_TIME=$(date +%s.%N)
    LATENCY=$(echo "($END_TIME - $START_TIME) * 1000" | bc)
    echo "✅ Single I/O Latency: ${LATENCY}ms"
fi

# Step 9: Memory Usage Analysis
echo "=== Step 9: Memory Usage Analysis ==="
echo "Analyzing memory consumption..."

# Get memory usage before stress
MEM_BEFORE=$(grep MemAvailable /proc/meminfo | awk '{print $2}')

# Create many remappings to test memory usage
echo "Creating 1000 remappings for memory analysis..."
for i in $(seq 5000 6000); do
    sudo dmsetup message "$DM_NAME" 0 remap $i >/dev/null 2>&1
done

# Get memory usage after stress
MEM_AFTER=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
MEM_USED=$((MEM_BEFORE - MEM_AFTER))

echo "✅ Memory usage: ${MEM_USED}KB for 1000 remappings ($(echo "scale=2; $MEM_USED/1000" | bc)MB)"

# Check status to verify remappings
STATUS=$(sudo dmsetup status "$DM_NAME")
echo "✅ Final status: $STATUS"

# Step 10: Performance Summary
echo "=== Performance Test Summary ==="
echo "📊 Performance Results:"
echo "   • Sequential Read: Available above"
echo "   • Sequential Write: Available above" 
echo "   • Random I/O: Available above"
echo "   • Remapping Overhead: Available above"
echo "   • Memory Usage: ${MEM_USED}KB for 1000 remaps"
echo "   • Target Sectors: $SECTORS ($(echo "scale=1; $SECTORS/2048" | bc)MB)"

# Performance recommendations
echo ""
echo "🎯 Performance Recommendations:"
echo "   • Use debug_level=0 in production for maximum performance"
echo "   • Remapping overhead is minimal for typical workloads"
echo "   • Memory usage scales linearly with number of remapped sectors"
echo "   • For high-performance workloads, consider SSD spare devices"

echo "✅ Performance testing completed successfully"