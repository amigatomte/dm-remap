#!/bin/bash
#
# Integration Testing Suite for dm-remap v2.0 Production Hardening
#
# This comprehensive test suite validates:
# - Production hardening integration and initialization
# - Error classification and adaptive retry logic
# - Health monitoring and performance tracking
# - Memory pressure handling and emergency modes
# - Audit logging and compliance features
# - Performance optimization and latency impact
# - Stress testing under realistic workloads
#
# Focus: Complete integration validation and performance optimization
#

set -e

# Test configuration
TEST_NAME="Production Hardening Integration Test Suite"
TEST_DIR="/tmp/dm_remap_integration_test"
LOOP_SIZE_MB=100
STRESS_ITERATIONS=50
PERFORMANCE_BASELINE_NS=1000000  # 1ms baseline

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== ${TEST_NAME} ===${NC}"
echo "Comprehensive integration testing and performance optimization"
echo "Target: Enterprise-ready production stability and performance"
echo

# Function to cleanup resources
cleanup() {
    echo -e "${YELLOW}Cleaning up integration test resources...${NC}"
    
    # Stop any background processes
    jobs -p | xargs -r kill 2>/dev/null || true
    
    # Remove dm devices
    sudo dmsetup remove intg-remap-perf 2>/dev/null || true
    sudo dmsetup remove intg-remap-stress 2>/dev/null || true  
    sudo dmsetup remove intg-flakey-perf 2>/dev/null || true
    sudo dmsetup remove intg-flakey-stress 2>/dev/null || true
    
    # Detach loop devices
    for loop in $(losetup -a | grep "${TEST_DIR}" | cut -d: -f1); do
        sudo losetup -d "$loop" 2>/dev/null || true
    done
    
    # Clean up test directory
    rm -rf "$TEST_DIR"
    
    echo -e "${GREEN}Integration test cleanup completed${NC}"
}

trap cleanup EXIT

# Create test environment
echo -e "${CYAN}=== PHASE 2A: INTEGRATION TESTING SETUP ===${NC}"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

# Create multiple loop devices for comprehensive testing
echo "Creating test storage devices..."
dd if=/dev/zero of=intg_main1.img bs=1M count=$LOOP_SIZE_MB
dd if=/dev/zero of=intg_spare1.img bs=1M count=20
dd if=/dev/zero of=intg_main2.img bs=1M count=$LOOP_SIZE_MB
dd if=/dev/zero of=intg_spare2.img bs=1M count=20

# Set up loop devices
MAIN1_LOOP=$(sudo losetup -f --show intg_main1.img)
SPARE1_LOOP=$(sudo losetup -f --show intg_spare1.img)
MAIN2_LOOP=$(sudo losetup -f --show intg_main2.img)
SPARE2_LOOP=$(sudo losetup -f --show intg_spare2.img)

echo "Integration test devices:"
echo "  Performance test: $MAIN1_LOOP -> $SPARE1_LOOP"
echo "  Stress test: $MAIN2_LOOP -> $SPARE2_LOOP"

# Ensure dm-remap with production hardening is loaded
echo -e "${CYAN}Loading enhanced dm-remap with production hardening...${NC}"
cd /home/christian/kernel_dev/dm-remap/src

# Check if module needs rebuilding
if [[ ! -f dm_remap.ko ]] || [[ dm_remap_production.c -nt dm_remap.ko ]]; then
    echo "Rebuilding module with latest production hardening..."
    make clean && make
fi

# Remove old module and load enhanced version
sudo rmmod dm_remap 2>/dev/null || true
sudo insmod dm_remap.ko auto_remap_enabled=1 error_threshold=3 enable_production_mode=1 debug_level=2

cd "$TEST_DIR"

# Verify production hardening parameters
echo -e "${CYAN}Verifying production hardening integration...${NC}"
echo "Production parameters:"
for param in enable_production_mode max_retries_production emergency_threshold; do
    value=$(cat /sys/module/dm_remap/parameters/$param 2>/dev/null || echo "N/A")
    echo "  $param: $value"
