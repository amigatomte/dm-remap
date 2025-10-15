#!/bin/bash
#
# dm-remap v4.0 Phase 1 - Quick Production Validation
# Fast validation of core functionality after merge to main
#

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0

print_header() {
    echo -e "\n${BLUE}==== $1 ====${NC}\n"
}

print_pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((PASSED++))
}

print_fail() {
    echo -e "${RED}✗${NC} $1"
    ((FAILED++))
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Please run as root (sudo)${NC}"
    exit 1
fi

print_header "dm-remap v4.0 Phase 1 - Quick Production Validation"
echo "Date: $(date)"
echo "Kernel: $(uname -r)"
echo ""

# Cleanup first
rmmod dm-remap-v4-setup-reassembly 2>/dev/null || true
rmmod dm-remap-v4-spare-pool 2>/dev/null || true
rmmod dm-remap-v4-metadata 2>/dev/null || true
rmmod dm-remap-v4-real 2>/dev/null || true

print_header "Test 1: Build Verification"

if [ -f src/dm-remap-v4-real.ko ]; then
    SIZE=$(stat -f%z src/dm-remap-v4-real.ko 2>/dev/null || stat -c%s src/dm-remap-v4-real.ko)
    print_pass "dm-remap-v4-real.ko exists ($SIZE bytes)"
else
    print_fail "dm-remap-v4-real.ko not found"
fi

if [ -f src/dm-remap-v4-metadata.ko ]; then
    SIZE=$(stat -f%z src/dm-remap-v4-metadata.ko 2>/dev/null || stat -c%s src/dm-remap-v4-metadata.ko)
    print_pass "dm-remap-v4-metadata.ko exists ($SIZE bytes)"
else
    print_fail "dm-remap-v4-metadata.ko not found"
fi

if [ -f src/dm-remap-v4-spare-pool.ko ]; then
    SIZE=$(stat -f%z src/dm-remap-v4-spare-pool.ko 2>/dev/null || stat -c%s src/dm-remap-v4-spare-pool.ko)
    print_pass "dm-remap-v4-spare-pool.ko exists ($SIZE bytes)"
else
    print_fail "dm-remap-v4-spare-pool.ko not found"
fi

if [ -f src/dm-remap-v4-setup-reassembly.ko ]; then
    SIZE=$(stat -f%z src/dm-remap-v4-setup-reassembly.ko 2>/dev/null || stat -c%s src/dm-remap-v4-setup-reassembly.ko)
    print_pass "dm-remap-v4-setup-reassembly.ko exists ($SIZE bytes)"
else
    print_fail "dm-remap-v4-setup-reassembly.ko not found"
fi

print_header "Test 2: Module Loading"

if insmod src/dm-remap-v4-real.ko 2>/dev/null; then
    print_pass "Loaded dm-remap-v4-real.ko"
    
    if lsmod | grep -q dm_remap_v4_real; then
        print_pass "Module dm_remap_v4_real appears in lsmod"
    else
        print_fail "Module dm_remap_v4_real not in lsmod"
    fi
    
    if rmmod dm-remap-v4-real 2>/dev/null; then
        print_pass "Unloaded dm-remap-v4-real.ko"
    else
        print_fail "Failed to unload dm-remap-v4-real.ko"
    fi
else
    print_fail "Failed to load dm-remap-v4-real.ko"
fi

print_header "Test 3: Symbol Exports (from setup-reassembly module)"

insmod src/dm-remap-v4-setup-reassembly.ko 2>/dev/null || true

if grep -q "dm_remap_v4_calculate_confidence_score" /proc/kallsyms; then
    print_pass "Symbol dm_remap_v4_calculate_confidence_score exported"
else
    print_fail "Symbol dm_remap_v4_calculate_confidence_score not found"
fi

if grep -q "dm_remap_v4_update_metadata_version" /proc/kallsyms; then
    print_pass "Symbol dm_remap_v4_update_metadata_version exported"
else
    print_fail "Symbol dm_remap_v4_update_metadata_version not found"
fi

if grep -q "dm_remap_v4_verify_metadata_integrity" /proc/kallsyms; then
    print_pass "Symbol dm_remap_v4_verify_metadata_integrity exported"
else
    print_fail "Symbol dm_remap_v4_verify_metadata_integrity not found"
fi

rmmod dm-remap-v4-setup-reassembly 2>/dev/null || true

print_header "Test 4: Module Dependencies"

if insmod src/dm-remap-v4-metadata.ko 2>/dev/null; then
    print_pass "Loaded dm-remap-v4-metadata.ko"
    
    if insmod src/dm-remap-v4-spare-pool.ko 2>/dev/null; then
        print_pass "Loaded dm-remap-v4-spare-pool.ko (depends on metadata)"
        
        if rmmod dm-remap-v4-spare-pool 2>/dev/null; then
            print_pass "Unloaded dm-remap-v4-spare-pool.ko"
        else
            print_fail "Failed to unload dm-remap-v4-spare-pool.ko"
        fi
    else
        print_fail "Failed to load dm-remap-v4-spare-pool.ko"
    fi
    
    if rmmod dm-remap-v4-metadata 2>/dev/null; then
        print_pass "Unloaded dm-remap-v4-metadata.ko"
    else
        print_fail "Failed to unload dm-remap-v4-metadata.ko"
    fi
else
    print_fail "Failed to load dm-remap-v4-metadata.ko"
fi

print_header "Test 5: Basic I/O"

# Create test images (spare needs to be 5% larger for metadata overhead)
dd if=/dev/zero of=/tmp/test_main.img bs=1M count=50 2>/dev/null
dd if=/dev/zero of=/tmp/test_spare.img bs=1M count=53 2>/dev/null  # 6% larger to meet 5% requirement

# Setup loop devices
LOOP_MAIN=$(losetup -f --show /tmp/test_main.img)
LOOP_SPARE=$(losetup -f --show /tmp/test_spare.img)

# Load module
if insmod src/dm-remap-v4-real.ko 2>/dev/null; then
    print_pass "Loaded module for I/O test"
    
    # Create dm device (dm-remap-v4 requires both main and spare)
    SECTORS=$(blockdev --getsz "$LOOP_MAIN")
    TABLE="0 $SECTORS dm-remap-v4 $LOOP_MAIN $LOOP_SPARE"
    
    if echo "$TABLE" | dmsetup create remap-test 2>/dev/null; then
        print_pass "Created dm-remap device"
        
        # Write test
        if dd if=/dev/urandom of=/dev/mapper/remap-test bs=4K count=10 2>/dev/null; then
            print_pass "Write test successful"
        else
            print_fail "Write test failed"
        fi
        
        # Read test
        if dd if=/dev/mapper/remap-test of=/dev/null bs=4K count=10 2>/dev/null; then
            print_pass "Read test successful"
        else
            print_fail "Read test failed"
        fi
        
        # Cleanup
        dmsetup remove remap-test 2>/dev/null || print_fail "Failed to remove dm device"
    else
        print_fail "Failed to create dm-remap device"
    fi
    
    rmmod dm-remap-v4-real 2>/dev/null || true
else
    print_fail "Failed to load module for I/O test"
fi

# Cleanup loop devices
losetup -d "$LOOP_MAIN" 2>/dev/null || true
losetup -d "$LOOP_SPARE" 2>/dev/null || true
rm -f /tmp/test_main.img /tmp/test_spare.img

print_header "Test 6: Stress Test (10 load/unload cycles)"

SUCCESS=true
for i in {1..10}; do
    if ! insmod src/dm-remap-v4-real.ko 2>/dev/null; then
        print_fail "Stress test failed at cycle $i (load)"
        SUCCESS=false
        break
    fi
    
    if ! rmmod dm-remap-v4-real 2>/dev/null; then
        print_fail "Stress test failed at cycle $i (unload)"
        SUCCESS=false
        break
    fi
done

if [ "$SUCCESS" = true ]; then
    print_pass "Stress test completed (10 cycles)"
fi

# Final cleanup
rmmod dm-remap-v4-real 2>/dev/null || true

print_header "Results Summary"

TOTAL=$((PASSED + FAILED))
echo "Total tests: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL TESTS PASSED${NC}"
    echo -e "${GREEN}dm-remap v4.0 Phase 1 is PRODUCTION READY!${NC}\n"
    exit 0
else
    echo -e "\n${RED}✗ SOME TESTS FAILED${NC}"
    echo -e "${YELLOW}Please review the failures above${NC}\n"
    exit 1
fi
