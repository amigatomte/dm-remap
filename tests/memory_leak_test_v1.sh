#!/bin/bash

# memory_leak_test_v1.sh - Memory leak detection for dm-remap v1.1
# Tests for memory leaks during module lifecycle and operation

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm-remap.ko"
TEST_DIR="/tmp/dm-remap-memleak"
DM_NAME="memleak-remap-v1"

cleanup() {
    echo "=== Memory Leak Test Cleanup ==="
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

# Memory monitoring functions
get_memory_stats() {
    # Get various memory statistics
    echo "=== Memory Statistics ==="
    
    # System memory
    MEM_TOTAL=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    MEM_FREE=$(grep MemFree /proc/meminfo | awk '{print $2}')
    MEM_AVAILABLE=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
    MEM_USED=$((MEM_TOTAL - MEM_AVAILABLE))
    
    echo "System Memory: ${MEM_USED}KB used / ${MEM_TOTAL}KB total ($(echo "scale=1; $MEM_USED * 100 / $MEM_TOTAL" | bc)%)"
    
    # Kernel memory (slab allocations)
    if [ -f /proc/meminfo ]; then
        SLAB_TOTAL=$(grep "^Slab:" /proc/meminfo | awk '{print $2}')
        SLAB_RECLAIMABLE=$(grep "SReclaimable:" /proc/meminfo | awk '{print $2}')
        SLAB_UNRECLAIMABLE=$(grep "SUnreclaim:" /proc/meminfo | awk '{print $2}')
        echo "Kernel Slab: ${SLAB_TOTAL}KB total (${SLAB_RECLAIMABLE}KB reclaimable, ${SLAB_UNRECLAIMABLE}KB unreclaimable)"
    fi
    
    # Page cache and buffers
    BUFFERS=$(grep "^Buffers:" /proc/meminfo | awk '{print $2}')
    CACHED=$(grep "^Cached:" /proc/meminfo | awk '{print $2}')
    echo "Cache/Buffers: ${CACHED}KB cached, ${BUFFERS}KB buffers"
    
    # Module-specific memory (if we can detect it)
    if lsmod | grep -q dm_remap; then
        MODULE_SIZE=$(lsmod | grep dm_remap | awk '{print $2}')
        echo "dm-remap module size: ${MODULE_SIZE} bytes"
    fi
    
    echo ""
}

get_process_memory() {
    local process_name=$1
    if pgrep "$process_name" >/dev/null; then
        ps -o pid,vsz,rss,comm -C "$process_name" | grep -v PID
    fi
}

# Create test directory
mkdir -p "$TEST_DIR"

echo "=== dm-remap v1.1 Memory Leak Detection ==="
echo "Date: $(date)"
echo "System: $(uname -a)"

# Step 1: Baseline memory measurement
echo "=== Step 1: Baseline Memory Measurement ==="
echo "Measuring memory usage before any dm-remap operations..."

# Force memory cleanup
sync
echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null
sleep 2

echo "📊 Baseline Memory State:"
get_memory_stats

# Store baseline values
BASELINE_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}') 
BASELINE_MEM_USED=$((BASELINE_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))
BASELINE_SLAB=$(grep "^Slab:" /proc/meminfo | awk '{print $2}')

echo "Stored baseline: Memory=${BASELINE_MEM_USED}KB, Slab=${BASELINE_SLAB}KB"

# Step 2: Module Load/Unload Cycle Testing
echo "=== Step 2: Module Load/Unload Cycle Testing ==="
echo "Testing for memory leaks in module loading/unloading..."

cd /home/christian/kernel_dev/dm-remap/src
make clean >/dev/null
make >/dev/null

# Test multiple load/unload cycles
LOAD_CYCLES=10
echo "Performing $LOAD_CYCLES module load/unload cycles..."

