#!/bin/bash

# stress_test_v1.sh - Concurrent stress testing for dm-remap v1.1
# Tests module stability under heavy concurrent I/O load

set -euo pipefail

MODULE_PATH="/home/christian/kernel_dev/dm-remap/src/dm-remap.ko"
TEST_DIR="/tmp/dm-remap-stress"
DM_NAME="stress-remap-v1"
NUM_PROCESSES=8
TEST_DURATION=30  # seconds

cleanup() {
    echo "=== Stress Test Cleanup ==="
    # Kill any background processes
    pkill -f "stress_worker" 2>/dev/null || true
    pkill -f "dd.*$DM_NAME" 2>/dev/null || true
    sleep 2
    
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

echo "=== dm-remap v1.1 Concurrent Stress Testing ==="
echo "Date: $(date)"
echo "Configuration: $NUM_PROCESSES processes, ${TEST_DURATION}s duration"

# Step 1: Load module with stress parameters
echo "=== Step 1: Module setup for stress testing ==="
cd /home/christian/kernel_dev/dm-remap/src
make clean >/dev/null
make >/dev/null

if lsmod | grep -q dm_remap; then
    sudo rmmod dm_remap || true
fi

# Load with debug_level=1 to capture important events but not flood logs
sudo insmod dm-remap.ko debug_level=1 max_remaps=4096
echo "✅ Module loaded for stress testing (debug_level=1, max_remaps=4096)"

# Step 2: Create stress test devices
echo "=== Step 2: Stress test device setup ==="
# Create 1GB main device and 200MB spare device
truncate -s 1G "$TEST_DIR/main_stress.img"
truncate -s 200M "$TEST_DIR/spare_stress.img"

# Use dynamic loop device allocation to avoid conflicts
MAIN_LOOP=$(sudo losetup -f --show "$TEST_DIR/main_stress.img")
SPARE_LOOP=$(sudo losetup -f --show "$TEST_DIR/spare_stress.img")
echo "✅ Stress test devices created (1GB main, 200MB spare)"
echo "   Main device: $MAIN_LOOP, Spare device: $SPARE_LOOP"

# Step 3: Create dm target
echo "=== Step 3: Stress test target creation ==="
SECTORS=$((1024 * 1024 * 1024 / 512))  # 1GB in 512-byte sectors  
SPARE_SECTORS=$((200 * 1024 * 1024 / 512))  # 200MB in 512-byte sectors

echo "0 $SECTORS remap $MAIN_LOOP $SPARE_LOOP 0 $SPARE_SECTORS" | sudo dmsetup create "$DM_NAME"
echo "✅ Stress test target created ($SECTORS sectors)"

# Step 4: Create stress worker function
create_stress_worker() {
    local worker_id=$1
    local worker_file="$TEST_DIR/stress_worker_${worker_id}.sh"
    
    cat > "$worker_file" << EOF
#!/bin/bash
# Stress worker $worker_id

WORKER_ID=$worker_id
DM_DEVICE="/dev/mapper/$DM_NAME"
STATS_FILE="$TEST_DIR/worker_\${WORKER_ID}_stats.txt"
ERROR_FILE="$TEST_DIR/worker_\${WORKER_ID}_errors.txt"

# Initialize stats
echo "0" > "\$STATS_FILE"  # operation counter
echo "0" > "\$ERROR_FILE"  # error counter

# Get unique sector ranges for this worker to avoid conflicts
SECTOR_START=\$((WORKER_ID * 10000))
SECTOR_END=\$(((WORKER_ID + 1) * 10000 - 1))

echo "Worker \$WORKER_ID: Testing sectors \$SECTOR_START to \$SECTOR_END"

operations=0
errors=0

while [ ! -f "$TEST_DIR/stop_workers" ]; do
    # Random operation type
    OP_TYPE=\$((RANDOM % 4))
    
    case \$OP_TYPE in
        0)  # Sequential write
            OFFSET=\$((SECTOR_START + (operations % 1000)))
            if sudo dd if=/dev/zero of="\$DM_DEVICE" bs=4k seek=\$OFFSET count=1 oflag=direct 2>/dev/null; then
                operations=\$((operations + 1))
            else
                errors=\$((errors + 1))
            fi
            ;;
        1)  # Random write  
            OFFSET=\$((SECTOR_START + (RANDOM % 9000)))
            if sudo dd if=/dev/zero of="\$DM_DEVICE" bs=4k seek=\$OFFSET count=1 oflag=direct 2>/dev/null; then
                operations=\$((operations + 1))
            else
                errors=\$((errors + 1))
            fi
            ;;
        2)  # Random read
            OFFSET=\$((SECTOR_START + (RANDOM % 9000)))
            if sudo dd if="\$DM_DEVICE" of=/dev/null bs=4k skip=\$OFFSET count=1 iflag=direct 2>/dev/null; then
                operations=\$((operations + 1))
            else
                errors=\$((errors + 1))
            fi
            ;;
        3)  # Add remapping (occasionally)
            if [ \$((operations % 50)) -eq 0 ]; then
                REMAP_SECTOR=\$((SECTOR_START + (RANDOM % 5000)))
                if sudo dmsetup message "$DM_NAME" 0 remap \$REMAP_SECTOR 2>/dev/null; then
                    operations=\$((operations + 1))
                else
                    errors=\$((errors + 1))
                fi
            fi
            ;;
    esac
    
    # Update stats every 100 operations
    if [ \$((operations % 100)) -eq 0 ]; then
        echo "\$operations" > "\$STATS_FILE"
        echo "\$errors" > "\$ERROR_FILE"
    fi
    
    # Small delay to prevent overwhelming the system
    sleep 0.001