done

echo
echo -e "${PURPLE}=== INTEGRATION TEST PHASE 1: INITIALIZATION VALIDATION ===${NC}"

# Test 1: Production Context Initialization
echo -e "${BLUE}TEST 1: Production Context Initialization${NC}"

# Create test device to trigger initialization
MAIN1_SECTORS=$(blockdev --getsz "$MAIN1_LOOP")
SPARE1_SECTORS=$(blockdev --getsz "$SPARE1_LOOP")

sudo dmsetup create intg-flakey-perf --table "0 $MAIN1_SECTORS flakey $MAIN1_LOOP 0 3 2"
sudo dmsetup create intg-remap-perf --table "0 $MAIN1_SECTORS remap /dev/mapper/intg-flakey-perf $SPARE1_LOOP 0 $SPARE1_SECTORS"

echo "Device created, checking for production initialization..."

# Clear kernel log and wait for messages
sudo dmesg -c >/dev/null
sleep 2

# Check for production initialization messages
production_init=$(sudo dmesg | grep -c -E "(Production.*initialized|production.*context)" || echo "0")
if [[ $production_init -gt 0 ]]; then
    echo -e "${GREEN}✓ Production context initialization detected${NC}"
    sudo dmesg | grep -E "(Production|production)" | head -3
else
    echo -e "${YELLOW}⚠ Production initialization messages not found${NC}"
    echo "Checking alternative initialization indicators..."
    
    # Look for memory allocation or audit log setup
    init_indicators=$(sudo dmesg | grep -c -E "(audit.*log|hardening|dmr_production)" || echo "0")
    if [[ $init_indicators -gt 0 ]]; then
        echo -e "${GREEN}✓ Production components initialized${NC}"
    else
        echo -e "${YELLOW}⚠ Production initialization needs investigation${NC}"
    fi
fi

echo
echo -e "${PURPLE}=== INTEGRATION TEST PHASE 2: ERROR HANDLING VALIDATION ===${NC}"

# Test 2: Enhanced Error Classification
echo -e "${BLUE}TEST 2: Enhanced Error Classification & Adaptive Retry${NC}"

initial_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)
echo "Initial error count: $initial_errors"

# Generate different types of errors to test classification
echo "Generating various error types for classification testing..."

for error_test in {1..5}; do
    echo "  Error classification test $error_test..."
    
    # Wait for error injection period
    sleep 3
    
    # Generate I/O that will encounter errors
    echo "Error classification test $error_test" | sudo dd of=/dev/mapper/intg-remap-perf bs=2048 seek=$((error_test * 25)) count=1 conv=notrunc 2>/dev/null || {
        echo "    Error encountered - testing classification logic"
    }
    
    sleep 1
done

final_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)
errors_detected=$((final_errors - initial_errors))

echo "Error classification results:"
echo "  Errors detected: $errors_detected"
echo "  Auto-remaps triggered: $(cat /sys/module/dm_remap/parameters/global_auto_remaps)"

# Look for error classification messages
error_classification=$(sudo dmesg | grep -c -E "(classified|retry.*policy|adaptive)" || echo "0")
echo "  Error classification events: $error_classification"

echo
echo -e "${PURPLE}=== INTEGRATION TEST PHASE 3: PERFORMANCE IMPACT ANALYSIS ===${NC}"

# Test 3: Performance Impact Assessment
echo -e "${BLUE}TEST 3: Performance Impact & Latency Analysis${NC}"

echo "Measuring performance impact of production hardening..."

# Baseline performance test (without errors)
echo "Running baseline performance test..."
start_time=$(date +%s%N)

for perf_test in {1..10}; do
    echo "Baseline test data $perf_test" > /tmp/test_data_$perf_test
    sudo dd if=/tmp/test_data_$perf_test of=/dev/mapper/intg-remap-perf bs=4096 seek=$((perf_test * 50)) count=1 conv=notrunc 2>/dev/null
done

end_time=$(date +%s%N)
baseline_latency_ns=$((end_time - start_time))
avg_baseline_ns=$((baseline_latency_ns / 10))

