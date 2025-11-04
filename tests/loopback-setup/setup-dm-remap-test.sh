#!/bin/bash
#
# dm-remap Test Setup Script
# 
# Creates a test environment with loopback devices simulating bad sectors
# that dm-remap can exercise its remapping functionality against.
#
# Features:
#  - Creates sparse or regular loopback image files
#  - Configures bad sectors (via text file, fixed number, or percentage)
#  - Sets up dm-linear devices with error zones for bad sectors
#  - Auto-generates dm-remap configuration
#  - Provides testing and verification commands
#
# Usage: ./setup-dm-remap-test.sh [OPTIONS]
#

set -euo pipefail

###############################################################################
# CONFIGURATION DEFAULTS
###############################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_IMG="main.img"
SPARE_IMG="spare.img"
MAIN_SIZE="100M"
SPARE_SIZE="20M"
USE_SPARSE=1
BAD_SECTORS_FILE=""
BAD_SECTORS_COUNT=""
BAD_SECTORS_PERCENT=""
SECTOR_SIZE=4096  # Default sector size (bytes)
DRY_RUN=0
VERBOSE=0
CLEANUP_ONLY=0
OUTPUT_SCRIPT=""  # File to write setup commands to

# Loopback device names (will be auto-detected if not set)
MAIN_LOOP=""
SPARE_LOOP=""

# Device mapper names
DM_LINEAR_NAME="dm-test-linear"
DM_REMAP_NAME="dm-test-remap"

###############################################################################
# UTILITY FUNCTIONS
###############################################################################

log_info() {
    echo "[INFO] $*" >&2
}

log_warn() {
    echo "[WARN] $*" >&2
}

log_error() {
    echo "[ERROR] $*" >&2
}

log_debug() {
    if (( VERBOSE )); then
        echo "[DEBUG] $*" >&2
    fi
}

die() {
    log_error "$*"
    exit 1
}

require_root() {
    if (( EUID != 0 )); then
        die "This script must be run as root"
    fi
}

# Log a command for output script
# Usage: cmd_log "command description" "actual command"
cmd_log() {
    local desc="$1"
    local cmd="$2"
    
    log_info "$desc"
    log_debug "$ $cmd"
    
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        echo "# $desc" >> "$OUTPUT_SCRIPT"
        echo "$cmd" >> "$OUTPUT_SCRIPT"
        echo "" >> "$OUTPUT_SCRIPT"
    fi
    
    if (( ! DRY_RUN )); then
        eval "$cmd"
    fi
}

# Execute a command and log it
# Usage: cmd_exec "command"
cmd_exec() {
    local cmd="$1"
    
    log_debug "$ $cmd"
    
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        echo "$cmd" >> "$OUTPUT_SCRIPT"
    fi
    
    if (( ! DRY_RUN )); then
        eval "$cmd"
    fi
}

print_usage() {
    cat << 'EOF'
Usage: ./setup-dm-remap-test.sh [OPTIONS]

DESCRIPTION:
  Creates a test environment for dm-remap with loopback devices and simulated
  bad sectors. Supports multiple methods for specifying bad sectors.

OPTIONS:
  -m, --main FILE              Main image file (default: main.img)
  -s, --spare FILE             Spare image file (default: spare.img)
  -M, --main-size SIZE         Main device size (default: 100M)
                               Examples: 100M, 1G, 512K
  -S, --spare-size SIZE        Spare device size (default: 20M)
  
  --sparse                     Use sparse files (default: yes)
  --no-sparse                  Use non-sparse (pre-allocated) files
  
  BAD SECTOR OPTIONS (choose one):
    -f, --bad-sectors-file F   Text file with sector IDs (one per line)
    -c, --bad-sectors-count N  Create N random bad sectors
    -p, --bad-sectors-percent P Create bad sectors for P% of sectors
  
  OTHER OPTIONS:
    --sector-size SIZE         Sector size in bytes (default: 4096)
    --output-script FILE       Write all setup commands to FILE for later execution
    --dry-run                  Show what would be done, don't do it
    -v, --verbose              Verbose output
    --cleanup                  Remove all test artifacts and exit
    -h, --help                 Show this help message

EXAMPLES:
  # Create test setup with 100 random bad sectors
  ./setup-dm-remap-test.sh -c 100
  
  # Create sparse 1GB main + 200MB spare with 5% bad sectors
  ./setup-dm-remap-test.sh -M 1G -S 200M -p 5
  
  # Use specific bad sectors from file
  ./setup-dm-remap-test.sh -f bad_sectors.txt
  
  # Dry run to see commands without executing
  ./setup-dm-remap-test.sh -c 50 --dry-run -v
  
  # Generate setup script without executing
  ./setup-dm-remap-test.sh -c 50 --output-script setup.sh --dry-run
  
  # Execute generated script
  sudo bash setup.sh
  
  # Clean up all test artifacts
  ./setup-dm-remap-test.sh --cleanup

BAD SECTORS FILE FORMAT:
  Text file with one sector ID per line:
    # This is a comment
    0
    100
    500
    1000

SECTOR ID SPECIFICATION:
  Sector IDs are device-relative LBA (Logical Block Address) values.
  With default 4KB sectors:
    - Sector 0 = bytes 0-4095
    - Sector 1 = bytes 4096-8191
    - Sector 100 = bytes 409600-413695
  
  Calculate sector from byte offset: OFFSET / 4096

EOF
}