for cycle in $(seq 1 $LOAD_CYCLES); do
    echo -n "Cycle $cycle/$LOAD_CYCLES: "
    
    # Load module
    if sudo insmod dm-remap.ko debug_level=0 2>/dev/null; then
        echo -n "loaded, "
    else
        echo "❌ Failed to load module in cycle $cycle"
        continue
    fi
    
    # Brief pause
    sleep 0.5
    
    # Unload module  
    if sudo rmmod dm_remap 2>/dev/null; then
        echo "unloaded ✅"
    else
        echo "❌ Failed to unload module in cycle $cycle"
        # Force cleanup attempt
        sudo rmmod dm_remap 2>/dev/null || true
        continue
    fi
    
    sleep 0.5
done

# Check memory after load/unload cycles
echo "📊 Memory After Load/Unload Cycles:"
sync
echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null
sleep 2
get_memory_stats

AFTER_CYCLES_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}')
AFTER_CYCLES_MEM_USED=$((AFTER_CYCLES_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))
AFTER_CYCLES_SLAB=$(grep "^Slab:" /proc/meminfo | awk '{print $2}')

CYCLES_MEM_DIFF=$((AFTER_CYCLES_MEM_USED - BASELINE_MEM_USED))
CYCLES_SLAB_DIFF=$((AFTER_CYCLES_SLAB - BASELINE_SLAB))

echo "Memory change after cycles: ${CYCLES_MEM_DIFF}KB total, ${CYCLES_SLAB_DIFF}KB slab"

if [ $CYCLES_MEM_DIFF -lt 1000 ]; then
    echo "✅ Module load/unload: No significant memory leak detected"
else
    echo "⚠️  Module load/unload: Potential memory leak of ${CYCLES_MEM_DIFF}KB"
fi

# Step 3: Target Creation/Destruction Testing
echo "=== Step 3: Target Creation/Destruction Testing ==="
echo "Testing for memory leaks in target lifecycle..."

# Load module for target testing
sudo insmod dm-remap.ko debug_level=0

# Create test devices
truncate -s 100M "$TEST_DIR/main_mem.img"
truncate -s 50M "$TEST_DIR/spare_mem.img"

# Use dynamic loop device allocation
MAIN_LOOP=$(sudo losetup -f --show "$TEST_DIR/main_mem.img")
SPARE_LOOP=$(sudo losetup -f --show "$TEST_DIR/spare_mem.img")
echo "Main device: $MAIN_LOOP, Spare device: $SPARE_LOOP"

# Test multiple target create/destroy cycles
TARGET_CYCLES=20
echo "Performing $TARGET_CYCLES target create/destroy cycles..."

SECTORS=$((100 * 1024 * 1024 / 512))
SPARE_SECTORS=$((50 * 1024 * 1024 / 512))

for cycle in $(seq 1 $TARGET_CYCLES); do
    echo -n "Target cycle $cycle/$TARGET_CYCLES: "
    
    # Create target
    if echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create "${DM_NAME}-${cycle}" 2>/dev/null; then
        echo -n "created, "
    else
        echo "❌ Failed to create target in cycle $cycle"
        continue
    fi
    
    # Brief I/O to ensure target is active
    sudo dd if=/dev/zero of="/dev/mapper/${DM_NAME}-${cycle}" bs=4k count=1 oflag=direct 2>/dev/null || true
    
    # Destroy target
    if sudo dmsetup remove "${DM_NAME}-${cycle}" 2>/dev/null; then
        echo "destroyed ✅"
    else
        echo "❌ Failed to destroy target in cycle $cycle"
        # Force cleanup attempt
        sudo dmsetup remove "${DM_NAME}-${cycle}" 2>/dev/null || true
        continue
    fi
    
    # Brief pause to allow cleanup
    sleep 0.1
done

# Check memory after target cycles
echo "📊 Memory After Target Cycles:"
sync
sleep 2
get_memory_stats

AFTER_TARGETS_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}')
AFTER_TARGETS_MEM_USED=$((AFTER_TARGETS_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))
AFTER_TARGETS_SLAB=$(grep "^Slab:" /proc/meminfo | awk '{print $2}')