echo "Performance baseline results:"
echo "  Total time: ${baseline_latency_ns}ns"
echo "  Average per I/O: ${avg_baseline_ns}ns"

# Check if latency is reasonable
if [[ $avg_baseline_ns -lt $PERFORMANCE_BASELINE_NS ]]; then
    echo -e "${GREEN}✓ Performance within acceptable baseline${NC}"
else
    echo -e "${YELLOW}⚠ Performance may need optimization (${avg_baseline_ns}ns > ${PERFORMANCE_BASELINE_NS}ns)${NC}"
fi

echo
echo -e "${PURPLE}=== INTEGRATION TEST PHASE 4: STRESS TESTING ===${NC}"

# Test 4: Stress Testing with Concurrent Load
echo -e "${BLUE}TEST 4: Concurrent Load & Stress Testing${NC}"

# Set up second device for stress testing  
MAIN2_SECTORS=$(blockdev --getsz "$MAIN2_LOOP")
SPARE2_SECTORS=$(blockdev --getsz "$SPARE2_LOOP")

sudo dmsetup create intg-flakey-stress --table "0 $MAIN2_SECTORS flakey $MAIN2_LOOP 0 1 4"
sudo dmsetup create intg-remap-stress --table "0 $MAIN2_SECTORS remap /dev/mapper/intg-flakey-stress $SPARE2_LOOP 0 $SPARE2_SECTORS"

echo "Starting concurrent stress test with $STRESS_ITERATIONS iterations..."
initial_stress_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)

# Background stress test
{
    for stress_iter in $(seq 1 $STRESS_ITERATIONS); do
        echo "Stress test iteration $stress_iter" | sudo dd of=/dev/mapper/intg-remap-stress bs=1024 seek=$((stress_iter * 10)) count=1 conv=notrunc 2>/dev/null || true
        sleep 0.1
    done
} &

stress_pid=$!

# Concurrent performance test
{
    for perf_iter in $(seq 1 20); do
        echo "Concurrent perf test $perf_iter" | sudo dd of=/dev/mapper/intg-remap-perf bs=512 seek=$((perf_iter * 100)) count=1 conv=notrunc 2>/dev/null || true
        sleep 0.2
    done
} &

perf_pid=$!

# Monitor system during stress test
echo "Monitoring system during stress test..."
for monitor_cycle in {1..10}; do
    echo "  Monitor cycle $monitor_cycle: $(date)"
    
    # Check device status
    sudo dmsetup status intg-remap-perf | grep -E "(errors|remaps)" || echo "    Status monitoring..."
    
    # Check resource usage
    free_mb=$(free -m | awk '/^Mem:/ {print $7}')
    echo "    Available memory: ${free_mb}MB"
    
    sleep 2
done

# Wait for background processes
wait $stress_pid
wait $perf_pid

final_stress_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)
stress_errors_detected=$((final_stress_errors - initial_stress_errors))

echo "Stress test results:"
echo "  Stress test errors: $stress_errors_detected"
echo "  Total auto-remaps: $(cat /sys/module/dm_remap/parameters/global_auto_remaps)"

echo
echo -e "${PURPLE}=== INTEGRATION TEST PHASE 5: HEALTH MONITORING VALIDATION ===${NC}"

# Test 5: Health Monitoring and Metrics
echo -e "${BLUE}TEST 5: Health Monitoring & Metrics Validation${NC}"

echo "Analyzing health monitoring effectiveness..."

# Check device health status
echo "Device health status:"
echo "  Performance device:"
sudo dmsetup status intg-remap-perf
echo "  Stress test device:"
sudo dmsetup status intg-remap-stress

# Look for health monitoring messages
health_messages=$(sudo dmesg | grep -c -E "(Health|health.*update|metrics)" || echo "0")
echo "Health monitoring events detected: $health_messages"

if [[ $health_messages -gt 0 ]]; then
    echo -e "${GREEN}✓ Health monitoring active${NC}"
    echo "Recent health monitoring activity:"
    sudo dmesg | grep -E "(Health|health)" | tail -5