done

# Final stats update
echo "\$operations" > "\$STATS_FILE"
echo "\$errors" > "\$ERROR_FILE"
echo "Worker \$WORKER_ID completed: \$operations operations, \$errors errors"
EOF
    
    chmod +x "$worker_file"
    echo "$worker_file"
}

# Step 5: Launch concurrent stress workers
echo "=== Step 5: Launching $NUM_PROCESSES concurrent stress workers ==="

WORKER_PIDS=()
for i in $(seq 1 $NUM_PROCESSES); do
    WORKER_SCRIPT=$(create_stress_worker $i)
    bash "$WORKER_SCRIPT" &
    WORKER_PIDS+=($!)
    echo "✅ Started stress worker $i (PID: ${WORKER_PIDS[-1]})"
done

echo "✅ All $NUM_PROCESSES stress workers launched"

# Step 6: Monitor stress test progress
echo "=== Step 6: Monitoring stress test progress ==="
echo "Running stress test for ${TEST_DURATION} seconds..."

START_TIME=$(date +%s)
MONITOR_INTERVAL=5

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    
    if [ $ELAPSED -ge $TEST_DURATION ]; then
        break
    fi
    
    # Collect stats from all workers
    TOTAL_OPS=0
    TOTAL_ERRORS=0
    
    for i in $(seq 1 $NUM_PROCESSES); do
        if [ -f "$TEST_DIR/worker_${i}_stats.txt" ]; then
            OPS=$(cat "$TEST_DIR/worker_${i}_stats.txt" 2>/dev/null || echo "0")
            ERRORS=$(cat "$TEST_DIR/worker_${i}_errors.txt" 2>/dev/null || echo "0")
            TOTAL_OPS=$((TOTAL_OPS + OPS))
            TOTAL_ERRORS=$((TOTAL_ERRORS + ERRORS))
        fi
    done
    
    REMAINING=$((TEST_DURATION - ELAPSED))
    echo "⏱️  Progress: ${ELAPSED}s/${TEST_DURATION}s | Operations: $TOTAL_OPS | Errors: $TOTAL_ERRORS | Remaining: ${REMAINING}s"
    
    # Check system health
    LOAD_AVG=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
    MEM_FREE=$(grep MemAvailable /proc/meminfo | awk '{print int($2/1024)}')
    echo "📊 System: Load=${LOAD_AVG} | Free Memory=${MEM_FREE}MB"
    
    sleep $MONITOR_INTERVAL
done

# Step 7: Stop workers and collect results
echo "=== Step 7: Stopping workers and collecting results ==="
touch "$TEST_DIR/stop_workers"  # Signal workers to stop

# Wait for workers to finish gracefully
echo "Waiting for workers to finish gracefully..."
sleep 3

# Force kill any remaining workers
for pid in "${WORKER_PIDS[@]}"; do
    if kill -0 $pid 2>/dev/null; then
        kill $pid 2>/dev/null || true
    fi
done

