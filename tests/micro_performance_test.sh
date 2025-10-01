#!/bin/bash
#
# micro_performance_test.sh - Micro-benchmark for dm-remap fast path
#

set -e

echo "=== DM-REMAP v2.0 MICRO-PERFORMANCE TEST ==="

# Create very minimal test setup
cleanup() {
    sudo dmsetup remove micro-test 2>/dev/null || true
    sudo losetup -d /dev/loop40 2>/dev/null || true
    sudo losetup -d /dev/loop41 2>/dev/null || true
    rm -f /tmp/micro_main.img /tmp/micro_spare.img /tmp/micro_data
}

trap cleanup EXIT

echo "Setting up minimal test environment..."

# Create tiny devices for minimal overhead
dd if=/dev/zero of=/tmp/micro_main.img bs=1M count=1 2>/dev/null
dd if=/dev/zero of=/tmp/micro_spare.img bs=1M count=1 2>/dev/null
dd if=/dev/urandom of=/tmp/micro_data bs=512 count=1 2>/dev/null

sudo losetup /dev/loop40 /tmp/micro_main.img
sudo losetup /dev/loop41 /tmp/micro_spare.img

MAIN_SECTORS=$(sudo blockdev --getsz /dev/loop40)

echo "0 $MAIN_SECTORS remap /dev/loop40 /dev/loop41 0 100" | sudo dmsetup create micro-test

echo "Testing with 100 very small I/Os (512 bytes each)..."

# Use direct I/O to minimize cache effects
start_time=$(date +%s%N)

for i in {1..100}; do
    sudo dd if=/tmp/micro_data of=/dev/mapper/micro-test bs=512 seek=$i count=1 conv=notrunc,fdatasync 2>/dev/null
done

end_time=$(date +%s%N)

total_time_ns=$((end_time - start_time))
avg_time_ns=$((total_time_ns / 100))

echo "Micro-benchmark Results:"
echo "  Total time: ${total_time_ns}ns"
echo "  Average per I/O: ${avg_time_ns}ns"

if [[ $avg_time_ns -lt 1000000 ]]; then
    echo "✅ EXCELLENT! Fast path performance target achieved (${avg_time_ns}ns < 1ms)"
elif [[ $avg_time_ns -lt 5000000 ]]; then
    echo "✅ GOOD! Performance is acceptable (${avg_time_ns}ns < 5ms)"
else
    echo "⚠️ Performance needs further optimization (${avg_time_ns}ns)"
fi

# Test without sync for raw performance
echo ""
echo "Testing without sync (raw performance)..."

start_time=$(date +%s%N)

for i in {1..100}; do
    sudo dd if=/tmp/micro_data of=/dev/mapper/micro-test bs=512 seek=$((i + 200)) count=1 conv=notrunc 2>/dev/null
done

end_time=$(date +%s%N)

total_time_ns=$((end_time - start_time))
avg_time_ns=$((total_time_ns / 100))

echo "Raw performance Results:"
echo "  Total time: ${total_time_ns}ns"
echo "  Average per I/O: ${avg_time_ns}ns"

if [[ $avg_time_ns -lt 100000 ]]; then
    echo "🚀 OUTSTANDING! Raw performance is excellent (${avg_time_ns}ns < 100µs)"
elif [[ $avg_time_ns -lt 1000000 ]]; then
    echo "✅ EXCELLENT! Raw performance target achieved (${avg_time_ns}ns < 1ms)"
else
    echo "⚠️ Raw performance needs optimization (${avg_time_ns}ns)"
fi

echo "Micro-benchmark completed!"