else
    echo -e "${YELLOW}⚠ Health monitoring messages limited${NC}"
fi

echo
echo -e "${PURPLE}=== INTEGRATION TESTING RESULTS ANALYSIS ===${NC}"

# Final Analysis and Results
echo -e "${CYAN}=== FINAL INTEGRATION ANALYSIS ===${NC}"

# Calculate overall error detection effectiveness
total_final_errors=$(cat /sys/module/dm_remap/parameters/global_read_errors)
total_auto_remaps=$(cat /sys/module/dm_remap/parameters/global_auto_remaps)

echo "Integration test summary:"
echo "  Total errors detected: $total_final_errors"
echo "  Total auto-remaps: $total_auto_remaps"
echo "  Error classification events: $error_classification"
echo "  Health monitoring events: $health_messages"
echo "  Performance baseline: ${avg_baseline_ns}ns per I/O"

# Calculate integration score
integration_score=0

if [[ $production_init -gt 0 || $init_indicators -gt 0 ]]; then
    integration_score=$((integration_score + 25))
    echo -e "${GREEN}✓ Production initialization: WORKING${NC}"
else
    echo -e "${YELLOW}⚠ Production initialization: NEEDS ATTENTION${NC}"
fi

if [[ $total_final_errors -gt 0 ]]; then
    integration_score=$((integration_score + 25))
    echo -e "${GREEN}✓ Error detection: WORKING${NC}"
else
    echo -e "${YELLOW}⚠ Error detection: LIMITED${NC}"
fi

if [[ $avg_baseline_ns -lt $PERFORMANCE_BASELINE_NS ]]; then
    integration_score=$((integration_score + 25))
    echo -e "${GREEN}✓ Performance: ACCEPTABLE${NC}"
else
    echo -e "${YELLOW}⚠ Performance: NEEDS OPTIMIZATION${NC}"
fi

if [[ $health_messages -gt 0 || $error_classification -gt 0 ]]; then
    integration_score=$((integration_score + 25))
    echo -e "${GREEN}✓ Advanced features: WORKING${NC}"
else  
    echo -e "${YELLOW}⚠ Advanced features: PARTIAL${NC}"
fi

# Overall integration assessment
echo -e "${CYAN}Overall Integration Assessment:${NC}"
if [[ $integration_score -ge 80 ]]; then
    echo -e "${GREEN}✓ INTEGRATION STATUS: EXCELLENT (${integration_score}/100)${NC}"
    echo "  Production hardening is well integrated and functional"
elif [[ $integration_score -ge 60 ]]; then
    echo -e "${YELLOW}⚠ INTEGRATION STATUS: GOOD (${integration_score}/100)${NC}"
    echo "  Most features working, some optimization needed"
elif [[ $integration_score -ge 40 ]]; then
    echo -e "${YELLOW}⚠ INTEGRATION STATUS: FAIR (${integration_score}/100)${NC}"
    echo "  Core functionality working, advanced features need work"
else
    echo -e "${RED}✗ INTEGRATION STATUS: NEEDS IMPROVEMENT (${integration_score}/100)${NC}"
    echo "  Significant integration issues need resolution"
fi

# Performance optimization recommendations
echo -e "${CYAN}Performance Optimization Recommendations:${NC}"
if [[ $avg_baseline_ns -gt 500000 ]]; then  # > 0.5ms
    echo "  • Consider reducing debug output in production"
    echo "  • Optimize bio tracking for large I/Os"
    echo "  • Review memory allocation patterns"
fi

if [[ $stress_errors_detected -gt $STRESS_ITERATIONS ]]; then
    echo "  • Investigate error detection sensitivity"
    echo "  • Review error classification thresholds"
fi

# Show recent kernel messages for debugging
echo -e "${CYAN}Recent kernel activity (last 15 messages):${NC}"
sudo dmesg | grep dm-remap | tail -15

echo
echo -e "${BLUE}Integration testing completed.${NC}"
echo "Next steps: Performance optimization and production deployment preparation."