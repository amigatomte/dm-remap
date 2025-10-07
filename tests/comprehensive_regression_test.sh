#!/bin/bash

# dm-remap v4.0 Comprehensive Regression Test Suite
# Full system validation before Phase 2 development
# Tests all features: health scanning, memory pools, hotpath optimization

set -e

# Test configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REGRESSION_LOG="${SCRIPT_DIR}/regression_test_results.log"
TEST_RESULTS_DIR="${SCRIPT_DIR}/regression_results"
LOOP0_SIZE=200M
LOOP1_SIZE=100M

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date '+%H:%M:%S')]${NC} $1" | tee -a "$REGRESSION_LOG"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$REGRESSION_LOG"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$REGRESSION_LOG"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$REGRESSION_LOG"
}

header() {
    echo -e "${CYAN}============================================${NC}" | tee -a "$REGRESSION_LOG"
    echo -e "${CYAN}$1${NC}" | tee -a "$REGRESSION_LOG"
    echo -e "${CYAN}============================================${NC}" | tee -a "$REGRESSION_LOG"
}

section() {
    echo -e "${MAGENTA}--- $1 ---${NC}" | tee -a "$REGRESSION_LOG"
}

# Global test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
WARNINGS=0

# Track test result
track_test() {
    local test_name="$1"
    local result="$2"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    case "$result" in
        "PASS")
            PASSED_TESTS=$((PASSED_TESTS + 1))
            success "Test $TOTAL_TESTS: $test_name - PASSED"
            ;;
        "FAIL")
            FAILED_TESTS=$((FAILED_TESTS + 1))
            error "Test $TOTAL_TESTS: $test_name - FAILED"
            ;;
        "WARN")
            WARNINGS=$((WARNINGS + 1))
            warning "Test $TOTAL_TESTS: $test_name - WARNING"
            ;;
    esac
}

# Cleanup function
cleanup() {
    log "Cleaning up regression test environment..."
    
    # Remove all test dm devices
    for device in dm-regression-test dm-stress-test dm-health-test dm-memory-test; do
        dmsetup remove "$device" 2>/dev/null || true
    done
    
    # Detach all loop devices used in testing
    for loop in /dev/loop{30..39}; do
        losetup -d "$loop" 2>/dev/null || true
    done
    
    # Remove temporary files
    rm -f /tmp/regression_*.img
    
    log "Regression test cleanup completed"
}

# Setup signal handlers
trap cleanup EXIT
trap cleanup INT
trap cleanup TERM

# Initialize regression test environment
init_regression_tests() {
    header "dm-remap v4.0 Comprehensive Regression Test Suite"
    
    # Clear previous log and create results directory
    > "$REGRESSION_LOG"
    mkdir -p "$TEST_RESULTS_DIR"
    
    # Check if we're running as root
    if [[ $EUID -ne 0 ]]; then
        error "This regression test suite must be run as root"
        exit 1
    fi
    
    # Check if dm-remap module is loaded
    if ! lsmod | grep -q dm_remap; then
        log "Loading dm-remap module..."
        cd "${SCRIPT_DIR}/../src"
        if ! insmod dm_remap.ko; then
            error "Failed to load dm-remap module"
            exit 1
        fi
    fi
    
    # Verify module version and features
    log "Verifying module information..."
    modinfo dm_remap | head -15 | tee -a "$REGRESSION_LOG"
    
    success "Regression test environment initialized"
}

# Test 1: Module Loading and Basic Functionality
test_module_loading() {
    section "Module Loading and Basic Functionality"
    
    # Test module is loaded
    if lsmod | grep -q dm_remap; then
        track_test "Module Loading" "PASS"
    else
        track_test "Module Loading" "FAIL"
        return 1
    fi
    
    # Check module parameters
    local param_files=("/sys/module/dm_remap/parameters/debug_level" 
                      "/sys/module/dm_remap/parameters/max_remaps"
                      "/sys/module/dm_remap/parameters/error_threshold")
    
    for param_file in "${param_files[@]}"; do
        if [[ -r "$param_file" ]]; then
            local param_name=$(basename "$param_file")
            local param_value=$(cat "$param_file")
            log "Parameter $param_name: $param_value"
            track_test "Parameter $param_name" "PASS"
        else
            track_test "Parameter $(basename "$param_file")" "FAIL"
        fi
    done
}