print_help_detailed() {
    cat << 'EOF'

THEORY OF OPERATION:
  
  1. IMAGE FILE CREATION
     Creates one or more image files (sparse or regular) that will be
     used as backing stores for loopback devices.
  
  2. LOOPBACK DEVICE SETUP
     Attaches image files to loopback devices (e.g., /dev/loop0).
     These appear as regular block devices to the system.
  
  3. BAD SECTOR MARKING
     Specifies which sectors should simulate bad blocks.
     Three methods supported:
     - List in text file
     - Random count (e.g., 100 bad sectors)
     - Percentage distribution (e.g., 5% bad sectors)
  
  4. DM-LINEAR SETUP
     Creates device mapper "linear" target that passes through I/O
     to the main loopback device. For bad sectors, it uses "error"
     target that returns I/O errors on access.
  
     Layout:
       Sectors 0-99:        linear -> main loopback
       Sector 100:          error (bad sector)
       Sectors 101-999:     linear -> main loopback
       Sector 1000:         error (bad sector)
       etc.
  
  5. DM-REMAP SETUP
     Creates device mapper "remap" target that:
     - Detects writes to bad sectors
     - Remaps them to spare pool storage
     - Maintains metadata about which sectors are remapped
     - Provides health monitoring
  
  6. TESTING
     Applications can write to the dm-remap device. When they try to
     access bad sectors, dm-remap intercepts the errors and remaps
     the I/O to spare storage.

TEST WORKFLOW:
  
  1. Run: ./setup-dm-remap-test.sh -c 100
  
  2. Creates:
     - main.img (sparse, 100M)
     - spare.img (sparse, 20M)
     - /dev/loop0 -> main.img
     - /dev/loop1 -> spare.img
     - /dev/mapper/dm-test-linear (dm-linear with bad sectors)
     - /dev/mapper/dm-test-remap (dm-remap mapping)
  
  3. Load dm-remap module (if not already loaded)
  
  4. Test with:
     - dd if=/dev/urandom of=/dev/mapper/dm-test-remap bs=4K count=1000
     - Observe remapping of bad sectors
     - Check health status: dmsetup status dm-test-remap
     - View metadata: dmsetup table dm-test-remap

VERIFICATION:
  
  After setup, check status:
    dmsetup ls                           # List all device mappings
    dmsetup table dm-test-linear         # Show linear mapping
    dmsetup table dm-test-remap          # Show remap configuration
    losetup -a                           # Show loopback devices
    ls -lh main.img spare.img           # Check image file sizes

CLEANUP:
  
  Run: ./setup-dm-remap-test.sh --cleanup
  
  Or manually:
    dmsetup remove dm-test-remap        # Remove remap device
    dmsetup remove dm-test-linear       # Remove linear device
    losetup -d /dev/loop0               # Detach main.img
    losetup -d /dev/loop1               # Detach spare.img
    rm -f main.img spare.img            # Remove image files

TROUBLESHOOTING:
  
  "Device or resource busy":
    - Check: lsof +D .
    - Ensure no processes have files open
    - Try: sync; sleep 1
  
  "Sector size mismatch":
    - Verify: blockdev --getss /dev/loop0
    - Use: --sector-size to match filesystem
  
  "dm-remap module not found":
    - Load: modprobe dm-remap
    - Or build from source
  
  "Permission denied":
    - Run with sudo: sudo ./setup-dm-remap-test.sh

PERFORMANCE NOTES:
  
  Sparse vs Regular Files:
    - Sparse: Faster creation, uses less disk initially
    - Regular: May perform better with I/O, preallocates space
    - Default: Sparse (recommended for testing)
  
  Bad Sector Distribution:
    - Few sectors: Good for targeted testing
    - Many sectors: Tests worst-case behavior
    - Random: More realistic scenario
    - Clustered: Use text file to create patterns

EOF
}

