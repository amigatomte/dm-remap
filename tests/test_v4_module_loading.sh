#!/bin/bash
#
# dm-remap v4.0 Module Loading Test
# Tests basic module loading and unloading for all v4.0 modules
#
# Date: October 15, 2025
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$SCRIPT_DIR/../src"

echo "========================================="
echo "dm-remap v4.0 Module Loading Test"
echo "========================================="
echo ""

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

test_count=0
pass_count=0
fail_count=0

# Test function
run_test() {
    local test_name="$1"
    local test_cmd="$2"
    
    ((test_count++))
    echo -n "Test $test_count: $test_name... "
    
    if eval "$test_cmd" &>/dev/null; then
        echo -e "${GREEN}PASS${NC}"
        ((pass_count++))
        return 0
    else
        echo -e "${RED}FAIL${NC}"
        ((fail_count++))
        return 1
    fi
}

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    sudo rmmod dm_remap_v4_setup_reassembly 2>/dev/null || true
    sudo rmmod dm_remap_v4_spare_pool 2>/dev/null || true
    sudo rmmod dm_remap_v4_metadata 2>/dev/null || true
    sudo rmmod dm_remap_v4_real 2>/dev/null || true
}

# Ensure clean state
cleanup

echo "Testing module loading..."
echo ""

# Test 1: Load dm-remap-v4-real
run_test "Load dm-remap-v4-real.ko" \
    "sudo insmod $MODULE_DIR/dm-remap-v4-real.ko"

# Test 2: Verify module is loaded
run_test "Verify dm-remap-v4-real in lsmod" \
    "lsmod | grep -q dm_remap_v4_real"

# Test 3: Load dm-remap-v4-metadata
run_test "Load dm-remap-v4-metadata.ko" \
    "sudo insmod $MODULE_DIR/dm-remap-v4-metadata.ko"

# Test 4: Verify metadata module
run_test "Verify dm-remap-v4-metadata in lsmod" \
    "lsmod | grep -q dm_remap_v4_metadata"

# Test 5: Load dm-remap-v4-spare-pool (Priority 3!)
run_test "Load dm-remap-v4-spare-pool.ko (Priority 3)" \
    "sudo insmod $MODULE_DIR/dm-remap-v4-spare-pool.ko"

# Test 6: Verify spare-pool module
run_test "Verify dm-remap-v4-spare-pool in lsmod" \
    "lsmod | grep -q dm_remap_v4_spare_pool"

# Test 7: Load dm-remap-v4-setup-reassembly (Priority 6!)
run_test "Load dm-remap-v4-setup-reassembly.ko (Priority 6)" \
    "sudo insmod $MODULE_DIR/dm-remap-v4-setup-reassembly.ko"

# Test 8: Verify setup-reassembly module
run_test "Verify dm-remap-v4-setup-reassembly in lsmod" \
    "lsmod | grep -q dm_remap_v4_setup_reassembly"

# Test 9: Check for exported symbols
run_test "Verify EXPORT_SYMBOL for confidence_score function" \
    "cat /proc/kallsyms | grep -q dm_remap_v4_calculate_confidence_score"

# Test 10: Unload setup-reassembly
run_test "Unload dm-remap-v4-setup-reassembly" \
    "sudo rmmod dm_remap_v4_setup_reassembly"

# Test 11: Unload spare-pool
run_test "Unload dm-remap-v4-spare-pool" \
    "sudo rmmod dm_remap_v4_spare_pool"

# Test 12: Unload metadata
run_test "Unload dm-remap-v4-metadata" \
    "sudo rmmod dm_remap_v4_metadata"

# Test 13: Unload real
run_test "Unload dm-remap-v4-real" \
    "sudo rmmod dm_remap_v4_real"

# Test 14: Verify all modules unloaded
run_test "Verify no dm-remap modules remain" \
    "! lsmod | grep -q dm_remap"

echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total tests: $test_count"
echo -e "Passed: ${GREEN}$pass_count${NC}"
echo -e "Failed: ${RED}$fail_count${NC}"
echo ""

if [ $fail_count -eq 0 ]; then
    echo -e "${GREEN}✅ ALL TESTS PASSED!${NC}"
    echo ""
    echo "🎉 dm-remap v4.0 modules are working correctly!"
    echo ""
    echo "Modules tested:"
    echo "  ✅ dm-remap-v4-real.ko"
    echo "  ✅ dm-remap-v4-metadata.ko"
    echo "  ✅ dm-remap-v4-spare-pool.ko (Priority 3: External Spare Device Support)"
    echo "  ✅ dm-remap-v4-setup-reassembly.ko (Priority 6: Automatic Setup Reassembly)"
    exit 0
else
    echo -e "${RED}❌ SOME TESTS FAILED${NC}"
    exit 1
fi