TARGETS_MEM_DIFF=$((AFTER_TARGETS_MEM_USED - AFTER_CYCLES_MEM_USED))
TARGETS_SLAB_DIFF=$((AFTER_TARGETS_SLAB - AFTER_CYCLES_SLAB))

echo "Memory change after target cycles: ${TARGETS_MEM_DIFF}KB total, ${TARGETS_SLAB_DIFF}KB slab"

if [ $TARGETS_MEM_DIFF -lt 1000 ]; then
    echo "✅ Target lifecycle: No significant memory leak detected"
else
    echo "⚠️  Target lifecycle: Potential memory leak of ${TARGETS_MEM_DIFF}KB"
fi

# Step 4: Remapping Operations Memory Testing
echo "=== Step 4: Remapping Operations Memory Testing ==="
echo "Testing for memory leaks in remapping operations..."

# Create a single target for remapping tests
echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create "$DM_NAME"

# Perform many remapping operations
REMAP_OPERATIONS=1000
echo "Performing $REMAP_OPERATIONS remapping operations..."

for i in $(seq 1 $REMAP_OPERATIONS); do
    SECTOR=$((i * 10))  # Spread out the sectors
    sudo dmsetup message "$DM_NAME" 0 remap $SECTOR >/dev/null 2>&1
    
    if [ $((i % 100)) -eq 0 ]; then
        echo -n "."
    fi
done
echo ""

# Check memory after remapping operations
echo "📊 Memory After Remapping Operations:"
get_memory_stats

AFTER_REMAPS_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}')
AFTER_REMAPS_MEM_USED=$((AFTER_REMAPS_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))
AFTER_REMAPS_SLAB=$(grep "^Slab:" /proc/meminfo | awk '{print $2}')

REMAPS_MEM_DIFF=$((AFTER_REMAPS_MEM_USED - AFTER_TARGETS_MEM_USED))
REMAPS_SLAB_DIFF=$((AFTER_REMAPS_SLAB - AFTER_TARGETS_SLAB))

echo "Memory change after $REMAP_OPERATIONS remaps: ${REMAPS_MEM_DIFF}KB total, ${REMAPS_SLAB_DIFF}KB slab"

# This is expected to use memory for storing remapping table entries
EXPECTED_REMAP_MEMORY=$((REMAP_OPERATIONS * 16 / 1024))  # Rough estimate: 16 bytes per remap entry
echo "Expected memory for remaps: ~${EXPECTED_REMAP_MEMORY}KB"

if [ $REMAPS_MEM_DIFF -lt $((EXPECTED_REMAP_MEMORY * 3)) ]; then
    echo "✅ Remapping operations: Memory usage within expected range"
else
    echo "⚠️  Remapping operations: Memory usage higher than expected"
fi

# Test clearing remaps to see if memory is freed
echo "Testing memory cleanup with clear command..."
sudo dmsetup message "$DM_NAME" 0 clear

sleep 2
sync

AFTER_CLEAR_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}')
AFTER_CLEAR_MEM_USED=$((AFTER_CLEAR_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))

CLEAR_MEM_DIFF=$((AFTER_CLEAR_MEM_USED - AFTER_REMAPS_MEM_USED))
echo "Memory change after clear: ${CLEAR_MEM_DIFF}KB"

if [ $CLEAR_MEM_DIFF -gt -500 ]; then
    echo "⚠️  Clear command: Memory not significantly freed (possible leak)"
else
    echo "✅ Clear command: Memory properly freed"
fi

# Step 5: I/O Operations Memory Testing
echo "=== Step 5: I/O Operations Memory Testing ==="
echo "Testing for memory leaks in I/O operations..."

# Perform sustained I/O operations
IO_OPERATIONS=2000
echo "Performing $IO_OPERATIONS I/O operations..."

