#!/bin/bash
#
# complete_test_suite_v3.sh - Comprehensive test suite for dm-remap v3.0
# 
# This script runs all major test suites for dm-remap v3.0:
# - Phase 1: Metadata Infrastructure (test_metadata_v3.sh)
# - Phase 2: Persistence Engine (test_metadata_io_v3.sh) 
# - Phase 3: Recovery System (test_recovery_v3.sh)
# - Legacy v2.0 functionality (complete_test_suite_v2.sh)
# - Production hardening tests
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m'

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
LOG_DIR="/tmp/dm-remap-v3-complete-test-logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="$LOG_DIR/complete_test_results_v3_$TIMESTAMP.log"

# Test configuration
TOTAL_SUITES=0
PASSED_SUITES=0
FAILED_SUITES=0

# Ensure we're running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    exit 1
fi

# Create log directory
mkdir -p "$LOG_DIR"

print_header() {
    echo -e "${WHITE}================================================================="
    echo -e "           dm-remap v3.0 COMPLETE TEST SUITE"
    echo -e "=================================================================${NC}"
    echo -e "${CYAN}Date: $(date)${NC}"
    echo -e "${CYAN}System: $(uname -a)${NC}"
    echo -e "${CYAN}Kernel: $(uname -r)${NC}"
    echo -e "${CYAN}Log Directory: $LOG_DIR${NC}"
    echo
    echo -e "${BLUE}This comprehensive test suite covers:${NC}"
    echo -e "${BLUE}  • Phase 1: Metadata Infrastructure${NC}"
    echo -e "${BLUE}  • Phase 2: Persistence Engine${NC}"
    echo -e "${BLUE}  • Phase 3: Recovery System${NC}"
    echo -e "${BLUE}  • Legacy v2.0 Functionality${NC}"
    echo -e "${BLUE}  • Production Hardening${NC}"
    echo
}

run_test_suite() {
    local suite_name="$1"
    local script_path="$2"
    local suite_description="$3"
    
    TOTAL_SUITES=$((TOTAL_SUITES + 1))
    
    echo -e "${WHITE}================================================================="
    echo -e "SUITE $TOTAL_SUITES: $suite_name"
    echo -e "=================================================================${NC}"
    echo -e "${CYAN}Description: $suite_description${NC}"
    echo -e "${CYAN}Script: $script_path${NC}"
    echo
    
    local log_file="$LOG_DIR/${suite_name// /_}_$TIMESTAMP.log"
    
    echo -e "${YELLOW}Running $suite_name...${NC}"
    
    if [ ! -f "$script_path" ]; then
        echo -e "${RED}✗ SUITE FAILED: Script not found: $script_path${NC}"
        FAILED_SUITES=$((FAILED_SUITES + 1))
        echo "SUITE FAILED: Script not found" >> "$log_file"
        return 1
    fi
    
    if chmod +x "$script_path"; then
        if "$script_path" 2>&1 | tee "$log_file"; then
            local exit_code=${PIPESTATUS[0]}
            if [ $exit_code -eq 0 ]; then
                echo -e "${GREEN}✓ SUITE PASSED: $suite_name${NC}"
                PASSED_SUITES=$((PASSED_SUITES + 1))
                echo "SUITE PASSED" >> "$log_file"
            else
                echo -e "${RED}✗ SUITE FAILED: $suite_name (exit code: $exit_code)${NC}"
                FAILED_SUITES=$((FAILED_SUITES + 1))
                echo "SUITE FAILED" >> "$log_file"
            fi
        else
            echo -e "${RED}✗ SUITE FAILED: $suite_name (script execution failed)${NC}"
            FAILED_SUITES=$((FAILED_SUITES + 1))
            echo "SUITE FAILED" >> "$log_file"
        fi
    else
        echo -e "${RED}✗ SUITE FAILED: $suite_name${NC}"
        FAILED_SUITES=$((FAILED_SUITES + 1))
        echo "SUITE FAILED" >> "$log_file"
        
        # Ask user if they want to continue
        echo
        echo -e "${YELLOW}Suite failed. Continue with remaining tests? (y/n)${NC}"
        read -r -t 10 continue_choice || continue_choice="y"
        if [[ "$continue_choice" != "y" && "$continue_choice" != "Y" ]]; then
            echo -e "${RED}Test suite execution aborted by user.${NC}"
            exit 1
        fi
    fi
    
    echo
}

run_cleanup() {
    echo -e "${YELLOW}Performing final cleanup...${NC}"
    
    # Remove any test targets
    sudo dmsetup remove test-remap-v3 2>/dev/null || true
    sudo dmsetup remove test-message 2>/dev/null || true
    sudo dmsetup remove test-status 2>/dev/null || true
    sudo dmsetup remove test-recovery 2>/dev/null || true
    
    # Clean up loop devices
    for i in {17..25}; do
        sudo losetup -d /dev/loop$i 2>/dev/null || true
    done
    
    # Remove test files
    rm -f /tmp/test_*.img /tmp/metadata.json
    
    echo -e "${GREEN}Final cleanup completed.${NC}"
}

