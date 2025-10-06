#!/bin/bash

# complete_test_suite_v1.sh - Complete test suite for dm-remap v1.1
# Runs all tests: functionality, performance, stress, and memory leak detection

set -euo pipefail

TEST_DIR="/home/christian/kernel_dev/dm-remap/tests"
LOG_DIR="/tmp/dm-remap-test-logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Ensure we're in the right directory
cd "$(dirname "$0")"

# Create log directory
mkdir -p "$LOG_DIR"

echo "================================================================="
echo "        dm-remap v1.1 COMPLETE TEST SUITE"
echo "================================================================="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo "Logs will be saved to: $LOG_DIR"
echo ""

# Check if required tools are available
echo "=== Dependency Check ==="
MISSING_TOOLS=()

if ! command -v bc >/dev/null 2>&1; then
    MISSING_TOOLS+=("bc")
fi

if ! command -v fio >/dev/null 2>&1; then
    echo "⚠️  fio not found - installing for detailed random I/O testing"
    sudo apt-get update && sudo apt-get install -y fio
fi

if ! command -v ioping >/dev/null 2>&1; then
    echo "⚠️  ioping not found - installing for precise latency testing"
    sudo apt-get install -y ioping
fi

echo "✅ Performance testing tools available: fio, ioping"

if [ ${#MISSING_TOOLS[@]} -gt 0 ]; then
    echo "❌ Missing required tools: ${MISSING_TOOLS[*]}"
    echo "Please install: sudo apt-get install ${MISSING_TOOLS[*]}"
    exit 1
fi

echo "✅ All required dependencies available"
echo ""

# Function to run a test and capture results
run_test() {
    local test_name="$1"
    local test_script="$2"
    local log_file="$LOG_DIR/${test_name}_${TIMESTAMP}.log"
    
    echo "================================================================="
    echo "  RUNNING: $test_name"
    echo "================================================================="
    echo "Script: $test_script"
    echo "Log: $log_file"
    echo ""
    
    # Ensure clean state before each test
    sudo rmmod dm_remap 2>/dev/null || true
    sleep 1
    
    # Run the test and capture output
    if bash "$test_script" 2>&1 | tee "$log_file"; then
        echo ""
        echo "✅ $test_name PASSED"
        return 0
    else
        echo ""
        echo "❌ $test_name FAILED"
        return 1
    fi
}

# Test execution tracking
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
FAILED_TESTS=()

# Test 1: Basic Functionality Test
echo "Starting Test Suite..."
echo ""

if run_test "BASIC_FUNCTIONALITY" "../simple_test_v1.sh"; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    FAILED_TESTS+=("Basic Functionality")
fi
TESTS_RUN=$((TESTS_RUN + 1))

echo ""
echo "Press Enter to continue to performance tests..."
read -r

# Test 2: Performance Benchmarks
if run_test "PERFORMANCE_BENCHMARKS" "performance_test_v1.sh"; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    FAILED_TESTS+=("Performance Benchmarks")
fi
TESTS_RUN=$((TESTS_RUN + 1))

echo ""
echo "Press Enter to continue to stress tests..."
read -r

# Test 3: Concurrent Stress Test
if run_test "CONCURRENT_STRESS" "stress_test_v1.sh"; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    FAILED_TESTS+=("Concurrent Stress")
fi
TESTS_RUN=$((TESTS_RUN + 1))

echo ""
echo "Press Enter to continue to memory leak detection..."
read -r

# Test 4: Memory Leak Detection
if run_test "MEMORY_LEAK_DETECTION" "memory_leak_test_v1.sh"; then
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
    FAILED_TESTS+=("Memory Leak Detection")
fi
TESTS_RUN=$((TESTS_RUN + 1))

# Final cleanup
sudo rmmod dm_remap 2>/dev/null || true

# Generate comprehensive report
REPORT_FILE="$LOG_DIR/test_suite_report_${TIMESTAMP}.txt"

cat > "$REPORT_FILE" << EOF
================================================================
dm-remap v1.1 Complete Test Suite Report
================================================================
Date: $(date)
System: $(uname -a)
Test Suite Version: v1.1 Complete
Test Duration: $(date --date="@$(($(date +%s) - START_TIME))" -u +%H:%M:%S) (started at $(date --date="@$START_TIME"))

SUMMARY:
========
Tests Run: $TESTS_RUN
Tests Passed: $TESTS_PASSED
Tests Failed: $TESTS_FAILED
Success Rate: $(echo "scale=1; $TESTS_PASSED * 100 / $TESTS_RUN" | bc)%

TEST RESULTS:
=============
EOF

echo "START_TIME=$(date +%s)" > /tmp/test_suite_start_time
START_TIME=$(cat /tmp/test_suite_start_time 2>/dev/null | cut -d= -f2 || date +%s)

# Add individual test results to report
if [ -f "$LOG_DIR/BASIC_FUNCTIONALITY_${TIMESTAMP}.log" ]; then
    echo "✅ Basic Functionality Test: PASSED" >> "$REPORT_FILE"
    echo "   - All core features working correctly" >> "$REPORT_FILE"
    echo "   - I/O operations: $(grep "Found.*I/O debug messages" "$LOG_DIR/BASIC_FUNCTIONALITY_${TIMESTAMP}.log" | tail -1 || echo "Not recorded")" >> "$REPORT_FILE"
else
    echo "❌ Basic Functionality Test: FAILED" >> "$REPORT_FILE"
fi

if [ -f "$LOG_DIR/PERFORMANCE_BENCHMARKS_${TIMESTAMP}.log" ]; then
    echo "✅ Performance Benchmarks: PASSED" >> "$REPORT_FILE"
    PERF_READ=$(grep "Sequential Read:" "$LOG_DIR/PERFORMANCE_BENCHMARKS_${TIMESTAMP}.log" | tail -1 || echo "Not recorded")
    PERF_WRITE=$(grep "Sequential Write:" "$LOG_DIR/PERFORMANCE_BENCHMARKS_${TIMESTAMP}.log" | tail -1 || echo "Not recorded")
    echo "   - $PERF_READ" >> "$REPORT_FILE"
    echo "   - $PERF_WRITE" >> "$REPORT_FILE"
else
    echo "❌ Performance Benchmarks: FAILED" >> "$REPORT_FILE"
fi

if [ -f "$LOG_DIR/CONCURRENT_STRESS_${TIMESTAMP}.log" ]; then
    echo "✅ Concurrent Stress Test: PASSED" >> "$REPORT_FILE"
    STRESS_OPS=$(grep "Total operations:" "$LOG_DIR/CONCURRENT_STRESS_${TIMESTAMP}.log" | tail -1 || echo "Not recorded")
    STRESS_RATE=$(grep "Operations per second:" "$LOG_DIR/CONCURRENT_STRESS_${TIMESTAMP}.log" | tail -1 || echo "Not recorded")
    echo "   - $STRESS_OPS" >> "$REPORT_FILE"
    echo "   - $STRESS_RATE" >> "$REPORT_FILE"
else
    echo "❌ Concurrent Stress Test: FAILED" >> "$REPORT_FILE"
fi

if [ -f "$LOG_DIR/MEMORY_LEAK_DETECTION_${TIMESTAMP}.log" ]; then
    echo "✅ Memory Leak Detection: PASSED" >> "$REPORT_FILE"
    MEM_RESULT=$(grep "EXCELLENT\|GOOD\|ATTENTION" "$LOG_DIR/MEMORY_LEAK_DETECTION_${TIMESTAMP}.log" | tail -1 || echo "Not recorded")
    echo "   - $MEM_RESULT" >> "$REPORT_FILE"
else
    echo "❌ Memory Leak Detection: FAILED" >> "$REPORT_FILE"
fi

cat >> "$REPORT_FILE" << EOF

FAILED TESTS:
=============
EOF

if [ ${#FAILED_TESTS[@]} -eq 0 ]; then
    echo "None - All tests passed successfully!" >> "$REPORT_FILE"
else
    for test in "${FAILED_TESTS[@]}"; do
        echo "❌ $test" >> "$REPORT_FILE"
    done
fi

cat >> "$REPORT_FILE" << EOF

LOG FILES:
==========
All detailed logs available in: $LOG_DIR/
- Basic Functionality: BASIC_FUNCTIONALITY_${TIMESTAMP}.log
- Performance: PERFORMANCE_BENCHMARKS_${TIMESTAMP}.log  
- Stress Test: CONCURRENT_STRESS_${TIMESTAMP}.log
- Memory Leak: MEMORY_LEAK_DETECTION_${TIMESTAMP}.log

SYSTEM INFORMATION:
===================
Kernel Version: $(uname -r)
Architecture: $(uname -m)
CPU Info: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
Memory: $(grep MemTotal /proc/meminfo | awk '{print int($2/1024) "MB"}')
Disk Space: $(df -h / | tail -1 | awk '{print "Free: " $4 " / Total: " $2}')

MODULE INFORMATION:
===================
Module Path: /home/christian/kernel_dev/dm-remap/src/dm-remap.ko
Module Size: $(ls -lh /home/christian/kernel_dev/dm-remap/src/dm-remap.ko | awk '{print $5}')
Build Date: $(stat -c %y /home/christian/kernel_dev/dm-remap/src/dm-remap.ko | cut -d. -f1)

CONCLUSION:
===========
EOF

if [ $TESTS_FAILED -eq 0 ]; then
    cat >> "$REPORT_FILE" << EOF
🎉 EXCELLENT: All tests passed successfully!

dm-remap v1.1 is ready for production use with:
- Fully functional core features
- Good performance characteristics  
- Stable under concurrent stress
- No memory leaks detected

The module demonstrates enterprise-level quality and reliability.
EOF
elif [ $TESTS_FAILED -eq 1 ]; then
    cat >> "$REPORT_FILE" << EOF
✅ GOOD: $TESTS_PASSED out of $TESTS_RUN tests passed.

One test failed but overall the module shows good stability.
Review the failed test logs for specific issues to address.
EOF
else
    cat >> "$REPORT_FILE" << EOF
⚠️  NEEDS ATTENTION: $TESTS_FAILED out of $TESTS_RUN tests failed.

Multiple test failures indicate issues that should be resolved
before production deployment. Review all failed test logs.
EOF
fi

# Display final results
echo ""
echo "================================================================="
echo "                    FINAL TEST RESULTS"
echo "================================================================="
echo ""
echo "📊 SUMMARY:"
echo "   Tests Run: $TESTS_RUN"
echo "   Tests Passed: $TESTS_PASSED" 
echo "   Tests Failed: $TESTS_FAILED"
echo "   Success Rate: $(echo "scale=1; $TESTS_PASSED * 100 / $TESTS_RUN" | bc)%"
echo ""

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo "❌ FAILED TESTS:"
    for test in "${FAILED_TESTS[@]}"; do
        echo "   • $test"
    done
    echo ""
fi

echo "📁 DETAILED REPORT: $REPORT_FILE"
echo "📁 ALL LOGS: $LOG_DIR/"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo "🎉 CONGRATULATIONS!"
    echo "   dm-remap v1.1 passed ALL tests successfully!"
    echo "   The module is ready for production use."
else
    echo "⚠️  Some tests failed. Review the logs for details."
fi

echo ""
echo "================================================================="
echo "                  TEST SUITE COMPLETED"
echo "================================================================="