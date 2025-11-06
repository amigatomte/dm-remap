#!/bin/bash

################################################################################
# ZFS Mirror Pool with dm-remap Degradation Testing
#
# This script creates a ZFS mirror pool combining:
#   1. Sparse file loopback device (baseline)
#   2. dm-remap device with clean state (no bad sectors)
#
# Workflow:
#   1. Create clean dm-remap device
#   2. Create ZFS mirror pool
#   3. Run baseline tests (write/read/verify)
#   4. Inject bad sectors progressively
#   5. Observe ZFS resilience and recovery
#
# Usage:
#   sudo ./test-zfs-mirror-degradation.sh [OPTIONS]
#
# Options:
#   -p, --pool-name NAME       ZFS pool name (default: test-mirror)
#   -s, --pool-size SIZE       Pool device size in MB (default: 1024)
#   -b, --bad-sector-count N   Bad sectors to add per injection (default: 50)
#   -n, --num-stages N         Number of degradation stages (default: 5)
#   -d, --dry-run              Show what would be done without executing
#   -v, --verbose              Verbose output
#   -h, --help                 Show this help
#
################################################################################

set -euo pipefail

################################################################################
# Configuration
################################################################################

POOL_NAME="test-mirror"
POOL_SIZE=1024  # MB
BAD_SECTOR_COUNT=50
NUM_STAGES=5
VERBOSE=0
DRY_RUN=0

SETUP_SCRIPT="$(dirname "$0")/setup-dm-remap-test.sh"
LOOPBACK_FILE="/tmp/zfs-mirror-loopback.img"
LOOPBACK_DEVICE=""
REMAP_DEVICE="/dev/mapper/dm-test-remap"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

################################################################################
# Logging Functions
################################################################################

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

log_debug() {
    if (( VERBOSE )); then
        echo -e "${BLUE}[DEBUG]${NC} $*"
    fi
}

################################################################################
# Argument Parsing
################################################################################

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -p|--pool-name)
                POOL_NAME="$2"
                shift 2
                ;;
            -s|--pool-size)
                POOL_SIZE="$2"
                shift 2
                ;;
            -b|--bad-sector-count)
                BAD_SECTOR_COUNT="$2"
                shift 2
                ;;
            -n|--num-stages)
                NUM_STAGES="$2"
                shift 2
                ;;
            -d|--dry-run)
                DRY_RUN=1
                shift
                ;;
            -v|--verbose)
                VERBOSE=1
                shift
                ;;
            -h|--help)
                print_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                print_usage
                exit 1
                ;;
        esac
    done
}

print_usage() {
    cat << 'EOF'
ZFS Mirror Pool with dm-remap Degradation Testing

Usage: test-zfs-mirror-degradation.sh [OPTIONS]

Options:
  -p, --pool-name NAME        ZFS pool name (default: test-mirror)
  -s, --pool-size SIZE        Pool device size in MB (default: 1024)
  -b, --bad-sector-count N    Bad sectors per injection (default: 50)
  -n, --num-stages N          Degradation stages (default: 5)
  -d, --dry-run               Show what would be done
  -v, --verbose               Verbose output
  -h, --help                  Show this help

Examples:

  # Basic test with defaults
  sudo ./test-zfs-mirror-degradation.sh

  # Larger pool with more degradation stages
  sudo ./test-zfs-mirror-degradation.sh -s 2048 -n 8

  # Dry run to see what will happen
  sudo ./test-zfs-mirror-degradation.sh -d

  # Custom pool name with verbose output
  sudo ./test-zfs-mirror-degradation.sh -p my-pool -v

EOF
}

################################################################################
# Utility Functions
################################################################################

run_cmd() {
    local cmd="$*"
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] $cmd"
        return 0
    fi
    
    log_debug "Running: $cmd"
    if ! eval "$cmd"; then
        log_error "Command failed: $cmd"
        return 1
    fi
}

check_requirements() {
    log_info "Checking requirements..."
    
    local missing=0
    
    if ! command -v zfs &> /dev/null; then
        log_error "zfs command not found. Please install ZFS utilities."
        missing=1
    fi
    
    if ! command -v dmsetup &> /dev/null; then
        log_error "dmsetup command not found."
        missing=1
    fi
    
    if ! command -v losetup &> /dev/null; then
        log_error "losetup command not found."
        missing=1
    fi
    
    if [[ ! -x "$SETUP_SCRIPT" ]]; then
        log_error "Setup script not found or not executable: $SETUP_SCRIPT"
        missing=1
    fi
    
    if (( missing )); then
        log_error "Missing required tools. Cannot proceed."
        return 1
    fi
    
    log_success "All requirements met"
}

