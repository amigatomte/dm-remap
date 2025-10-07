#!/bin/bash

# Quick Hotpath Performance Test
# Lightweight version for rapid performance validation

set -e

# Color output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() {
    echo -e "${BLUE}[$(date '+%H:%M:%S')]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

# Quick cleanup
cleanup() {
    dmsetup remove dm-remap-quick 2>/dev/null || true
    losetup -d /dev/loop20 2>/dev/null || true
    losetup -d /dev/loop21 2>/dev/null || true
    rm -f /tmp/quick_main.img /tmp/quick_spare.img
}

trap cleanup EXIT

# Quick setup
quick_setup() {
    log "Setting up quick performance test..."
    
    # Check root
    if [[ $EUID -ne 0 ]]; then
        echo "Please run as root: sudo $0"
        exit 1
    fi
    
    # Create small test files (50MB each)
    dd if=/dev/zero of=/tmp/quick_main.img bs=1M count=50 2>/dev/null
    dd if=/dev/zero of=/tmp/quick_spare.img bs=1M count=25 2>/dev/null
    
    # Setup loop devices
    losetup /dev/loop20 /tmp/quick_main.img
    losetup /dev/loop21 /tmp/quick_spare.img
    
    # Create dm device with correct parameters: main_dev spare_dev spare_start spare_len
    local spare_sectors=$(blockdev --getsz /dev/loop21)
    echo "0 $(blockdev --getsz /dev/loop20) remap /dev/loop20 /dev/loop21 0 $spare_sectors" | \
        dmsetup create dm-remap-quick
    
    success "Quick test environment ready"
}

# Get stats helper
get_stats() {
    local dm_minor=$(dmsetup info dm-remap-quick 2>/dev/null | grep "Minor:" | awk '{print $2}')
    local stats_file="/sys/block/dm-${dm_minor}/dm-remap/hotpath/hotpath_stats"
    
    if [[ -r "$stats_file" ]]; then
        cat "$stats_file"
    else
        echo "Stats not available"
    fi
}

# Reset stats helper
reset_stats() {
    local dm_minor=$(dmsetup info dm-remap-quick 2>/dev/null | grep "Minor:" | awk '{print $2}')
    local reset_file="/sys/block/dm-${dm_minor}/dm-remap/hotpath/hotpath_reset"
    
    if [[ -w "$reset_file" ]]; then
        echo "1" > "$reset_file" 2>/dev/null
    fi
}

# Quick performance test
quick_performance_test() {
    log "Running quick performance validation..."
    
    local device="/dev/mapper/dm-remap-quick"
    
    # Test 1: Sequential read performance
    log "Test 1: Sequential read performance (4K blocks)"
    reset_stats
    
    local start_time=$(date +%s.%N)
    dd if="$device" of=/dev/null bs=4K count=1000 2>/dev/null
    local end_time=$(date +%s.%N)
    
    local duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "1")
    local throughput=$(echo "scale=1; 4000 / $duration / 1024" | bc -l 2>/dev/null || echo "N/A")
    
    log "Sequential read: ${throughput} MB/s"
    
    local stats=$(get_stats)
    local total_ios=$(echo "$stats" | grep "Total I/Os:" | awk '{print $3}' || echo "0")
    local fastpath_ios=$(echo "$stats" | grep "Fast-path I/Os:" | awk '{print $3}' || echo "0")
    local efficiency=$(echo "$stats" | grep "efficiency:" | awk '{print $3}' | tr -d '%' || echo "0")
    
    log "I/O Statistics: Total=${total_ios}, Fast-path=${fastpath_ios}, Efficiency=${efficiency}%"
    
    # Test 2: Small random I/O for batch testing
    log "Test 2: Small I/O batch processing test"
    reset_stats
    
    local start_time=$(date +%s.%N)
    for i in {1..100}; do
        dd if=/dev/zero of="$device" bs=512 count=1 seek=$((i * 8)) 2>/dev/null &
        if [[ $((i % 16)) -eq 0 ]]; then
            wait  # Process in batches of 16
        fi
    done
    wait
    local end_time=$(date +%s.%N)
    
    local batch_duration=$(echo "$end_time - $start_time" | bc -l 2>/dev/null || echo "1")
    local ops_per_sec=$(echo "scale=1; 100 / $batch_duration" | bc -l 2>/dev/null || echo "N/A")
    
    log "Batch processing: ${ops_per_sec} ops/sec"
    
    local batch_stats=$(get_stats)
    local batch_processed=$(echo "$batch_stats" | grep "Batch processed:" | awk '{print $3}' || echo "0")
    local batch_efficiency=$(echo "scale=1; $batch_processed * 100 / 100" | bc -l 2>/dev/null || echo "0")
    
    log "Batch Statistics: Processed=${batch_processed}, Efficiency=${batch_efficiency}%"
    
    # Test 3: Cache alignment test
    log "Test 3: Cache alignment effectiveness"
    reset_stats
    
    # Sequential pattern (cache-friendly)
    dd if="$device" of=/dev/null bs=64 count=500 2>/dev/null
    
    local cache_stats=$(get_stats)
    local cache_hits=$(echo "$cache_stats" | grep "Cache line hits:" | awk '{print $4}' || echo "0")
    local cache_total=$(echo "$cache_stats" | grep "Total I/Os:" | awk '{print $3}' || echo "1")
    local cache_hit_rate=$(echo "scale=1; $cache_hits * 100 / $cache_total" | bc -l 2>/dev/null || echo "0")
    
    log "Cache alignment: ${cache_hit_rate}% hit rate (${cache_hits}/${cache_total})"
}

# Validate hotpath features
validate_hotpath_features() {
    log "Validating hotpath optimization features..."
    
    local dm_minor=$(dmsetup info dm-remap-quick 2>/dev/null | grep "Minor:" | awk '{print $2}')
    local hotpath_dir="/sys/block/dm-${dm_minor}/dm-remap/hotpath"
    
    if [[ -d "$hotpath_dir" ]]; then
        success "Hotpath sysfs interface available"
        
        local interfaces=("hotpath_stats" "hotpath_efficiency" "hotpath_batch_size" "hotpath_reset")
        for interface in "${interfaces[@]}"; do
            if [[ -r "${hotpath_dir}/${interface}" ]]; then
                success "✓ ${interface} interface working"
            else
                warning "✗ ${interface} interface not available"
            fi
        done
        
        # Show current stats
        log "Current hotpath statistics:"
        get_stats | sed 's/^/  /'
        
    else
        warning "Hotpath sysfs interface not found at: $hotpath_dir"
    fi
}

# Main execution
main() {
    echo "=================================================="
    echo "Quick Hotpath Performance Validation"
    echo "=================================================="
    
    quick_setup
    validate_hotpath_features
    quick_performance_test
    
    success "Quick performance test completed!"
    log "For comprehensive benchmarking, run: benchmark_hotpath_performance.sh"
}

# Check basic dependencies
if ! command -v bc >/dev/null 2>&1; then
    warning "bc not installed. Install with: apt-get install bc"
    warning "Some calculations may show 'N/A'"
fi

main "$@"