# Test 2: Device Creation and Basic I/O
test_device_creation() {
    section "Device Creation and Basic I/O"
    
    # Create test device files
    log "Creating test device files..."
    dd if=/dev/zero of=/tmp/regression_main.img bs=1M count=${LOOP0_SIZE%M} 2>/dev/null
    dd if=/dev/zero of=/tmp/regression_spare.img bs=1M count=${LOOP1_SIZE%M} 2>/dev/null
    
    # Setup loop devices
    losetup /dev/loop30 /tmp/regression_main.img
    losetup /dev/loop31 /tmp/regression_spare.img
    
    # Create dm-remap device
    local spare_sectors=$(blockdev --getsz /dev/loop31)
    if echo "0 $(blockdev --getsz /dev/loop30) remap /dev/loop30 /dev/loop31 0 $spare_sectors" | \
       dmsetup create dm-regression-test; then
        track_test "Device Creation" "PASS"
    else
        track_test "Device Creation" "FAIL"
        return 1
    fi
    
    # Verify device is active
    if dmsetup info dm-regression-test | grep -q "State.*ACTIVE"; then
        track_test "Device State" "PASS"
    else
        track_test "Device State" "FAIL"
    fi
    
    # Test basic read operation
    if dd if=/dev/mapper/dm-regression-test of=/dev/null bs=1M count=1 2>/dev/null; then
        track_test "Basic Read I/O" "PASS"
    else
        track_test "Basic Read I/O" "FAIL"
    fi
    
    # Test basic write operation
    if dd if=/dev/zero of=/dev/mapper/dm-regression-test bs=1M count=1 2>/dev/null; then
        track_test "Basic Write I/O" "PASS"
    else
        track_test "Basic Write I/O" "FAIL"
    fi
}

# Test 3: Health Scanning System Regression
test_health_scanning() {
    section "Health Scanning System Regression"
    
    # Check if health scanning initialized
    if dmesg | tail -50 | grep -q "Health scanner initialized successfully"; then
        track_test "Health Scanner Initialization" "PASS"
    else
        track_test "Health Scanner Initialization" "WARN"
    fi
    
    # Check if background health scanning started
    if dmesg | tail -50 | grep -q "Background health scanning started"; then
        track_test "Background Health Scanning" "PASS"
    else
        track_test "Background Health Scanning" "WARN"
    fi
    
    # Test health map creation
    if dmesg | tail -50 | grep -q "Health map initialized"; then
        track_test "Health Map Creation" "PASS"
    else
        track_test "Health Map Creation" "FAIL"
    fi
    
    # Check health sysfs interface (if available)
    local dm_minor=$(dmsetup info dm-regression-test | grep "Minor:" | awk '{print $2}')
    local health_dir="/sys/block/dm-${dm_minor}/dm-remap/health"
    
    if [[ -d "$health_dir" ]]; then
        track_test "Health Sysfs Interface" "PASS"
        
        # Test reading health statistics
        local health_files=("health_stats" "scan_progress" "error_count")
        for health_file in "${health_files[@]}"; do
            if [[ -r "${health_dir}/${health_file}" ]]; then
                track_test "Health File $health_file" "PASS"
            else
                track_test "Health File $health_file" "WARN"
            fi
        done
    else
        track_test "Health Sysfs Interface" "WARN"
    fi
}

# Test 4: Memory Pool System Regression
test_memory_pools() {
    section "Memory Pool System Regression"
    
    # Check memory pool initialization messages
    local pool_types=(0 1 2 3)
    for pool_type in "${pool_types[@]}"; do
        if dmesg | tail -50 | grep -q "Pool type $pool_type initialized"; then
            track_test "Memory Pool Type $pool_type" "PASS"
        else
            track_test "Memory Pool Type $pool_type" "FAIL"
        fi
    done
    
    # Check memory pool manager initialization
    if dmesg | tail -50 | grep -q "Memory pool manager initialized successfully"; then
        track_test "Memory Pool Manager" "PASS"
    else
        track_test "Memory Pool Manager" "FAIL"
    fi
    
    # Check memory pool system overall
    if dmesg | tail -50 | grep -q "Memory pool system initialized successfully"; then
        track_test "Memory Pool System" "PASS"
    else
        track_test "Memory Pool System" "FAIL"
    fi
    
    # Test memory pool statistics (if sysfs available)
    local dm_minor=$(dmsetup info dm-regression-test | grep "Minor:" | awk '{print $2}')
    local pools_dir="/sys/block/dm-${dm_minor}/dm-remap/memory_pools"
    
    if [[ -d "$pools_dir" ]]; then
        track_test "Memory Pool Sysfs" "PASS"
    else
        track_test "Memory Pool Sysfs" "WARN"
    fi
}