###############################################################################
# ARGUMENT PARSING
###############################################################################

parse_arguments() {
    while (( $# > 0 )); do
        case "$1" in
            -m|--main)
                MAIN_IMG="$2"
                shift 2
                ;;
            -s|--spare)
                SPARE_IMG="$2"
                shift 2
                ;;
            -M|--main-size)
                MAIN_SIZE="$2"
                shift 2
                ;;
            -S|--spare-size)
                SPARE_SIZE="$2"
                shift 2
                ;;
            --sparse)
                USE_SPARSE=1
                shift
                ;;
            --no-sparse)
                USE_SPARSE=0
                shift
                ;;
            -f|--bad-sectors-file)
                BAD_SECTORS_FILE="$2"
                shift 2
                ;;
            -c|--bad-sectors-count)
                BAD_SECTORS_COUNT="$2"
                shift 2
                ;;
            -p|--bad-sectors-percent)
                BAD_SECTORS_PERCENT="$2"
                shift 2
                ;;
            --sector-size)
                SECTOR_SIZE="$2"
                shift 2
                ;;
            --output-script)
                OUTPUT_SCRIPT="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN=1
                shift
                ;;
            -v|--verbose)
                VERBOSE=1
                shift
                ;;
            --cleanup)
                CLEANUP_ONLY=1
                shift
                ;;
            -h|--help)
                print_usage
                print_help_detailed
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

###############################################################################
# VALIDATION
###############################################################################

validate_arguments() {
    # Check for mutually exclusive options
    local bad_sector_opts=0
    [[ -n "$BAD_SECTORS_FILE" ]] && bad_sector_opts=$((bad_sector_opts + 1))
    [[ -n "$BAD_SECTORS_COUNT" ]] && bad_sector_opts=$((bad_sector_opts + 1))
    [[ -n "$BAD_SECTORS_PERCENT" ]] && bad_sector_opts=$((bad_sector_opts + 1))
    
    if (( bad_sector_opts > 1 )); then
        die "Specify only one of: --bad-sectors-file, --bad-sectors-count, --bad-sectors-percent"
    fi
    
    if (( bad_sector_opts == 0 )); then
        die "Must specify bad sectors via: --bad-sectors-file, --bad-sectors-count, or --bad-sectors-percent"
    fi
    
    # Validate bad sectors file if specified
    if [[ -n "$BAD_SECTORS_FILE" ]]; then
        if [[ ! -f "$BAD_SECTORS_FILE" ]]; then
            die "Bad sectors file not found: $BAD_SECTORS_FILE"
        fi
    fi
    
    # Validate counts
    if [[ -n "$BAD_SECTORS_COUNT" ]]; then
        if ! [[ "$BAD_SECTORS_COUNT" =~ ^[0-9]+$ ]]; then
            die "Bad sectors count must be a number: $BAD_SECTORS_COUNT"
        fi
        if (( BAD_SECTORS_COUNT == 0 )); then
            die "Bad sectors count must be > 0"
        fi
    fi
    
    if [[ -n "$BAD_SECTORS_PERCENT" ]]; then
        if ! [[ "$BAD_SECTORS_PERCENT" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
            die "Bad sectors percent must be a number: $BAD_SECTORS_PERCENT"
        fi
        # Check range would go here, but allowing any percent for flexibility
    fi
}

###############################################################################
# SIZE CONVERSION
###############################################################################

# Convert human-readable size to bytes
size_to_bytes() {
    local size="$1"
    
    case "$size" in
        *K|*k)
            echo $((${size%[Kk]} * 1024))
            ;;
        *M|*m)
            echo $((${size%[Mm]} * 1024 * 1024))
            ;;
        *G|*g)
            echo $((${size%[Gg]} * 1024 * 1024 * 1024))
            ;;
        *T|*t)
            echo $((${size%[Tt]} * 1024 * 1024 * 1024 * 1024))
            ;;
        *)
            if [[ "$size" =~ ^[0-9]+$ ]]; then
                echo "$size"
            else
                die "Invalid size format: $size"
            fi
            ;;
    esac
}

