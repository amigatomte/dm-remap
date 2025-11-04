#!/bin/bash

################################################################################
# dm-remap ZFS Compatibility Diagnostic Script
# 
# This script identifies why ZFS pool creation with dm-remap fails.
# It checks sector sizes, device properties, and kernel logs.
#
# Usage: sudo ./scripts/diagnose-zfs-compat.sh
#
################################################################################

set -o pipefail

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
ISSUES_FOUND=0
WARNINGS_FOUND=0

################################################################################
# Utility Functions
################################################################################

print_header() {
    echo -e "\n${BLUE}=== $1 ===${NC}\n"
}

print_check() {
    echo -ne "  Checking $1... "
}

print_pass() {
    echo -e "${GREEN}✓ PASS${NC}"
}

print_fail() {
    local msg="$1"
    echo -e "${RED}✗ FAIL${NC}"
    echo -e "    ${RED}Error: $msg${NC}"
    ((ISSUES_FOUND++))
}

print_warn() {
    local msg="$1"
    echo -e "${YELLOW}⚠ WARN${NC}"
    echo -e "    ${YELLOW}Warning: $msg${NC}"
    ((WARNINGS_FOUND++))
}

print_info() {
    echo -e "  ${BLUE}ℹ${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}Error: This script must be run as root (use sudo)${NC}"
        exit 1
    fi
}

get_device_property() {
    local device=$1
    local property=$2
    
    if [[ ! -b "$device" ]]; then
        echo "N/A (device not found)"
        return 1
    fi
    
    case "$property" in
        logical_sector)
            blockdev --getss "$device" 2>/dev/null || echo "N/A"
            ;;
        physical_block)
            blockdev --getpbsz "$device" 2>/dev/null || echo "N/A"
            ;;
        size_sectors)
            blockdev --getsz "$device" 2>/dev/null || echo "N/A"
            ;;
        size_mb)
            local sectors=$(blockdev --getsz "$device" 2>/dev/null)
            if [[ -n "$sectors" ]]; then
                echo "scale=1; $sectors * 512 / 1048576" | bc 2>/dev/null || echo "N/A"
            else
                echo "N/A"
            fi
            ;;
        *)
            echo "N/A"
            ;;
    esac
}

################################################################################
# Checks
################################################################################

check_prerequisites() {
    print_header "Checking Prerequisites"
    
    print_check "sudo/root access"
    if [[ $EUID -eq 0 ]]; then
        print_pass
    else
        print_fail "Script must be run as root"
        exit 1
    fi
    
    print_check "blockdev utility"
    if command -v blockdev &> /dev/null; then
        print_pass
    else
        print_fail "blockdev not found (install with: apt-get install util-linux)"
    fi
    
    print_check "dmsetup utility"
    if command -v dmsetup &> /dev/null; then
        print_pass
    else
        print_fail "dmsetup not found"
    fi
    
    print_check "bc utility (for calculations)"
    if command -v bc &> /dev/null; then
        print_pass
    else
        print_warn "bc not found - size calculations will be skipped (not critical)"
    fi
}

check_devices_exist() {
    print_header "Checking Device Existence"
    
    print_check "/dev/loop2 (healthy reference)"
    if [[ -b /dev/loop2 ]]; then
        print_pass
    else
        print_fail "/dev/loop2 does not exist - setup may be incomplete"
        return 1
    fi
    
    print_check "/dev/mapper/dm-test-remap (dm-remap device)"
    if [[ -b /dev/mapper/dm-test-remap ]]; then
        print_pass
    else
        print_fail "/dev/mapper/dm-test-remap does not exist - dm-remap not loaded"
        print_info "Create it with: cd tests/loopback-setup && sudo ./setup-dm-remap-test.sh"
        return 1
    fi
}

