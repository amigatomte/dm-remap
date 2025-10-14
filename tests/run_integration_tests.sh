#!/bin/bash
# Integration test runner for dm-remap v4.0
# Tests all priorities working together

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=========================================="
echo "dm-remap v4.0 Integration Test Suite"
echo "=========================================="
echo ""
echo "Project root: $PROJECT_ROOT"
echo "Test directory: $SCRIPT_DIR"
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Function to run a test
run_test() {
    local test_name="$1"
    local test_module="$2"
    
    echo "----------------------------------------"
    echo "Running: $test_name"
    echo "Module: $test_module"
    echo "----------------------------------------"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Check if module exists
    if [ ! -f "$PROJECT_ROOT/tests/$test_module" ]; then
        echo -e "${YELLOW}[SKIP]${NC} Module not built: $test_module"
        echo "       Run 'make' in tests/ directory first"
        return
    fi
    
    # Load module
    if sudo insmod "$PROJECT_ROOT/tests/$test_module"; then
        echo -e "${GREEN}[LOAD]${NC} Module loaded successfully"
        
        # Check dmesg for test results
        sleep 1
        dmesg | tail -50 | grep -E "\[(PASS|FAIL|INFO)\]" || true
        
        # Unload module
        local module_name=$(basename "$test_module" .ko)
        if sudo rmmod "$module_name" 2>/dev/null; then
            echo -e "${GREEN}[UNLOAD]${NC} Module unloaded successfully"
        else
            echo -e "${RED}[ERROR]${NC} Failed to unload module"
        fi
        
        # Check if test passed
        if dmesg | tail -100 | grep -q "ALL.*TESTS PASSED"; then
            echo -e "${GREEN}[PASS]${NC} $test_name"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}[FAIL]${NC} $test_name"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${RED}[ERROR]${NC} Failed to load module"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    echo ""
}

# Check if running as root
if [ "$EUID" -ne 0 ] && [ -z "$SUDO_USER" ]; then
    echo -e "${YELLOW}WARNING:${NC} This script needs root privileges to load kernel modules"
    echo "         Re-running with sudo..."
    sudo "$0" "$@"
    exit $?
fi

# Clear dmesg
echo "Clearing kernel message buffer..."
sudo dmesg -C

echo ""
echo "=========================================="
echo "Starting Integration Tests"
echo "=========================================="
echo ""

# Run integration test suite
run_test "Integration Tests (All Priorities)" "test_v4_integration.ko"

# Run individual priority tests for comparison
echo ""
echo "=========================================="
echo "Running Individual Priority Tests"
echo "=========================================="
echo ""

run_test "Priority 1: Health Monitoring" "test_v4_health_monitoring.ko"
run_test "Priority 2: Predictive Analysis" "test_v4_health_monitoring.ko"
run_test "Priority 3: Spare Pool Management" "test_v4_spare_pool.ko"
run_test "Priority 6: Setup Reassembly" "test_v4_setup_reassembly.ko"

# Final results
echo ""
echo "=========================================="
echo "Integration Test Summary"
echo "=========================================="
echo -e "Total tests:  $TOTAL_TESTS"
echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"
echo "=========================================="
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}✓ ALL INTEGRATION TESTS PASSED${NC}"
    echo ""
    echo "dm-remap v4.0 is ready for:"
    echo "  - Performance benchmarking"
    echo "  - Real-world scenario testing"
    echo "  - Release candidate preparation"
    echo ""
    exit 0
else
    echo -e "${RED}✗ INTEGRATION TESTS FAILED${NC}"
    echo ""
    echo "Please fix failing tests before proceeding to:"
    echo "  - Performance benchmarking"
    echo "  - Release preparation"
    echo ""
    echo "Check dmesg for detailed error messages:"
    echo "  sudo dmesg | grep -E '\[(PASS|FAIL|ERROR)\]'"
    echo ""
    exit 1
fi