# Convert bytes to human-readable
bytes_to_human() {
    local bytes=$1
    local units=("B" "KB" "MB" "GB" "TB")
    local unit_idx=0
    local value=$bytes
    
    while (( value > 1024 && unit_idx < 4 )); do
        value=$((value / 1024))
        (( unit_idx++ ))
    done
    
    echo "${value}${units[$unit_idx]}"
}

###############################################################################
# IMAGE FILE CREATION
###############################################################################

create_image_file() {
    local filename="$1"
    local size="$2"
    
    log_info "Creating image file: $filename ($(bytes_to_human $(size_to_bytes "$size")))"
    
    if [[ -f "$filename" ]] && ! (( DRY_RUN )); then
        log_warn "File already exists: $filename (skipping)"
        return
    fi
    
    if (( USE_SPARSE )); then
        # Create sparse file
        cmd_exec "truncate -s \"$size\" \"$filename\""
        cmd_exec "chmod 600 \"$filename\""
        if ! (( DRY_RUN )); then
            log_debug "Created sparse file: $filename"
        fi
    else
        # Create regular file filled with zeros
        local bytes=$(size_to_bytes "$size")
        cmd_exec "dd if=/dev/zero of=\"$filename\" bs=1M count=$((bytes / (1024*1024))) 2>/dev/null || dd if=/dev/zero of=\"$filename\" bs=\"$size\" count=1 2>/dev/null"
        cmd_exec "chmod 600 \"$filename\""
        if ! (( DRY_RUN )); then
            log_debug "Created regular file: $filename"
        fi
    fi
}

###############################################################################
# LOOPBACK DEVICE SETUP
###############################################################################

setup_loopback() {
    local image_file="$1"
    
    log_info "Setting up loopback device for: $image_file"
    
    if (( DRY_RUN )); then
        log_debug "DRY RUN: Would setup loopback for $image_file"
        echo "/dev/loopX"
        return
    fi
    
    local loop_device=$(losetup -f)
    if [[ -z "$loop_device" ]]; then
        die "No free loopback device available"
    fi
    
    losetup "$loop_device" "$image_file"
    log_info "Attached $image_file to $loop_device"
    
    # Verify
    if ! losetup "$loop_device" > /dev/null 2>&1; then
        die "Failed to setup loopback device"
    fi
    
    echo "$loop_device"
}

teardown_loopback() {
    local loop_device="$1"
    
    if (( DRY_RUN )); then
        log_debug "DRY RUN: Would detach $loop_device"
        return
    fi
    
    if [[ -b "$loop_device" ]]; then
        losetup -d "$loop_device" 2>/dev/null || {
            log_warn "Failed to detach $loop_device"
        }
    fi
}

###############################################################################
# BAD SECTORS HANDLING
###############################################################################

read_bad_sectors_from_file() {
    local filename="$1"
    local arrayname="$2"
    
    log_info "Reading bad sectors from: $filename"
    
    while IFS= read -r line; do
        # Skip comments and empty lines
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$line" ]] && continue
        
        # Trim whitespace
        line="${line#[[:space:]]*}"
        line="${line%[[:space:]]*}"
        
        if [[ "$line" =~ ^[0-9]+$ ]]; then
            eval "$arrayname+=('$line')"
        else
            log_warn "Invalid sector ID in file: $line"
        fi
    done < "$filename"
    
    eval "log_info \"Loaded \${#$arrayname[@]} bad sector IDs\""
}