check_sector_sizes() {
    print_header "Checking Sector Sizes (CRITICAL)"
    
    local loop2_logical=$(get_device_property /dev/loop2 logical_sector)
    local remap_logical=$(get_device_property /dev/mapper/dm-test-remap logical_sector)
    
    print_check "loop2 logical sector size"
    if [[ "$loop2_logical" != "N/A" ]]; then
        echo -e "${GREEN}✓${NC} $loop2_logical bytes"
    else
        echo -e "${RED}✗${NC} Could not determine"
        return 1
    fi
    
    print_check "dm-remap logical sector size"
    if [[ "$remap_logical" != "N/A" ]]; then
        echo -e "${GREEN}✓${NC} $remap_logical bytes"
    else
        echo -e "${RED}✗${NC} Could not determine"
        return 1
    fi
    
    # Compare
    print_check "Sector size match (loop2 vs dm-remap)"
    if [[ "$loop2_logical" == "$remap_logical" ]]; then
        print_pass
        print_info "Both use $loop2_logical-byte sectors (good for ZFS)"
        if [[ "$loop2_logical" -eq 512 ]]; then
            print_info "512-byte sectors: Standard, compatible with most ZFS configurations"
        elif [[ "$loop2_logical" -eq 4096 ]]; then
            print_info "4096-byte sectors: Modern alignment, ensure ZFS uses ashift=12"
        fi
    else
        print_fail "Sector size mismatch!"
        print_info "  loop2:     $loop2_logical bytes"
        print_info "  dm-remap:  $remap_logical bytes"
        print_info "  This is likely why ZFS pool creation failed"
        print_info "  FIX: Recreate dm-remap with: sudo ./setup-dm-remap-test.sh --sector-size $loop2_logical"
    fi
}

check_physical_block_sizes() {
    print_header "Checking Physical Block Sizes"
    
    local loop2_phys=$(get_device_property /dev/loop2 physical_block)
    local remap_phys=$(get_device_property /dev/mapper/dm-test-remap physical_block)
    
    print_info "loop2 physical block size: $loop2_phys bytes"
    print_info "dm-remap physical block size: $remap_phys bytes"
    
    if [[ "$loop2_phys" == "$remap_phys" ]]; then
        print_pass
    else
        print_warn "Physical block sizes differ - may affect performance but not ZFS creation"
    fi
}

check_device_sizes() {
    print_header "Checking Device Sizes"
    
    local loop2_sectors=$(get_device_property /dev/loop2 size_sectors)
    local remap_sectors=$(get_device_property /dev/mapper/dm-test-remap size_sectors)
    
    local loop2_mb=$(get_device_property /dev/loop2 size_mb)
    local remap_mb=$(get_device_property /dev/mapper/dm-test-remap size_mb)
    
    print_info "loop2 size: $loop2_sectors sectors (~${loop2_mb}MB)"
    print_info "dm-remap size: $remap_sectors sectors (~${remap_mb}MB)"
    
    # ZFS uses smaller device, so it's OK if they differ slightly
    if [[ "$loop2_sectors" -gt 0 && "$remap_sectors" -gt 0 ]]; then
        if [[ $((loop2_sectors - remap_sectors)) -lt 1000 ]]; then
            print_pass
            print_info "Sizes are compatible"
        else
            print_warn "Significant size difference detected - ZFS will use the smaller device"
        fi
    fi
}

check_device_queue_properties() {
    print_header "Checking Kernel Queue Properties"
    
    # Find dm device number
    local dm_device=$(dmsetup ls 2>/dev/null | grep "dm-test-remap" | awk '{print $NF}' | tr -d '()' || echo "")
    
    if [[ -z "$dm_device" ]]; then
        print_warn "Could not determine dm device number - skipping detailed queue checks"
        return
    fi
    
    print_info "Found dm device: $dm_device"
    
    # Extract device number
    local major=$(echo "$dm_device" | cut -d: -f1)
    local minor=$(echo "$dm_device" | cut -d: -f2)
    
    # Find dm-X device in sysfs
    local sysfs_path=""
    for d in /sys/block/dm-*; do
        if [[ -f "$d/dev" ]]; then
            if grep -q "$major:$minor" "$d/dev" 2>/dev/null; then
                sysfs_path="$d"
                break
            fi
        fi
    done
    
    if [[ -z "$sysfs_path" ]]; then
        print_warn "Could not locate dm device in sysfs"
        return
    fi
    
    print_info "dm device sysfs path: $sysfs_path"
    
    # Check queue properties
    if [[ -f "$sysfs_path/queue/logical_block_size" ]]; then
        local logical=$(cat "$sysfs_path/queue/logical_block_size")
        print_info "Kernel reported logical block size: $logical bytes"
    fi
    
    if [[ -f "$sysfs_path/queue/physical_block_size" ]]; then
        local physical=$(cat "$sysfs_path/queue/physical_block_size")
        print_info "Kernel reported physical block size: $physical bytes"
    fi
}