cleanup() {
    log_info "Cleaning up..."
    
    # Destroy ZFS pool if it exists
    if run_cmd "zpool list '$POOL_NAME' &> /dev/null"; then
        log_info "Destroying ZFS pool: $POOL_NAME"
        run_cmd "zpool destroy -f '$POOL_NAME'" || true
    fi
    
    # Cleanup dm-remap devices
    log_info "Cleaning up dm-remap setup..."
    run_cmd "cd '$(dirname "$SETUP_SCRIPT")' && ./setup-dm-remap-test.sh --cleanup" || true
    
    # Remove loopback file
    if [[ -f "$LOOPBACK_FILE" ]]; then
        log_info "Removing loopback file: $LOOPBACK_FILE"
        run_cmd "rm -f '$LOOPBACK_FILE'"
    fi
    
    log_success "Cleanup complete"
}

################################################################################
# Setup Functions
################################################################################

setup_loopback_device() {
    log_info "Setting up loopback device..."
    log_info "  File: $LOOPBACK_FILE"
    log_info "  Size: ${POOL_SIZE}M"
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] Would create sparse file and loopback device"
        LOOPBACK_DEVICE="/dev/loop99"
        return 0
    fi
    
    # Create sparse file
    run_cmd "dd if=/dev/zero of='$LOOPBACK_FILE' bs=1M count=0 seek=$POOL_SIZE"
    
    # Create loopback device
    LOOPBACK_DEVICE=$(losetup -f)
    run_cmd "losetup '$LOOPBACK_DEVICE' '$LOOPBACK_FILE'"
    
    log_success "Loopback device created: $LOOPBACK_DEVICE"
}

setup_dm_remap_device() {
    log_info "Setting up dm-remap device..."
    log_info "  Size: ${POOL_SIZE}M"
    log_info "  Bad sectors: 0 (clean)"
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] Would create clean dm-remap device"
        return 0
    fi
    
    # Create clean dm-remap device using setup script
    cd "$(dirname "$SETUP_SCRIPT")"
    
    if ! ./setup-dm-remap-test.sh --no-bad-sectors -s "${POOL_SIZE}M" -v; then
        log_error "Failed to create dm-remap device"
        return 1
    fi
    
    cd - > /dev/null
    
    log_success "dm-remap device created: $REMAP_DEVICE"
}

create_zfs_pool() {
    log_info "Creating ZFS mirror pool: $POOL_NAME"
    log_info "  Device 1 (baseline): $LOOPBACK_DEVICE"
    log_info "  Device 2 (dm-remap): $REMAP_DEVICE"
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] Would create mirror pool"
        return 0
    fi
    
    # Create mirror pool
    if ! run_cmd "zpool create -f '$POOL_NAME' mirror '$LOOPBACK_DEVICE' '$REMAP_DEVICE'"; then
        log_error "Failed to create ZFS pool"
        return 1
    fi
    
    # Wait for pool to be ready
    sleep 2
    
    log_success "ZFS pool created: $POOL_NAME"
    
    # Show pool status
    log_info "Pool status:"
    zpool status "$POOL_NAME" | sed 's/^/  /'
}

################################################################################
# Test Functions
################################################################################

run_baseline_tests() {
    log_info "Running baseline tests on clean pool..."
    
    local pool_mount="/$POOL_NAME"
    local test_file="$pool_mount/test_baseline.dat"
    local test_dir="$pool_mount/test_files"
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] Would run baseline tests"
        return 0
    fi
    
    # Create test data
    log_info "Writing test data (100MB)..."
    if ! dd if=/dev/urandom of="$test_file" bs=1M count=100 2>/dev/null; then
        log_error "Failed to write test data"
        return 1
    fi
    
    # Calculate checksum
    log_info "Calculating checksums..."
    local checksum_baseline=$(sha256sum "$test_file" | awk '{print $1}')
    log_info "  Baseline checksum: ${checksum_baseline:0:16}..."
    echo "$checksum_baseline" > "$pool_mount/.checksum_baseline"
    
    # Create test directory with multiple files
    mkdir -p "$test_dir"
    for i in {1..10}; do
        dd if=/dev/urandom of="$test_dir/file_$i.dat" bs=1M count=10 2>/dev/null
    done
    log_success "Created 10 test files (10MB each)"
    
    # Read and verify all data
    log_info "Reading and verifying test data..."
    local checksum_verify=$(sha256sum "$test_file" | awk '{print $1}')
    
    if [[ "$checksum_baseline" != "$checksum_verify" ]]; then
        log_error "Checksum mismatch! Data corruption detected!"
        return 1
    fi
    
    log_success "Baseline tests passed"
    log_info "  Test file: $test_file"
    log_info "  Test directory: $test_dir"
}