for i in $(seq 1 $IO_OPERATIONS); do
    OFFSET=$((i % 1000))
    
    # Alternate between reads and writes
    if [ $((i % 2)) -eq 0 ]; then
        sudo dd if=/dev/zero of="/dev/mapper/$DM_NAME" bs=4k seek=$OFFSET count=1 oflag=direct 2>/dev/null || true
    else
        sudo dd if="/dev/mapper/$DM_NAME" of=/dev/null bs=4k skip=$OFFSET count=1 iflag=direct 2>/dev/null || true
    fi
    
    if [ $((i % 200)) -eq 0 ]; then
        echo -n "."
    fi
done
echo ""

# Check memory after I/O operations
echo "📊 Memory After I/O Operations:"
get_memory_stats

AFTER_IO_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}')
AFTER_IO_MEM_USED=$((AFTER_IO_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))

IO_MEM_DIFF=$((AFTER_IO_MEM_USED - AFTER_CLEAR_MEM_USED))
echo "Memory change after $IO_OPERATIONS I/O ops: ${IO_MEM_DIFF}KB"

if [ $IO_MEM_DIFF -lt 500 ]; then
    echo "✅ I/O operations: No significant memory leak detected"
else
    echo "⚠️  I/O operations: Potential memory leak of ${IO_MEM_DIFF}KB"
fi

# Step 6: Final Cleanup and Leak Assessment
echo "=== Step 6: Final Cleanup and Leak Assessment ==="

# Clean up target
sudo dmsetup remove "$DM_NAME"
sudo losetup -d "$MAIN_LOOP"
sudo losetup -d "$SPARE_LOOP"

# Unload module
sudo rmmod dm_remap

# Force system cleanup
sync
echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null
sleep 3

echo "📊 Final Memory State:"
get_memory_stats

FINAL_MEM_USED=$(grep MemTotal /proc/meminfo | awk '{print $2}')
FINAL_MEM_USED=$((FINAL_MEM_USED - $(grep MemAvailable /proc/meminfo | awk '{print $2}')))
FINAL_SLAB=$(grep "^Slab:" /proc/meminfo | awk '{print $2}')

TOTAL_MEM_DIFF=$((FINAL_MEM_USED - BASELINE_MEM_USED))
TOTAL_SLAB_DIFF=$((FINAL_SLAB - BASELINE_SLAB))

echo "=== Memory Leak Detection Summary ==="
echo "📊 Memory Changes from Baseline:"
echo "   • Total System Memory: ${TOTAL_MEM_DIFF}KB"
echo "   • Kernel Slab Memory: ${TOTAL_SLAB_DIFF}KB"
echo ""
echo "📋 Test Results:"
echo "   • Module load/unload cycles: $LOAD_CYCLES cycles"
echo "   • Target create/destroy cycles: $TARGET_CYCLES cycles"  
echo "   • Remapping operations: $REMAP_OPERATIONS operations"
echo "   • I/O operations: $IO_OPERATIONS operations"

# Overall assessment
echo ""
echo "🔍 Leak Assessment:"
if [ $TOTAL_MEM_DIFF -lt 1000 ] && [ $TOTAL_SLAB_DIFF -lt 500 ]; then
    echo "🎉 EXCELLENT: No significant memory leaks detected"
    echo "   ✅ All memory properly freed after operations"
elif [ $TOTAL_MEM_DIFF -lt 5000 ] && [ $TOTAL_SLAB_DIFF -lt 2000 ]; then
    echo "✅ GOOD: Minimal memory usage increase (likely normal variance)"
    echo "   📊 Changes within acceptable limits for system noise"
else
    echo "⚠️  ATTENTION: Potential memory leak detected"
    echo "   🔍 Investigation recommended for production use"
    echo "   📊 Memory increase: ${TOTAL_MEM_DIFF}KB total, ${TOTAL_SLAB_DIFF}KB kernel"
fi

echo ""
echo "✅ Memory leak detection completed"