check_loopback_device_properties() {
    print_header "Checking Loopback Device Properties"
    
    if losetup -l &>/dev/null; then
        print_info "Active loopback devices:"
        losetup -l 2>/dev/null | tail -n +2 | while read line; do
            echo "    $line"
        done
    else
        print_warn "Could not list loopback devices"
    fi
}

check_zfs_presence() {
    print_header "Checking ZFS Availability"
    
    print_check "ZFS utilities"
    if command -v zpool &> /dev/null && command -v zfs &> /dev/null; then
        print_pass
        
        print_check "ZFS kernel module"
        if grep -q zfs /proc/modules 2>/dev/null; then
            print_pass
            print_info "ZFS is loaded and ready"
        else
            print_warn "ZFS kernel module not loaded (install with: sudo apt-get install zfsutils-linux)"
        fi
    else
        print_warn "ZFS not installed (zpool/zfs commands not found)"
        print_info "Install with: sudo apt-get install zfsutils-linux"
    fi
}

check_kernel_logs() {
    print_header "Checking Kernel Logs for Errors"
    
    print_info "Recent kernel messages related to dm-remap or I/O:"
    
    local errors=$(dmesg 2>/dev/null | grep -i "remap\|error\|i/o\|zfs" | tail -20 || echo "")
    
    if [[ -n "$errors" ]]; then
        echo "$errors" | while read line; do
            if echo "$line" | grep -qi "error\|failed\|reject"; then
                echo -e "    ${RED}✗ $line${NC}"
            elif echo "$line" | grep -qi "remap"; then
                echo -e "    ${BLUE}ℹ $line${NC}"
            else
                echo "    $line"
            fi
        done
    else
        print_info "No relevant kernel messages found"
    fi
    
    print_info "For full kernel log, run: sudo dmesg | tail -50"
}

check_dm_remap_status() {
    print_header "Checking dm-remap Device Status"
    
    if dmsetup exists dm-test-remap 2>/dev/null; then
        print_info "dm-remap device table:"
        dmsetup table dm-test-remap 2>/dev/null | while read line; do
            echo "    $line"
        done
        
        print_info "dm-remap status:"
        dmsetup status dm-test-remap 2>/dev/null | while read line; do
            echo "    $line"
        done
    else
        print_warn "dm-remap not found in dmsetup"
    fi
}

check_io_performance() {
    print_header "Quick I/O Performance Check"
    
    if [[ ! -b /dev/mapper/dm-test-remap ]]; then
        print_warn "dm-remap not available - skipping I/O check"
        return
    fi
    
    print_check "Basic I/O read test"
    if timeout 5 dd if=/dev/mapper/dm-test-remap of=/dev/null bs=4K count=100 2>/dev/null; then
        print_pass
        print_info "Device is readable"
    else
        print_fail "Cannot read from dm-remap device"
    fi
}

################################################################################
# Recommendations
################################################################################