inject_bad_sectors() {
    local stage=$1
    local total_to_add=$((BAD_SECTOR_COUNT * stage))
    
    log_info "Stage $stage: Injecting bad sectors (total: ~$total_to_add bad sectors)..."
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] Would add $BAD_SECTOR_COUNT bad sectors"
        return 0
    fi
    
    cd "$(dirname "$SETUP_SCRIPT")"
    
    if ! ./setup-dm-remap-test.sh --add-bad-sectors -c "$BAD_SECTOR_COUNT" -v; then
        log_error "Failed to inject bad sectors"
        return 1
    fi
    
    cd - > /dev/null
    
    # Wait for device mapper to settle
    sleep 1
    
    log_success "Bad sectors injected"
}

run_degradation_tests() {
    local stage=$1
    local pool_mount="/$POOL_NAME"
    local test_file="$pool_mount/test_baseline.dat"
    local test_dir="$pool_mount/test_files"
    local stage_dir="$pool_mount/stage_${stage}"
    
    if (( DRY_RUN )); then
        log_debug "[DRY RUN] Would run degradation tests"
        return 0
    fi
    
    log_info "Stage $stage: Running filesystem tests..."
    
    # Show pool status
    log_info "  Pool status:"
    zpool status "$POOL_NAME" 2>/dev/null | sed 's/^/    /'
    
    # Show device mapper status
    log_info "  Device mapper status:"
    dmsetup status dm-test-remap 2>/dev/null | sed 's/^/    /'
    
    # Try to read baseline test file
    log_info "  Reading baseline test file..."
    if ! dd if="$test_file" of=/dev/null bs=1M 2>/dev/null; then
        log_warn "Could not read baseline test file (may be due to bad sectors)"
    else
        local checksum_stage=$(sha256sum "$test_file" | awk '{print $1}')
        local checksum_baseline=$(cat "$pool_mount/.checksum_baseline" 2>/dev/null || echo "unknown")
        
        if [[ "$checksum_stage" == "$checksum_baseline" ]]; then
            log_success "  Checksum verified (data integrity maintained)"
        else
            log_warn "  Checksum mismatch (possible data corruption or read errors)"
        fi
    fi
    
    # Write new test data for this stage
    mkdir -p "$stage_dir"
    log_info "  Writing stage test data..."
    if ! dd if=/dev/urandom of="$stage_dir/stage_${stage}.dat" bs=1M count=50 2>/dev/null; then
        log_warn "  Could not write stage test data (device may have I/O errors)"
    else
        log_success "  Stage data written successfully"
    fi
    
    # Try to read directory listing
    log_info "  Reading directory contents..."
    if ! ls -lah "$test_dir" > /dev/null 2>&1; then
        log_warn "  Could not list directory contents"
    else
        local file_count=$(ls "$test_dir" | wc -l)
        log_success "  Directory readable ($file_count files)"
    fi
    
    log_success "Stage $stage tests complete"
}

################################################################################
# Main Workflow
################################################################################

main() {
    log_info "========================================="
    log_info "ZFS Mirror Pool Degradation Testing"
    log_info "========================================="
    
    # Parse arguments
    parse_arguments "$@"
    
    # Show configuration
    log_info "Configuration:"
    log_info "  Pool name: $POOL_NAME"
    log_info "  Pool size: ${POOL_SIZE}MB"
    log_info "  Bad sectors per stage: $BAD_SECTOR_COUNT"
    log_info "  Degradation stages: $NUM_STAGES"
    log_info "  Verbose: $VERBOSE"
    log_info "  Dry run: $DRY_RUN"
    
    # Set up error trap
    trap cleanup EXIT
    
    # Check requirements
    check_requirements || exit 1
    
    # Setup phase
    log_info ""
    log_info "========== SETUP PHASE =========="
    setup_loopback_device
    setup_dm_remap_device
    create_zfs_pool
    
    # Baseline testing
    log_info ""
    log_info "========== BASELINE TESTING =========="
    run_baseline_tests
    
    # Progressive degradation
    log_info ""
    log_info "========== DEGRADATION TESTING =========="
    
    for stage in $(seq 1 "$NUM_STAGES"); do
        log_info ""
        log_info "--- Stage $stage/$NUM_STAGES ---"
        
        inject_bad_sectors "$stage"
        sleep 2
        run_degradation_tests "$stage"
    done
    
    # Final summary
    log_info ""
    log_info "========== FINAL STATUS =========="
    log_info "ZFS Pool Status:"
    if (( !DRY_RUN )); then
        zpool status "$POOL_NAME" 2>/dev/null | sed 's/^/  /'
    fi
    
    log_info ""
    log_success "Testing complete!"
}

################################################################################
# Entry Point
################################################################################

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