generate_random_bad_sectors() {
    local count=$1
    local total_sectors=$2
    local arrayname=$3
    
    log_info "Generating $count random bad sectors"
    
    # Fast path: for large counts, we accept some duplicates and deduplicate later
    # This is much faster than uniqueness checking in a loop
    if (( count > 1000 )); then
        for (( i = 0; i < count; i++ )); do
            local sector=$((RANDOM % total_sectors))
            eval "$arrayname+=('$sector')"
        done
        return
    fi
    
    # Slow path: for small counts, ensure uniqueness during generation
    local generated=0
    local attempts=0
    local max_attempts=$((count * 10))
    
    while (( generated < count && attempts < max_attempts )); do
        local sector=$((RANDOM % total_sectors))
        
        # Check if already in list using eval
        local found=0
        eval "for existing in \"\${$arrayname[@]}\"; do
            if [[ \"\$existing\" == \"$sector\" ]]; then
                found=1
                break
            fi
        done"
        
        if (( found == 0 )); then
            eval "$arrayname+=('$sector')"
            generated=$((generated + 1))
        fi
        
        attempts=$((attempts + 1))
    done
    
    if (( generated < count )); then
        log_warn "Could only generate $generated/$count bad sectors (uniqueness constraint)"
    fi
}

generate_percent_bad_sectors() {
    local percent=$1
    local total_sectors=$2
    local arrayname=$3
    
    log_info "Generating $percent% bad sectors (from $total_sectors total sectors)"
    
    local count=$(echo "$total_sectors * $percent / 100" | bc)
    count=${count%.*}  # Round down
    
    if (( count == 0 )); then
        log_warn "Percentage too small, would result in 0 sectors"
        return
    fi
    
    log_info "Creating $count bad sectors"
    generate_random_bad_sectors "$count" "$total_sectors" "$arrayname"
}

get_bad_sectors() {
    local arrayname=$1
    local total_sectors=$2
    
    if [[ -n "$BAD_SECTORS_FILE" ]]; then
        read_bad_sectors_from_file "$BAD_SECTORS_FILE" "$arrayname"
    elif [[ -n "$BAD_SECTORS_COUNT" ]]; then
        generate_random_bad_sectors "$BAD_SECTORS_COUNT" "$total_sectors" "$arrayname"
    elif [[ -n "$BAD_SECTORS_PERCENT" ]]; then
        generate_percent_bad_sectors "$BAD_SECTORS_PERCENT" "$total_sectors" "$arrayname"
    fi
    
    # Sort the array - for large arrays this is necessary to ensure correct sector ordering
    # Use printf and sort in a subshell, then reassign
    eval "$arrayname=($(eval "printf '%s\n' \"\${$arrayname[@]}\"" | sort -n))"
}

###############################################################################
# DM-LINEAR SETUP
###############################################################################

generate_linear_table() {
    local loop_device="$1"
    shift
    local -n bad_sectors_ref=$1
    
    log_info "Generating dm-linear table with bad sector error zones"
    
    # In dry-run mode, estimate the size; otherwise query the device
    local total_size
    if (( DRY_RUN )); then
        # Use main device size to estimate sectors
        total_size=$(($(size_to_bytes "$MAIN_SIZE") / SECTOR_SIZE))
    else
        total_size=$(blockdev --getsz "$loop_device")
    fi
    
    local current_pos=0
    
    # Create table entries for linear and error targets
    # Coalesce consecutive bad sectors into ranges to avoid dmsetup limits
    local table_entries=()
    local error_start=""
    local error_end=""
    
    for bad_sector in "${bad_sectors_ref[@]}"; do
        # Check if this bad sector is consecutive to the previous error range
        if [[ -n "$error_start" ]] && (( bad_sector == error_end + 1 )); then
            # Extend current error range
            error_end=$bad_sector
        else
            # Flush previous error range if any
            if [[ -n "$error_start" ]]; then
                local error_size=$((error_end - error_start + 1))
                table_entries+=("$error_start $error_size error")
                current_pos=$((error_end + 1))
            fi
            
            # Add linear chunk before this bad sector if needed
            if (( bad_sector > current_pos )); then
                local chunk_size=$((bad_sector - current_pos))
                table_entries+=("$current_pos $chunk_size linear $loop_device $current_pos")
                current_pos=$bad_sector
            fi
            
            # Start new error range
            error_start=$bad_sector
            error_end=$bad_sector
        fi
    done
    
    # Flush final error range if any
    if [[ -n "$error_start" ]]; then
        local error_size=$((error_end - error_start + 1))
        table_entries+=("$error_start $error_size error")
        current_pos=$((error_end + 1))
    fi
    
    # Add linear chunk for remainder
    if (( current_pos < total_size )); then
        local chunk_size=$((total_size - current_pos))
        table_entries+=("$current_pos $chunk_size linear $loop_device $current_pos")
    fi
    
    printf '%s\n' "${table_entries[@]}"
}

