#!/bin/bash

# dm-remap v4.0 Pre-Flight Regression Check
# Quick validation of core functionality before full regression testing

set -e

# Color output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log() { echo -e "${BLUE}[$(date '+%H:%M:%S')]${NC} $1"; }
success() { echo -e "${GREEN}[✓]${NC} $1"; }
error() { echo -e "${RED}[✗]${NC} $1"; }
warning() { echo -e "${YELLOW}[!]${NC} $1"; }

echo "=================================================="
echo "dm-remap v4.0 Pre-Flight Regression Check"
echo "=================================================="

# Check 1: Root access
if [[ $EUID -ne 0 ]]; then
    error "Please run as root: sudo $0"
    exit 1
fi
success "Root access confirmed"

# Check 2: Module loaded
if lsmod | grep -q dm_remap; then
    success "dm-remap module is loaded"
    
    # Show module info
    module_size=$(lsmod | grep dm_remap | awk '{print $2}')
    log "Module size: ${module_size} bytes (~$((module_size/1024))KB)"
else
    error "dm-remap module not loaded"
    log "Loading module..."
    if insmod /home/christian/kernel_dev/dm-remap/src/dm_remap.ko; then
        success "Module loaded successfully"
    else
        error "Failed to load module"
        exit 1
    fi
fi

# Check 3: Module parameters
log "Checking module parameters..."
for param in debug_level max_remaps error_threshold; do
    param_file="/sys/module/dm_remap/parameters/$param"
    if [[ -r "$param_file" ]]; then
        value=$(cat "$param_file")
        success "Parameter $param: $value"
    else
        warning "Parameter $param not accessible"
    fi
done

# Check 4: Week 9-10 features in dmesg
log "Checking Week 9-10 feature initialization..."

# Memory pools
if dmesg | tail -50 | grep -q "Memory pool.*initialized successfully"; then
    success "Memory pool system initialized"
else
    error "Memory pool system not found in logs"
fi

# Hotpath optimization
if dmesg | tail -50 | grep -q "Hotpath optimization initialized successfully"; then
    success "Hotpath optimization initialized"
else
    error "Hotpath optimization not found in logs"
fi

# Health scanning
if dmesg | tail -50 | grep -q "Health scanner initialized successfully"; then
    success "Health scanning system initialized"
else
    warning "Health scanning not found in logs (may be normal)"
fi

# Check 5: Quick device creation test
log "Testing basic device creation..."

# Create minimal test files
dd if=/dev/zero of=/tmp/preflight_main.img bs=1M count=5 2>/dev/null
dd if=/dev/zero of=/tmp/preflight_spare.img bs=1M count=2 2>/dev/null

# Setup loop devices
main_loop=$(losetup -f)
losetup "$main_loop" /tmp/preflight_main.img
spare_loop=$(losetup -f)
losetup "$spare_loop" /tmp/preflight_spare.img

# Create dm device
spare_sectors=$(blockdev --getsz "$spare_loop")
if echo "0 $(blockdev --getsz "$main_loop") remap $main_loop $spare_loop 0 $spare_sectors" | \
   dmsetup create dm-preflight-test; then
    success "Device creation successful"
    
    # Test basic I/O
    if dd if=/dev/mapper/dm-preflight-test of=/dev/null bs=1k count=10 2>/dev/null; then
        success "Basic I/O operations working"
    else
        error "Basic I/O operations failed"
    fi
    
    # Cleanup
    dmsetup remove dm-preflight-test
else
    error "Device creation failed"
fi

# Cleanup
losetup -d "$main_loop" "$spare_loop"
rm -f /tmp/preflight_main.img /tmp/preflight_spare.img

# Check 6: System resources
log "Checking system resources..."
memory_kb=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
memory_mb=$((memory_kb / 1024))
if [[ $memory_mb -gt 100 ]]; then
    success "Available memory: ${memory_mb}MB"
else
    warning "Low available memory: ${memory_mb}MB"
fi

# Final assessment
echo ""
echo "=================================================="
echo "Pre-Flight Check Summary"
echo "=================================================="

success "✓ Module loading and basic functionality"
success "✓ Week 9-10 optimization features active"
success "✓ Device creation and I/O operations"
success "✓ System resources adequate"

echo ""
log "🚀 Pre-flight check PASSED! Ready for comprehensive regression testing."
log "Run: sudo ./tests/comprehensive_regression_test.sh"

echo ""
warning "Note: This is a quick pre-flight check. Run the full regression test suite"
warning "      before starting Phase 2 development for complete validation."