# Test 5: Hotpath Optimization Regression
test_hotpath_optimization() {
    section "Hotpath Optimization Regression"
    
    # Check hotpath initialization
    if dmesg | tail -50 | grep -q "Hotpath optimization initialized successfully"; then
        track_test "Hotpath Initialization" "PASS"
    else
        track_test "Hotpath Initialization" "FAIL"
    fi
    
    # Test hotpath sysfs interface
    local dm_minor=$(dmsetup info dm-regression-test | grep "Minor:" | awk '{print $2}')
    local hotpath_dir="/sys/block/dm-${dm_minor}/dm-remap/hotpath"
    
    if [[ -d "$hotpath_dir" ]]; then
        track_test "Hotpath Sysfs Interface" "PASS"
        
        # Test hotpath statistics files
        local hotpath_files=("hotpath_stats" "hotpath_efficiency" "hotpath_batch_size")
        for hotpath_file in "${hotpath_files[@]}"; do
            if [[ -r "${hotpath_dir}/${hotpath_file}" ]]; then
                track_test "Hotpath File $hotpath_file" "PASS"
            else
                track_test "Hotpath File $hotpath_file" "WARN"
            fi
        done
        
        # Test statistics reset functionality
        local reset_file="${hotpath_dir}/hotpath_reset"
        if [[ -w "$reset_file" ]]; then
            echo "1" > "$reset_file" 2>/dev/null
            track_test "Hotpath Stats Reset" "PASS"
        else
            track_test "Hotpath Stats Reset" "WARN"
        fi
        
    else
        track_test "Hotpath Sysfs Interface" "WARN"
    fi
}

# Test 6: I/O Stress Testing
test_io_stress() {
    section "I/O Stress Testing"
    
    local device="/dev/mapper/dm-regression-test"
    
    # Sequential read stress
    log "Running sequential read stress test..."
    if dd if="$device" of=/dev/null bs=4k count=1000 2>/dev/null; then
        track_test "Sequential Read Stress" "PASS"
    else
        track_test "Sequential Read Stress" "FAIL"
    fi
    
    # Sequential write stress
    log "Running sequential write stress test..."
    if dd if=/dev/zero of="$device" bs=4k count=500 2>/dev/null; then
        track_test "Sequential Write Stress" "PASS"
    else
        track_test "Sequential Write Stress" "FAIL"
    fi
    
    # Random I/O stress (if possible)
    log "Running random I/O stress test..."
    local random_success=0
    for i in {1..100}; do
        local offset=$((RANDOM % 1000))
        if dd if=/dev/zero of="$device" bs=512 count=1 seek=$offset 2>/dev/null; then
            random_success=$((random_success + 1))
        fi
    done
    
    if [[ $random_success -gt 90 ]]; then
        track_test "Random I/O Stress" "PASS"
    elif [[ $random_success -gt 50 ]]; then
        track_test "Random I/O Stress" "WARN"
    else
        track_test "Random I/O Stress" "FAIL"
    fi
    
    log "Random I/O success rate: ${random_success}/100"
}

# Test 7: Error Handling and Recovery
test_error_handling() {
    section "Error Handling and Recovery"
    
    # Test invalid device creation
    if ! echo "0 1000 remap /dev/nonexistent /dev/alsonotexist 0 100" | \
         dmsetup create invalid-test 2>/dev/null; then
        track_test "Invalid Device Rejection" "PASS"
    else
        track_test "Invalid Device Rejection" "FAIL"
        dmsetup remove invalid-test 2>/dev/null || true
    fi
    
    # Test device removal
    if dmsetup remove dm-regression-test; then
        track_test "Device Removal" "PASS"
    else
        track_test "Device Removal" "FAIL"
    fi
    
    # Recreate device for remaining tests
    local spare_sectors=$(blockdev --getsz /dev/loop31)
    echo "0 $(blockdev --getsz /dev/loop30) remap /dev/loop30 /dev/loop31 0 $spare_sectors" | \
        dmsetup create dm-regression-test
}