create_dm_linear() {
    local loop_device="$1"
    shift
    local -n bad_sectors_array=$1
    
    log_info "Creating dm-linear device: /dev/mapper/$DM_LINEAR_NAME"
    
    # Deduplicate and sort bad sectors (may contain duplicates from fast random generation)
    local sorted_unique=($(printf '%s\n' "${bad_sectors_array[@]}" | sort -n -u))
    bad_sectors_array=("${sorted_unique[@]}")
    
    # Generate the table
    local table=$(generate_linear_table "$loop_device" bad_sectors_array)
    
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        local linear_table_file="${DM_LINEAR_NAME}.table"
        local linear_table_path="$(dirname "$OUTPUT_SCRIPT")/$linear_table_file"
        printf '%s\n' "$table" > "$linear_table_path"

        echo "# dm-linear table stored in $linear_table_file" >> "$OUTPUT_SCRIPT"
        echo "dmsetup create \"$DM_LINEAR_NAME\" $linear_table_file" >> "$OUTPUT_SCRIPT"
        echo "" >> "$OUTPUT_SCRIPT"
    fi
    
    if (( DRY_RUN )); then
        log_debug "DRY RUN: Would create dm-linear with $(echo "$table" | wc -l) table entries"
        return
    fi
    
    # Create device
    dmsetup create "$DM_LINEAR_NAME" <<< "$table" || {
        die "Failed to create dm-linear device"
    }
    
    log_info "Created dm-linear with $(echo "$table" | wc -l) table entries"
}

###############################################################################
# DM-REMAP CONFIGURATION
###############################################################################

generate_remap_table() {
    local linear_device="/dev/mapper/$DM_LINEAR_NAME"
    local spare_device="$1"
    local fallback_sectors="$2"

    log_info "Generating dm-remap table configuration"

    local total_size=""
    if [[ -b "$linear_device" ]]; then
        total_size=$(blockdev --getsz "$linear_device")
    else
        total_size="$fallback_sectors"
    fi

    # Generate remap target line using dm-remap-v4 target
    # Format: start size dm-remap-v4 main_device spare_device
    # (Note: dm-remap-v4 handles internal offset mapping, not part of table)
    echo "0 $total_size dm-remap-v4 $linear_device $spare_device"
}

create_dm_remap() {
    local spare_device="$1"
    local total_sectors="$2"
    
    log_info "Creating dm-remap device: /dev/mapper/$DM_REMAP_NAME"
    
    # Generate table regardless of DRY_RUN so we can capture it in output script
    local table=$(generate_remap_table "$spare_device" "$total_sectors")

    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        local remap_table_file="${DM_REMAP_NAME}.table"
        local remap_table_path="$(dirname "$OUTPUT_SCRIPT")/$remap_table_file"
        printf '%s\n' "$table" > "$remap_table_path"

        echo "# dm-remap table stored in $remap_table_file" >> "$OUTPUT_SCRIPT"
        echo "dmsetup create \"$DM_REMAP_NAME\" $remap_table_file" >> "$OUTPUT_SCRIPT"
        echo "" >> "$OUTPUT_SCRIPT"
    fi

    if (( DRY_RUN )); then
        log_debug "DRY RUN: Would create dm-remap device"
        return
    fi

    # Check if dm-remap module is loaded
    if ! grep -q "^dm_remap " /proc/modules 2>/dev/null; then
        log_info "Loading dm-remap module..."
        modprobe dm-remap || {
            log_warn "Failed to load dm-remap module (might not be installed)"
        }
    fi

    dmsetup create "$DM_REMAP_NAME" <<< "$table" || {
        die "Failed to create dm-remap device"
    }

    log_info "Created dm-remap device"
}