print_recommendations() {
    print_header "RECOMMENDATIONS"
    
    if [[ $ISSUES_FOUND -eq 0 && $WARNINGS_FOUND -eq 0 ]]; then
        echo -e "${GREEN}All checks passed! Your setup should be compatible with ZFS.${NC}"
        echo ""
        echo "If ZFS pool creation still fails, possible causes:"
        echo "  1. Different dm-remap and loop device actual implementations"
        echo "  2. ZFS version incompatibilities"
        echo "  3. Kernel module issues"
        echo ""
        echo "Try with explicit ashift parameter:"
        echo "  sudo zpool create -o ashift=9 mypool mirror \\"
        echo "      /dev/mapper/dm-test-remap /dev/loop2"
        echo ""
        return
    fi
    
    if [[ $ISSUES_FOUND -gt 0 ]]; then
        echo -e "${RED}Found $ISSUES_FOUND critical issue(s) to fix:${NC}\n"
        
        # Check specific issues
        local loop2_logical=$(get_device_property /dev/loop2 logical_sector)
        local remap_logical=$(get_device_property /dev/mapper/dm-test-remap logical_sector)
        
        if [[ "$loop2_logical" != "$remap_logical" ]]; then
            echo "1. SECTOR SIZE MISMATCH (Most Likely Cause of ZFS Failure)"
            echo "   Problem: loop2 has $loop2_logical-byte sectors, dm-remap has $remap_logical-byte sectors"
            echo "   Fix: Rebuild dm-remap with matching sector size"
            echo ""
            echo "   Run these commands:"
            echo "   ${BLUE}sudo dmsetup remove dm-test-remap dm-test-linear${NC}"
            echo "   ${BLUE}sudo losetup -d /dev/loop0 /dev/loop1 /dev/loop2${NC}"
            echo "   ${BLUE}cd tests/loopback-setup${NC}"
            echo "   ${BLUE}sudo ./setup-dm-remap-test.sh --sector-size $loop2_logical -c 10${NC}"
            echo ""
            echo "   Then retry ZFS pool creation:"
            echo "   ${BLUE}sudo zpool create mynewpool mirror \\${NC}"
            echo "   ${BLUE}    /dev/mapper/dm-test-remap /dev/loop2${NC}"
            echo ""
        fi
        
        if [[ ! -b /dev/mapper/dm-test-remap ]]; then
            echo "2. dm-remap DEVICE NOT FOUND"
            echo "   Problem: /dev/mapper/dm-test-remap does not exist"
            echo "   Fix: Create it by running setup script"
            echo ""
            echo "   ${BLUE}cd tests/loopback-setup${NC}"
            echo "   ${BLUE}sudo ./setup-dm-remap-test.sh${NC}"
            echo ""
        fi
    fi
    
    if [[ $WARNINGS_FOUND -gt 0 ]]; then
        echo -e "${YELLOW}Found $WARNINGS_FOUND warning(s) - may need attention:${NC}\n"
        echo "  • Physical block size differences may affect performance"
        echo "  • ZFS not installed - needed to create pool"
        echo "  • Device size differences - ZFS will use smaller device"
        echo ""
    fi
}

################################################################################
# Main Execution
################################################################################

main() {
    echo -e "${BLUE}"
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║     dm-remap ZFS Compatibility Diagnostic Tool                ║"
    echo "║     Diagnosing why: cannot create 'mynewpool': I/O error     ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
    
    check_prerequisites
    check_devices_exist || exit 1
    check_sector_sizes
    check_physical_block_sizes
    check_device_sizes
    check_device_queue_properties
    check_loopback_device_properties
    check_zfs_presence
    check_dm_remap_status
    check_io_performance
    check_kernel_logs
    
    print_recommendations
    
    echo ""
    print_header "Diagnostic Summary"
    echo -e "Issues Found: ${RED}$ISSUES_FOUND${NC}"
    echo -e "Warnings: ${YELLOW}$WARNINGS_FOUND${NC}"
    
    if [[ $ISSUES_FOUND -gt 0 ]]; then
        echo -e "\n${RED}Status: ISSUES DETECTED - Follow recommendations above${NC}"
        exit 1
    elif [[ $WARNINGS_FOUND -gt 0 ]]; then
        echo -e "\n${YELLOW}Status: NO CRITICAL ISSUES - Some warnings to review${NC}"
        exit 0
    else
        echo -e "\n${GREEN}Status: ALL CHECKS PASSED${NC}"
        exit 0
    fi
}

main "$@"

if [ "$LOOP2_SS" = "ERROR" ] || [ "$REMAP_SS" = "ERROR" ]; then
    log_error "Failed to read sector sizes"
    exit 1
fi

if [ "$LOOP2_SS" = "$REMAP_SS" ]; then
    log_info "Sector sizes match!"
else
    log_error "Sector size mismatch: $LOOP2_SS vs $REMAP_SS"
    echo ""
    echo "LIKELY CAUSE: ZFS requires matching sector sizes across mirror devices"
    echo ""
    echo "FIX: Recreate dm-remap with matching sector size:"
    echo "  sudo dmsetup remove dm-test-remap"
    echo "  sudo dmsetup remove dm-test-linear"
    echo "  sudo ./setup-dm-remap-test.sh --sector-size $LOOP2_SS -c 10"
    exit 1
fi

echo "=== DEVICE SIZES ==="
echo ""

LOOP2_SZ=$(blockdev --getsz /dev/loop2)
REMAP_SZ=$(blockdev --getsz /dev/mapper/dm-test-remap)

echo "  /dev/loop2:                $LOOP2_SZ sectors ($(( LOOP2_SZ * 512 / 1024 / 1024 )) MB)"
echo "  /dev/mapper/dm-test-remap: $REMAP_SZ sectors ($(( REMAP_SZ * 512 / 1024 / 1024 )) MB)"
echo ""

if [ "$LOOP2_SZ" = "$REMAP_SZ" ]; then
    log_info "Device sizes match!"
