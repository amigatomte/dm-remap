#!/bin/bash

# dm-remap Health & Performance Monitor
# Provides real-time statistics on dm-remap device operation
# Useful for production monitoring and debugging

set -e

DEVICE_NAME="${1:-dm-remap-test}"
UPDATE_INTERVAL="${2:-5}"  # Update interval in seconds
OUTPUT_FILE="/tmp/remap_monitor_$(date +%s).log"

# Color codes for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get current metrics
get_device_status() {
    sudo dmsetup status "$DEVICE_NAME" 2>/dev/null || echo "Device not found"
}

get_device_table() {
    sudo dmsetup table "$DEVICE_NAME" 2>/dev/null || echo "Device not found"
}

get_active_mappings() {
    cat /sys/kernel/dm_remap/active_mappings 2>/dev/null || echo "0"
}

get_dmesg_tail() {
    sudo dmesg | grep dm-remap | tail -5
}

# Format output nicely
print_header() {
    echo ""
    echo "=========================================="
    echo "dm-remap Health Monitor"
    echo "Device: $DEVICE_NAME"
    echo "Time: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "=========================================="
}

print_status() {
    echo ""
    echo "Device Status:"
    echo "--------------"
    
    local status=$(get_device_status)
    if echo "$status" | grep -q "Device not found"; then
        echo -e "${RED}✗ Device not found${NC}"
        return 1
    else
        echo -e "${GREEN}✓ Device active${NC}"
        echo "Status: $status"
    fi
}

print_configuration() {
    echo ""
    echo "Configuration:"
    echo "---------------"
    
    local table=$(get_device_table)
    echo "Table: $table"
}

print_mappings() {
    echo ""
    echo "Remap Statistics:"
    echo "-----------------"
    
    local mappings=$(get_active_mappings)
    echo "Active remaps: $mappings"
}

print_recent_activity() {
    echo ""
    echo "Recent Activity (dmesg):"
    echo "------------------------"
    
    get_dmesg_tail
}

print_sysfs_stats() {
    echo ""
    echo "sysfs Statistics:"
    echo "-----------------"
    
    if [ -d "/sys/kernel/dm_remap" ]; then
        for stat_file in /sys/kernel/dm_remap/*; do
            if [ -f "$stat_file" ]; then
                name=$(basename "$stat_file")
                value=$(cat "$stat_file" 2>/dev/null || echo "N/A")
                printf "  %-30s: %s\n" "$name" "$value"
            fi
        done
    else
        echo "  No sysfs interface available"
    fi
}

# Single snapshot
snapshot() {
    print_header
    print_status || return 1
    print_configuration
    print_mappings
    print_recent_activity
    print_sysfs_stats
}

# Continuous monitoring
monitor() {
    echo "Starting continuous monitoring..."
    echo "Press Ctrl+C to stop"
    echo ""
    
    while true; do
        clear
        snapshot
        
        # Log to file
        echo "$(date '+%Y-%m-%d %H:%M:%S'): $(get_active_mappings) active remaps" >> "$OUTPUT_FILE"
        
        echo ""
        echo "Next update in ${UPDATE_INTERVAL}s (Ctrl+C to stop)..."
        echo "Log: $OUTPUT_FILE"
        
        sleep "$UPDATE_INTERVAL"
    done
}

# Health check
health_check() {
    echo ""
    echo "Health Check Results:"
    echo "====================="
    echo ""
    
    local passed=0
    local failed=0
    
    # Check 1: Device exists and is active
    if get_device_status > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Device is active${NC}"
        ((passed++))
    else
        echo -e "${RED}✗ Device not found or inactive${NC}"
        ((failed++))
    fi
    
    # Check 2: sysfs interface available
    if [ -d "/sys/kernel/dm_remap" ]; then
        echo -e "${GREEN}✓ sysfs interface available${NC}"
        ((passed++))
    else
        echo -e "${YELLOW}! sysfs interface not found${NC}"
        ((failed++))
    fi
    
    # Check 3: No recent errors in dmesg
    if ! sudo dmesg | grep -i "error" | grep dm-remap | tail -1 | grep -q "dm-remap"; then
        echo -e "${GREEN}✓ No recent errors in dmesg${NC}"
        ((passed++))
    else
        echo -e "${RED}✗ Recent errors detected${NC}"
        ((failed++))
    fi
    
    # Check 4: Remap count reasonable
    local mappings=$(get_active_mappings)
    if [ "$mappings" -gt 0 ] 2>/dev/null; then
        echo -e "${GREEN}✓ Remaps active: $mappings${NC}"
        ((passed++))
    else
        echo -e "${YELLOW}! No active remaps${NC}"
        ((passed++))
    fi
    
    echo ""
    echo "Summary: $passed passed, $failed failed"
    
    if [ $failed -eq 0 ]; then
        echo -e "${GREEN}✓ System is healthy${NC}"
        return 0
    else
        echo -e "${RED}✗ System has issues${NC}"
        return 1
    fi
}

# Usage information
usage() {
    cat << EOF
Usage: $0 [DEVICE] [COMMAND] [OPTIONS]

DEVICE:
  Device name (default: dm-remap-test)

COMMAND:
  snapshot    - Single snapshot of current status (default)
  monitor     - Continuous monitoring
  health      - Health check

OPTIONS:
  For monitor command:
    -i, --interval N  Update interval in seconds (default: 5)

Examples:
  $0                          # Single snapshot of dm-remap-test
  $0 my-device snapshot       # Single snapshot of my-device
  $0 my-device monitor -i 2   # Monitor my-device every 2 seconds
  $0 my-device health         # Run health check

EOF
}

# Parse arguments
main() {
    # Check if first argument is a help request
    if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        usage
        exit 0
    fi
    
    # Parse device name and command
    if [ -n "$1" ]; then
        DEVICE_NAME="$1"
    fi
    
    local command="${2:-snapshot}"
    
    case "$command" in
        snapshot)
            snapshot
            ;;
        monitor)
            # Parse interval if provided
            if [ -n "$3" ] && [ "$3" = "-i" ] && [ -n "$4" ]; then
                UPDATE_INTERVAL="$4"
            fi
            monitor
            ;;
        health)
            snapshot
            health_check
            ;;
        *)
            echo "Unknown command: $command"
            usage
            exit 1
            ;;
    esac
}

# Run
main "$@"