print_summary() {
    echo -e "${WHITE}================================================================="
    echo -e "                      TEST SUMMARY"
    echo -e "=================================================================${NC}"
    echo -e "${CYAN}Total Test Suites: $TOTAL_SUITES${NC}"
    echo -e "${GREEN}Passed: $PASSED_SUITES${NC}"
    echo -e "${RED}Failed: $FAILED_SUITES${NC}"
    echo
    
    local success_rate=0
    if [ $TOTAL_SUITES -gt 0 ]; then
        success_rate=$((PASSED_SUITES * 100 / TOTAL_SUITES))
    fi
    
    echo -e "${CYAN}Success Rate: ${success_rate}%${NC}"
    echo
    
    if [ $FAILED_SUITES -eq 0 ]; then
        echo -e "${GREEN}🎉 ALL TEST SUITES PASSED!${NC}"
        echo -e "${GREEN}dm-remap v3.0 is ready for production deployment!${NC}"
        echo
        echo -e "${BLUE}Achievements:${NC}"
        echo -e "${BLUE}  ✓ Phase 1: Metadata Infrastructure - COMPLETE${NC}"
        echo -e "${BLUE}  ✓ Phase 2: Persistence Engine - COMPLETE${NC}"
        echo -e "${BLUE}  ✓ Phase 3: Recovery System - COMPLETE${NC}"
        echo -e "${BLUE}  ✓ Legacy v2.0 Functionality - VERIFIED${NC}"
        echo -e "${BLUE}  ✓ Production Hardening - VALIDATED${NC}"
        echo
        echo -e "${WHITE}dm-remap v3.0 - Enterprise-grade device mapper target ready!${NC}"
    else
        echo -e "${RED}❌ Some test suites failed.${NC}"
        echo -e "${YELLOW}Please review the individual test logs in: $LOG_DIR${NC}"
        if [ $PASSED_SUITES -gt 0 ]; then
            echo -e "${YELLOW}Note: $PASSED_SUITES suites passed, indicating partial functionality.${NC}"
        fi
    fi
    
    echo
    echo -e "${CYAN}Complete test results saved to: $RESULTS_FILE${NC}"
    
    # Save summary to results file
    {
        echo "dm-remap v3.0 Complete Test Suite Results"
        echo "========================================"
        echo "Date: $(date)"
        echo "Total Suites: $TOTAL_SUITES"
        echo "Passed: $PASSED_SUITES"
        echo "Failed: $FAILED_SUITES"
        echo "Success Rate: ${success_rate}%"
        echo
        if [ $FAILED_SUITES -eq 0 ]; then
            echo "RESULT: ALL TESTS PASSED - PRODUCTION READY"
        else
            echo "RESULT: SOME TESTS FAILED - REVIEW REQUIRED"
        fi
    } > "$RESULTS_FILE"
}

# Main execution
main() {
    cd "$SCRIPT_DIR"
    
    print_header
    
    # Phase 1: Metadata Infrastructure
    run_test_suite "Phase 1 Metadata Infrastructure" \
                   "./test_metadata_v3.sh" \
                   "Tests metadata structures, persistence foundation, and basic functionality"
    
    # Phase 2: Persistence Engine  
    run_test_suite "Phase 2 Persistence Engine" \
                   "./test_metadata_io_v3.sh" \
                   "Tests metadata I/O operations, read/write persistence, and data integrity"
    
    # Phase 3: Recovery System
    run_test_suite "Phase 3 Recovery System" \
                   "./test_recovery_v3.sh" \
                   "Tests recovery functionality, message interface, and production features"
    
    # Legacy v2.0 Functionality
    run_test_suite "Legacy v2.0 Functionality" \
                   "./complete_test_suite_v2.sh" \
                   "Ensures backward compatibility and validates v2.0 features still work"
    
    # Production Hardening
    if [ -f "./production_hardening_test.sh" ]; then
        run_test_suite "Production Hardening" \
                       "./production_hardening_test.sh" \
                       "Tests error handling, edge cases, and production readiness"
    else
        echo -e "${YELLOW}Note: Production hardening test not found, skipping...${NC}"
    fi
    
    # Performance validation
    if [ -f "./simple_performance_test.sh" ]; then
        run_test_suite "Performance Validation" \
                       "./simple_performance_test.sh" \
                       "Validates performance characteristics and overhead measurements"
    else
        echo -e "${YELLOW}Note: Performance test not found, skipping...${NC}"
    fi
    
    run_cleanup
    print_summary
    
    # Exit with appropriate code
    if [ $FAILED_SUITES -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# Set trap for cleanup on exit
trap run_cleanup EXIT

# Execute main function
main "$@"