###############################################################################
# CLEANUP
###############################################################################

cleanup_devices() {
    log_info "Cleaning up test devices..."
    
    if (( DRY_RUN )); then
        log_debug "DRY RUN: Would remove devices"
        return
    fi
    
    # Remove dm-remap device
    if dmsetup info "$DM_REMAP_NAME" > /dev/null 2>&1; then
        log_info "Removing dm-remap device..."
        dmsetup remove "$DM_REMAP_NAME" 2>/dev/null || {
            log_warn "Failed to remove $DM_REMAP_NAME"
        }
    fi
    
    # Remove dm-linear device
    if dmsetup info "$DM_LINEAR_NAME" > /dev/null 2>&1; then
        log_info "Removing dm-linear device..."
        dmsetup remove "$DM_LINEAR_NAME" 2>/dev/null || {
            log_warn "Failed to remove $DM_LINEAR_NAME"
        }
    fi
    
    # Detach loopback devices
    for loop in /dev/loop*; do
        if losetup "$loop" 2>/dev/null | grep -q "$(basename "$MAIN_IMG")\|$(basename "$SPARE_IMG")"; then
            log_info "Detaching loopback device: $loop"
            losetup -d "$loop" 2>/dev/null || {
                log_warn "Failed to detach $loop"
            }
        fi
    done
}

cleanup_files() {
    log_info "Removing image files..."
    
    if (( DRY_RUN )); then
        log_debug "DRY RUN: Would remove files"
        return
    fi
    
    for file in "$MAIN_IMG" "$SPARE_IMG"; do
        if [[ -f "$file" ]]; then
            log_info "Removing: $file"
            rm -f "$file"
        fi
    done
}

###############################################################################
# MAIN EXECUTION
###############################################################################

