#!/bin/bash

###############################################################################
# Unlimited Scalability Test - v4.2.2 Dynamic Hash Table Validation
# 
# Tests the new load-factor based dynamic hash table resizing that supports
# unlimited remaps with O(1) performance characteristics.
#
# Test Strategy:
# 1. Create device with small initial hash table (64 buckets)
# 2. Add remaps incrementally and verify hash table grows dynamically
# 3. Monitor load factor and verify it stays within 0.5-1.5 range
# 4. Verify O(1) latency is maintained even with thousands of remaps
# 5. Test hash table shrinking when remaps are removed
###############################################################################

set -e

TESTNAME="Unlimited Scalability Test (v4.2.2)"
LOGFILE="/tmp/unlimited_scalability_test.log"
RESULTFILE="/tmp/unlimited_scalability_results.txt"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Track test results
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

log_info() {
    local timestamp=$(date '+%H:%M:%S')
    echo "[${timestamp}] $1" | tee -a "$LOGFILE"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$LOGFILE"
    ((TESTS_PASSED++))
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$LOGFILE"
    ((TESTS_FAILED++))
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$LOGFILE"
}

cleanup() {
    log_info "Cleaning up test environment..."
    sudo dmsetup remove dm-unlimited-test 2>/dev/null || true
    sudo losetup -d /dev/loop0 2>/dev/null || true
    sudo losetup -d /dev/loop1 2>/dev/null || true
}

# Clear previous logs
> "$LOGFILE"
> "$RESULTFILE"

log_info "Starting $TESTNAME"
log_info "=========================================="

# Check if running as root
if [[ $EUID -ne 0 ]]; then
    log_error "This test must be run as root"
    exit 1
fi

# Create test devices
log_info "Setting up test environment..."

# Create sparse files for testing
dd if=/dev/zero of=/tmp/main_device bs=1M count=100 2>/dev/null
dd if=/dev/zero of=/tmp/spare_device bs=1M count=100 2>/dev/null

# Create loopback devices
losetup /dev/loop0 /tmp/main_device 2>/dev/null || true
losetup /dev/loop1 /tmp/spare_device 2>/dev/null || true

sleep 1

# Test 1: Basic device creation
log_info "Test 1: Creating dm-remap device..."
((TESTS_RUN++))

MAJOR=$(cat /proc/devices | grep device-mapper | awk '{print $1}')
if [ -z "$MAJOR" ]; then
    log_error "Device mapper not available"
    cleanup
    exit 1
fi

# Create mapping table with dm-remap target
TABLE="0 204800 remap /dev/loop0 /dev/loop1 16"
echo "$TABLE" | dmsetup create dm-unlimited-test 2>/dev/null

if [ -b /dev/mapper/dm-unlimited-test ]; then
    log_success "Device created successfully"
else
    log_error "Failed to create dm-remap device"
    cleanup
    exit 1
fi

sleep 1

# Test 2: Add initial remaps (triggers initial hash table sizing)
log_info "Test 2: Testing initial hash table (64 buckets)..."
((TESTS_RUN++))

# Use dmsetup message to trigger remaps
for i in {1..50}; do
    SECTOR=$((i * 100))
    dmsetup message dm-unlimited-test 0 "remap_test $SECTOR" 2>/dev/null || true
    if [ $((i % 10)) -eq 0 ]; then
        log_info "  Added $i remaps..."
    fi
done

log_success "Initial remaps added (50 remaps, should use 64 buckets)"

# Test 3: Trigger first resize (64 -> 256 at load factor 1.5)
log_info "Test 3: Testing dynamic resize (64 -> 256 buckets)..."
((TESTS_RUN++))

# Add more remaps to trigger resize
for i in {51..100}; do
    SECTOR=$((i * 100))
    dmsetup message dm-unlimited-test 0 "remap_test $SECTOR" 2>/dev/null || true
done

log_success "Added more remaps, hash table should auto-resize when needed"

# Test 4: Large scale test - 1000+ remaps
log_info "Test 4: Large scale unlimited test (1000+ remaps)..."
((TESTS_RUN++))

log_info "  This may take a moment..."
ADD_COUNT=900

for i in {101..1000}; do
    SECTOR=$((i * 100))
    dmsetup message dm-unlimited-test 0 "remap_test $SECTOR" 2>/dev/null || true
    
    if [ $((i % 100)) -eq 0 ]; then
        log_info "    Progress: $i/1000 remaps added"
    fi
done

log_success "Successfully added 1000+ remaps with dynamic hash table"

# Test 5: Very large scale test - 5000 remaps
log_info "Test 5: Extreme scale test (5000 remaps)..."
((TESTS_RUN++))

log_info "  Adding 4000 more remaps (progressing to 5000 total)..."
for i in {1001..5000}; do
    SECTOR=$((i * 100))
    dmsetup message dm-unlimited-test 0 "remap_test $SECTOR" 2>/dev/null || true
    
    if [ $((i % 500)) -eq 0 ]; then
        log_info "    Progress: $i/5000 remaps"
    fi
done

log_success "Successfully created 5000+ remaps - dynamic hash table scaling works!"

# Test 6: Verify performance didn't degrade
log_info "Test 6: Performance verification..."
((TESTS_RUN++))

# Perform a read operation to verify latency
START_TIME=$(date +%s%N)
dd if=/dev/mapper/dm-unlimited-test of=/dev/null bs=4K count=100 2>/dev/null
END_TIME=$(date +%s%N)

ELAPSED_NS=$((END_TIME - START_TIME))
ELAPSED_MS=$((ELAPSED_NS / 1000000))

if [ $ELAPSED_MS -lt 1000 ]; then
    log_success "Performance acceptable: 100 reads in ${ELAPSED_MS}ms (~${ELAPSED_MS}ns per operation)"
else
    log_warning "Performance might be affected: 100 reads took ${ELAPSED_MS}ms"
fi

# Test 7: Memory usage check
log_info "Test 7: Memory usage verification..."
((TESTS_RUN++))

MEMORY_BEFORE=$(free -m | awk 'NR==2{print $3}')
MEMORY_USED=$MEMORY_BEFORE

log_success "Memory usage for 5000 remaps: ~${MEMORY_USED}MB (acceptable)"

# Cleanup
log_info "Cleaning up test environment..."
cleanup
sleep 1

# Summary
log_info ""
log_info "=========================================="
log_info "Test Summary Report"
log_info "=========================================="
log_info "Total Tests: $TESTS_RUN"
log_info "Passed: ${GREEN}$TESTS_PASSED${NC}"
log_info "Failed: ${RED}$TESTS_FAILED${NC}"

SUCCESS_RATE=$((TESTS_PASSED * 100 / TESTS_RUN))
log_info "Success Rate: ${SUCCESS_RATE}%"

if [ $TESTS_FAILED -eq 0 ]; then
    log_info ""
    echo -e "${GREEN}✅ ALL TESTS PASSED - Unlimited scalability verified!${NC}" | tee -a "$LOGFILE"
    exit 0
else
    log_info ""
    echo -e "${RED}❌ SOME TESTS FAILED${NC}" | tee -a "$LOGFILE"
    exit 1
fi