# Test 8: Resource Cleanup Testing
test_resource_cleanup() {
    section "Resource Cleanup Testing"
    
    # Create and remove multiple devices to test cleanup
    for i in {1..5}; do
        local test_device="dm-cleanup-test-$i"
        local spare_sectors=$(blockdev --getsz /dev/loop31)
        
        if echo "0 $(blockdev --getsz /dev/loop30) remap /dev/loop30 /dev/loop31 0 $spare_sectors" | \
           dmsetup create "$test_device"; then
            
            if dmsetup remove "$test_device"; then
                track_test "Cleanup Test $i" "PASS"
            else
                track_test "Cleanup Test $i" "FAIL"
            fi
        else
            track_test "Cleanup Test $i" "FAIL"
        fi
    done
}

# Test 9: Performance Regression Detection
test_performance_regression() {
    section "Performance Regression Detection"
    
    local device="/dev/mapper/dm-regression-test"
    
    # Baseline performance test
    log "Running performance regression test..."
    
    local start_time=$(date +%s.%N)
    dd if="$device" of=/dev/null bs=4k count=2000 2>/dev/null
    local end_time=$(date +%s.%N)
    
    local duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "1")
    local throughput=$(echo "scale=2; 8000 / $duration / 1024" | bc -l 2>/dev/null || echo "0")
    
    log "Performance test: ${throughput} MB/s in ${duration}s"
    
    # Check if performance is reasonable (>10 MB/s)
    if (( $(echo "$throughput > 10" | bc -l 2>/dev/null) )); then
        track_test "Performance Regression" "PASS"
    elif (( $(echo "$throughput > 1" | bc -l 2>/dev/null) )); then
        track_test "Performance Regression" "WARN"
    else
        track_test "Performance Regression" "FAIL"
    fi
    
    echo "throughput_mbps,duration_sec" > "${TEST_RESULTS_DIR}/performance_regression.csv"
    echo "${throughput},${duration}" >> "${TEST_RESULTS_DIR}/performance_regression.csv"
}

# Test 10: Memory Leak Detection
test_memory_leaks() {
    section "Memory Leak Detection"
    
    # Get initial memory usage
    local initial_memory=$(grep "VmallocUsed" /proc/meminfo | awk '{print $2}')
    log "Initial vmalloc memory: ${initial_memory} kB"
    
    # Create and destroy devices repeatedly
    for i in {1..20}; do
        local test_device="dm-leak-test"
        local spare_sectors=$(blockdev --getsz /dev/loop31)
        
        echo "0 $(blockdev --getsz /dev/loop30) remap /dev/loop30 /dev/loop31 0 $spare_sectors" | \
            dmsetup create "$test_device" 2>/dev/null
        
        # Do some I/O
        dd if=/dev/zero of="/dev/mapper/$test_device" bs=1k count=10 2>/dev/null || true
        
        dmsetup remove "$test_device" 2>/dev/null || true
    done
    
    # Give kernel time to cleanup
    sleep 2
    
    # Check final memory usage
    local final_memory=$(grep "VmallocUsed" /proc/meminfo | awk '{print $2}')
    local memory_diff=$((final_memory - initial_memory))
    
    log "Final vmalloc memory: ${final_memory} kB"
    log "Memory difference: ${memory_diff} kB"
    
    # Check for significant memory increase (>1MB indicates potential leak)
    if [[ $memory_diff -lt 1024 ]]; then
        track_test "Memory Leak Detection" "PASS"
    elif [[ $memory_diff -lt 5120 ]]; then
        track_test "Memory Leak Detection" "WARN"
    else
        track_test "Memory Leak Detection" "FAIL"
    fi
}