main() {
    # Show help if no arguments provided
    if (( $# == 0 )); then
        print_usage
        exit 0
    fi
    
    parse_arguments "$@"
    
    # Initialize output script file
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        cat > "$OUTPUT_SCRIPT" << 'EOF'
#!/bin/bash
# Auto-generated dm-remap test setup script
# Generated by: setup-dm-remap-test.sh
# 
# This script contains all the commands needed to set up the dm-remap test environment.
# You can modify it as needed before executing.
#
# Usage: sudo bash setup.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

EOF
        log_info "Writing commands to: $OUTPUT_SCRIPT"
    fi
    
    # Root is not required for help, dry-run, or cleanup
    if ! (( DRY_RUN )) && ! (( CLEANUP_ONLY )); then
        require_root
    fi
    
    if (( CLEANUP_ONLY )); then
        require_root
        cleanup_devices
        cleanup_files
        log_info "Cleanup complete"
        return 0
    fi
    
    validate_arguments
    
    log_info "=========================================="
    log_info "dm-remap Test Setup"
    log_info "=========================================="
    log_info "Main image: $MAIN_IMG ($(bytes_to_human $(size_to_bytes "$MAIN_SIZE")))"
    log_info "Spare image: $SPARE_IMG ($(bytes_to_human $(size_to_bytes "$SPARE_SIZE")))"
    log_info "Sparse files: $(( USE_SPARSE ? 1 : 0 ))"
    log_info "Sector size: $SECTOR_SIZE bytes"
    
    if [[ -n "$BAD_SECTORS_FILE" ]]; then
        log_info "Bad sectors: from file ($BAD_SECTORS_FILE)"
    elif [[ -n "$BAD_SECTORS_COUNT" ]]; then
        log_info "Bad sectors: $BAD_SECTORS_COUNT random"
    else
        log_info "Bad sectors: $BAD_SECTORS_PERCENT% distributed"
    fi
    
    if (( DRY_RUN )); then
        log_info "DRY RUN MODE - No changes will be made"
    fi
    log_info "=========================================="
    
    # Create image files
    create_image_file "$MAIN_IMG" "$MAIN_SIZE"
    create_image_file "$SPARE_IMG" "$SPARE_SIZE"
    
    # Setup loopback devices
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        echo "# Setup loopback devices" >> "$OUTPUT_SCRIPT"
        echo "MAIN_LOOP=\$(losetup -f)" >> "$OUTPUT_SCRIPT"
        echo "losetup \$MAIN_LOOP \"$MAIN_IMG\"" >> "$OUTPUT_SCRIPT"
        echo "SPARE_LOOP=\$(losetup -f)" >> "$OUTPUT_SCRIPT"
        echo "losetup \$SPARE_LOOP \"$SPARE_IMG\"" >> "$OUTPUT_SCRIPT"
        echo "" >> "$OUTPUT_SCRIPT"
    fi
    
    MAIN_LOOP=$(setup_loopback "$MAIN_IMG")
    SPARE_LOOP=$(setup_loopback "$SPARE_IMG")
    
    if [[ -n "$OUTPUT_SCRIPT" ]] && (( ! DRY_RUN )); then
        # Update the script with actual device names
        sed -i "s|MAIN_LOOP=\$(losetup -f)|MAIN_LOOP=$MAIN_LOOP|" "$OUTPUT_SCRIPT"
        sed -i "s|SPARE_LOOP=\$(losetup -f)|SPARE_LOOP=$SPARE_LOOP|" "$OUTPUT_SCRIPT"
    fi
    
    # Calculate total sectors
    local main_bytes=$(size_to_bytes "$MAIN_SIZE")
    local total_sectors=$((main_bytes / SECTOR_SIZE))
    
    log_info "Main device: $MAIN_LOOP ($total_sectors sectors)"
    log_info "Spare device: $SPARE_LOOP"
    
    # Get bad sectors
    local bad_sectors=()
    get_bad_sectors bad_sectors "$total_sectors"
    
    log_info "Total bad sectors: ${#bad_sectors[@]}"
    if (( VERBOSE && ${#bad_sectors[@]} <= 20 )); then
        log_debug "Bad sector IDs: ${bad_sectors[*]}"
    fi
    
    # Add note to output script
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        echo "# dmsetup tables will be written to ${DM_LINEAR_NAME}.table and ${DM_REMAP_NAME}.table" >> "$OUTPUT_SCRIPT"
        echo "# Review or modify those files before running the commands below if needed." >> "$OUTPUT_SCRIPT"
        echo "" >> "$OUTPUT_SCRIPT"
    fi
    
    # Create dm-linear device
    create_dm_linear "$MAIN_LOOP" bad_sectors
    
    # Create dm-remap device
    create_dm_remap "$SPARE_LOOP" "$total_sectors"
    
    # Print summary
    log_info "=========================================="
    log_info "Test Setup Complete!"
    log_info "=========================================="
    
    if [[ -n "$OUTPUT_SCRIPT" ]]; then
        local table_dir
        table_dir=$(dirname "$OUTPUT_SCRIPT")
        if (( DRY_RUN )); then
            log_info "Output script prepared: $OUTPUT_SCRIPT"
            log_info "To use it, run: sudo bash $OUTPUT_SCRIPT"
        else
            log_info "Output script written: $OUTPUT_SCRIPT"
            log_info "Review and execute later with: sudo bash $OUTPUT_SCRIPT"
        fi
        log_info "Table files: $table_dir/${DM_LINEAR_NAME}.table and $table_dir/${DM_REMAP_NAME}.table"
    fi
    
    if ! (( DRY_RUN )); then
        cat << EOF

Created Devices:
  Main loop:    $MAIN_LOOP
  Spare loop:   $SPARE_LOOP
  dm-linear:    /dev/mapper/$DM_LINEAR_NAME
  dm-remap:     /dev/mapper/$DM_REMAP_NAME

Bad Sectors:   ${#bad_sectors[@]}
Sector Size:   $SECTOR_SIZE bytes

TESTING COMMANDS:
  # Write test data (will trigger remapping on bad sectors)
  dd if=/dev/urandom of=/dev/mapper/$DM_REMAP_NAME bs=$SECTOR_SIZE count=1000
  
  # Check device status
  dmsetup status $DM_REMAP_NAME
  dmsetup table $DM_REMAP_NAME
  
  # View device mapping
  dmsetup ls
  losetup -a
  
  # Monitor activity
  dmsetup monitor

CLEANUP:
  # Remove all test artifacts
  $SCRIPT_DIR/$(basename "$0") --cleanup

EOF
    fi
    
    log_info "=========================================="
}

###############################################################################
# ENTRY POINT
###############################################################################

main "$@"