elif [ "$LOOP2_SZ" -gt "$REMAP_SZ" ] || [ "$LOOP2_SZ" -lt "$REMAP_SZ" ]; then
    log_warn "Device sizes differ: ZFS will use smaller size"
    DIFF=$(( LOOP2_SZ > REMAP_SZ ? LOOP2_SZ - REMAP_SZ : REMAP_SZ - LOOP2_SZ ))
    echo "   Difference: $DIFF sectors"
fi

echo "=== PHYSICAL BLOCK SIZES ==="
echo ""

LOOP2_PBS=$(blockdev --getpbsz /dev/loop2)
REMAP_PBS=$(blockdev --getpbsz /dev/mapper/dm-test-remap)

echo "  /dev/loop2:                $LOOP2_PBS bytes"
echo "  /dev/mapper/dm-test-remap: $REMAP_PBS bytes"
echo ""

if [ "$LOOP2_PBS" = "$REMAP_PBS" ]; then
    log_info "Physical block sizes match!"
else
    log_warn "Physical block sizes differ: $LOOP2_PBS vs $REMAP_PBS"
fi

echo "=== QUEUE PARAMETERS ==="
echo ""

# Check /sys/block settings
LOOP2_LBS=$(cat /sys/block/loop2/queue/logical_block_size 2>/dev/null || echo "unknown")
LOOP2_PBS_SYS=$(cat /sys/block/loop2/queue/physical_block_size 2>/dev/null || echo "unknown")

echo "  Loop2 (from /sys/block):"
echo "    Logical block size:  $LOOP2_LBS bytes"
echo "    Physical block size: $LOOP2_PBS_SYS bytes"
echo ""

# Find dm device name
DM_DEV=$(dmsetup ls | grep dm-test-remap | awk '{print $2}' | tr -d '()')
if [ -z "$DM_DEV" ]; then
    log_warn "Could not find dm-remap in dmsetup output"
else
    if [ -d "/sys/block/$DM_DEV" ]; then
        REMAP_LBS=$(cat /sys/block/$DM_DEV/queue/logical_block_size 2>/dev/null || echo "unknown")
        REMAP_PBS_SYS=$(cat /sys/block/$DM_DEV/queue/physical_block_size 2>/dev/null || echo "unknown")
        
        echo "  dm-remap (from /sys/block/$DM_DEV):"
        echo "    Logical block size:  $REMAP_LBS bytes"
        echo "    Physical block size: $REMAP_PBS_SYS bytes"
        echo ""
        
        if [ "$LOOP2_LBS" != "$REMAP_LBS" ]; then
            log_warn "Logical block size mismatch in sysfs"
        else
            log_info "Logical block sizes match in sysfs"
        fi
    fi
fi

echo "=== DEVICE MAPPER INFO ==="
echo ""
dmsetup info dm-test-remap
echo ""

echo "=== RECENT KERNEL MESSAGES ==="
echo ""
dmesg | tail -20
echo ""

echo "=== RECOMMENDATION ==="
echo ""

if [ "$LOOP2_SS" != "$REMAP_SS" ]; then
    log_error "Sector size mismatch is preventing ZFS from using dm-remap"
    echo ""
    echo "To fix:"
    echo "  1. Remove current dm-remap setup:"
    echo "     sudo dmsetup remove dm-test-remap dm-test-linear"
    echo "     sudo losetup -d /dev/loop0 /dev/loop1 /dev/loop2"
    echo ""
    echo "  2. Recreate with matching sector sizes (512 bytes):"
    echo "     cd tests/loopback-setup"
    echo "     sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10 -M 100M -S 20M"
    echo ""
    echo "  3. Try ZFS pool creation again:"
    echo "     sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2"
else
    log_info "Basic device parameters look correct"
    echo ""
    echo "If ZFS still fails, try:"
    echo ""
    echo "  Option 1: Create pool with explicit ashift:"
    echo "    sudo zpool create -o ashift=9 mynewpool mirror \\"
    echo "      /dev/mapper/dm-test-remap /dev/loop2"
    echo ""
    echo "  Option 2: Test dm-remap alone first:"
    echo "    sudo zpool create testpool /dev/mapper/dm-test-remap"
    echo ""
    echo "  Option 3: Check kernel logs for specific errors:"
    echo "    sudo dmesg | tail -50"
fi

echo ""
echo "================================================================"