# Generate comprehensive regression report
generate_regression_report() {
    local report_file="${TEST_RESULTS_DIR}/regression_report.txt"
    
    header "Regression Test Report"
    
    {
        echo "dm-remap v4.0 Comprehensive Regression Test Report"
        echo "Generated: $(date)"
        echo "Test Suite: Full system validation before Phase 2"
        echo "==========================================================="
        echo
        
        echo "OVERALL RESULTS:"
        echo "  Total Tests Run: $TOTAL_TESTS"
        echo "  Tests Passed: $PASSED_TESTS"
        echo "  Tests Failed: $FAILED_TESTS"
        echo "  Warnings: $WARNINGS"
        echo "  Success Rate: $(( (PASSED_TESTS * 100) / TOTAL_TESTS ))%"
        echo
        
        if [[ $FAILED_TESTS -eq 0 ]]; then
            echo "🎉 REGRESSION TEST STATUS: ALL CLEAR!"
            echo "✅ No regressions detected - safe to proceed with Phase 2"
        elif [[ $FAILED_TESTS -lt 3 ]]; then
            echo "⚠️  REGRESSION TEST STATUS: MINOR ISSUES"
            echo "⚠️  Some minor issues detected - review before Phase 2"
        else
            echo "❌ REGRESSION TEST STATUS: SIGNIFICANT ISSUES"
            echo "❌ Multiple failures detected - address before Phase 2"
        fi
        echo
        
        echo "SYSTEM COMPONENTS STATUS:"
        echo "  Module Loading: $(grep "Module Loading.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  Device Creation: $(grep "Device Creation.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  Health Scanning: $(grep "Health Scanner.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "⚠️  WARN")"
        echo "  Memory Pools: $(grep "Memory Pool Manager.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  Hotpath Optimization: $(grep "Hotpath Initialization.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  I/O Operations: $(grep "Sequential.*Stress.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  Error Handling: $(grep "Invalid Device Rejection.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  Resource Cleanup: $(grep "Cleanup Test.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "❌ FAIL")"
        echo "  Performance: $(grep "Performance Regression.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "⚠️  WARN")"
        echo "  Memory Leaks: $(grep "Memory Leak Detection.*PASS" "$REGRESSION_LOG" >/dev/null && echo "✅ OK" || echo "⚠️  WARN")"
        echo
        
        echo "READINESS FOR PHASE 2:"
        if [[ $FAILED_TESTS -eq 0 ]]; then
            echo "🚀 READY FOR PHASE 2 DEVELOPMENT"
            echo "   All critical systems validated"
            echo "   No blocking regressions detected"
            echo "   Performance baseline established"
        else
            echo "🔧 REQUIRES ATTENTION BEFORE PHASE 2"
            echo "   Address failed tests before proceeding"
            echo "   Review warning conditions"
        fi
        
    } | tee "$report_file"
    
    success "Comprehensive regression report generated: $report_file"
}

# Main regression test execution
run_all_regression_tests() {
    init_regression_tests
    
    # Core functionality tests
    test_module_loading
    test_device_creation
    
    # Feature regression tests
    test_health_scanning
    test_memory_pools
    test_hotpath_optimization
    
    # Stress and reliability tests
    test_io_stress
    test_error_handling
    test_resource_cleanup
    
    # Performance and resource tests
    test_performance_regression
    test_memory_leaks
    
    # Generate comprehensive report
    generate_regression_report
    
    # Final summary
    if [[ $FAILED_TESTS -eq 0 ]]; then
        success "🎉 ALL REGRESSION TESTS PASSED! Ready for Phase 2 Development!"
        return 0
    else
        error "❌ $FAILED_TESTS regression test(s) failed. Address issues before Phase 2."
        return 1
    fi
}

# Check dependencies
check_dependencies() {
    local missing_tools=()
    
    for tool in bc dmsetup blockdev; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_tools+=("$tool")
        fi
    done
    
    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        error "Missing required tools: ${missing_tools[*]}"
        log "Install missing tools: apt-get install ${missing_tools[*]}"
        exit 1
    fi
}

# Main execution
main() {
    check_dependencies
    
    log "Starting comprehensive regression testing..."
    log "This will validate all dm-remap v4.0 features before Phase 2"
    
    if run_all_regression_tests; then
        success "🚀 Regression testing complete - Ready to begin Phase 2!"
        log "Next: Start Week 11-12 Performance Scaling & Optimization"
    else
        error "🔧 Regression issues detected - resolve before Phase 2"
    fi
}

# Execute main function
main "$@"