# Collect final statistics
echo "=== Step 8: Final Statistics ==="
TOTAL_OPERATIONS=0
TOTAL_ERRORS=0
WORKER_STATS=()

for i in $(seq 1 $NUM_PROCESSES); do
    if [ -f "$TEST_DIR/worker_${i}_stats.txt" ] && [ -f "$TEST_DIR/worker_${i}_errors.txt" ]; then
        OPS=$(cat "$TEST_DIR/worker_${i}_stats.txt")
        ERRORS=$(cat "$TEST_DIR/worker_${i}_errors.txt")
        TOTAL_OPERATIONS=$((TOTAL_OPERATIONS + OPS))
        TOTAL_ERRORS=$((TOTAL_ERRORS + ERRORS))
        WORKER_STATS+=("Worker $i: $OPS ops, $ERRORS errors")
        echo "✅ Worker $i: $OPS operations, $ERRORS errors"
    else
        echo "⚠️  Worker $i: stats file missing"
    fi
done

# Step 9: System stability verification
echo "=== Step 9: System stability verification ==="

# Check if target is still responsive
if sudo dmsetup status "$DM_NAME" >/dev/null 2>&1; then
    STATUS=$(sudo dmsetup status "$DM_NAME")
    echo "✅ Target still responsive: $STATUS"
else
    echo "❌ Target not responsive after stress test"
fi

# Test basic I/O after stress
echo "Testing basic I/O functionality after stress..."
TEST_DATA="post-stress-test-data"
if echo -n "$TEST_DATA" | sudo dd of="/dev/mapper/$DM_NAME" bs=32 seek=999999 count=1 conv=notrunc 2>/dev/null; then
    READ_DATA=$(sudo dd if="/dev/mapper/$DM_NAME" bs=32 skip=999999 count=1 2>/dev/null | tr -d '\0')
    if [ "$READ_DATA" = "$TEST_DATA" ]; then
        echo "✅ Basic I/O still working after stress test"
    else
        echo "❌ Basic I/O corrupted after stress test"
    fi
else
    echo "❌ Basic I/O failed after stress test"
fi

# Check kernel logs for errors
echo "Checking kernel logs for errors..."
ERROR_COUNT=$(sudo dmesg | grep -i "error\|panic\|oops\|warn.*dm-remap" | wc -l)
if [ $ERROR_COUNT -eq 0 ]; then
    echo "✅ No kernel errors detected"
else
    echo "⚠️  Found $ERROR_COUNT potential kernel errors/warnings"
    sudo dmesg | grep -i "error\|panic\|oops\|warn.*dm-remap" | tail -5
fi

# Step 10: Performance impact analysis
echo "=== Step 10: Performance impact analysis ==="
OPS_PER_SECOND=$(echo "scale=2; $TOTAL_OPERATIONS / $TEST_DURATION" | bc)
ERROR_RATE=$(echo "scale=4; $TOTAL_ERRORS * 100 / $TOTAL_OPERATIONS" | bc 2>/dev/null || echo "0")

echo "📊 Stress Test Results Summary:"
echo "   • Duration: ${TEST_DURATION} seconds"
echo "   • Concurrent workers: $NUM_PROCESSES"
echo "   • Total operations: $TOTAL_OPERATIONS"
echo "   • Total errors: $TOTAL_ERRORS"
echo "   • Operations per second: $OPS_PER_SECOND"
echo "   • Error rate: ${ERROR_RATE}%"
echo "   • Average per worker: $(echo "scale=1; $TOTAL_OPERATIONS / $NUM_PROCESSES" | bc) operations"

# Stability assessment
if [ $TOTAL_ERRORS -eq 0 ] && [ $ERROR_COUNT -eq 0 ]; then
    echo "🎉 EXCELLENT: Module passed stress test with no errors"
elif [ $TOTAL_ERRORS -lt $((TOTAL_OPERATIONS / 1000)) ]; then
    echo "✅ GOOD: Module stable with minimal error rate (<0.1%)"
elif [ $TOTAL_ERRORS -lt $((TOTAL_OPERATIONS / 100)) ]; then
    echo "⚠️  ACCEPTABLE: Module mostly stable with low error rate (<1%)"
else
    echo "❌ CONCERNING: High error rate (>1%) - needs investigation"
fi

echo "✅ Concurrent stress testing completed"