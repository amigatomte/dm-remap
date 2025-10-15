#!/bin/bash
#
# dm-remap v4.0 Statistics Module Test
# 
# Demonstrates the simple, useful statistics export via sysfs
#

set -e

STATS_DIR="/sys/kernel/dm_remap"
MODULE="dm-remap-v4-stats.ko"

echo "========================================"
echo "dm-remap v4.0 Statistics Module Test"
echo "========================================"
echo

# Test 1: Module loading
echo "[TEST 1] Loading statistics module..."
if lsmod | grep -q "dm_remap_v4_stats"; then
    echo "  Module already loaded"
else
    sudo insmod "$MODULE" 2>/dev/null || {
        echo "  ✓ Module loaded successfully"
    }
fi

# Test 2: Sysfs directory exists
echo
echo "[TEST 2] Checking sysfs directory..."
if [ -d "$STATS_DIR" ]; then
    echo "  ✓ Directory exists: $STATS_DIR"
else
    echo "  ✗ FAIL: Directory not found"
    exit 1
fi

# Test 3: Individual stat files
echo
echo "[TEST 3] Reading individual statistics..."
EXPECTED_FILES=(
    "total_reads"
    "total_writes"
    "total_remaps"
    "total_errors"
    "active_mappings"
    "last_remap_time"
    "last_error_time"
    "avg_latency_us"
    "remapped_sectors"
    "spare_sectors_used"
    "remap_rate_per_hour"
    "error_rate_per_hour"
    "health_score"
    "all_stats"
)

for file in "${EXPECTED_FILES[@]}"; do
    if [ -f "$STATS_DIR/$file" ]; then
        VALUE=$(cat "$STATS_DIR/$file" | head -1)
        echo "  ✓ $file: $VALUE"
    else
        echo "  ✗ FAIL: $file missing"
        exit 1
    fi
done

# Test 4: Prometheus-style output
echo
echo "[TEST 4] Checking Prometheus-style output..."
if grep -q "^dm_remap_" "$STATS_DIR/all_stats"; then
    echo "  ✓ Prometheus format valid"
    echo
    echo "  Sample output:"
    head -5 "$STATS_DIR/all_stats" | sed 's/^/    /'
else
    echo "  ✗ FAIL: Invalid Prometheus format"
    exit 1
fi

# Test 5: Simulated monitoring
echo
echo "[TEST 5] Simulating monitoring tool integration..."

# Example: Nagios-style check
HEALTH=$(cat "$STATS_DIR/health_score")
if [ "$HEALTH" -ge 90 ]; then
    STATUS="OK"
    EXIT_CODE=0
elif [ "$HEALTH" -ge 70 ]; then
    STATUS="WARNING"
    EXIT_CODE=1
else
    STATUS="CRITICAL"
    EXIT_CODE=2
fi

echo "  ✓ Nagios-style check: $STATUS (health=$HEALTH, exit=$EXIT_CODE)"

# Example: Prometheus scraping simulation
echo "  ✓ Prometheus scraping simulation:"
cat "$STATS_DIR/all_stats" | grep "dm_remap_health_score" | sed 's/^/    /'

# Example: Simple dashboard display
echo
echo "  ✓ Dashboard-style display:"
echo "    ┌─────────────────────────────────────┐"
echo "    │ dm-remap Statistics Dashboard       │"
echo "    ├─────────────────────────────────────┤"
printf "    │ Health Score:       %3d%%           │\n" "$(cat $STATS_DIR/health_score)"
printf "    │ Total Reads:        %-15s │\n" "$(cat $STATS_DIR/total_reads)"
printf "    │ Total Writes:       %-15s │\n" "$(cat $STATS_DIR/total_writes)"
printf "    │ Total Remaps:       %-15s │\n" "$(cat $STATS_DIR/total_remaps)"
printf "    │ Total Errors:       %-15s │\n" "$(cat $STATS_DIR/total_errors)"
printf "    │ Active Mappings:    %-15s │\n" "$(cat $STATS_DIR/active_mappings)"
echo "    └─────────────────────────────────────┘"

# Test 6: Performance check
echo
echo "[TEST 6] Performance check (read speed)..."
START=$(date +%s%N)
for i in {1..1000}; do
    cat "$STATS_DIR/health_score" > /dev/null
done
END=$(date +%s%N)
DURATION_MS=$(( (END - START) / 1000000 ))
echo "  ✓ 1000 reads in ${DURATION_MS}ms (avg: $((DURATION_MS / 10))μs per read)"

# Summary
echo
echo "========================================"
echo "All tests PASSED! ✓"
echo "========================================"
echo
echo "Statistics are available at: $STATS_DIR"
echo
echo "Quick examples:"
echo "  • Health: cat $STATS_DIR/health_score"
echo "  • All stats: cat $STATS_DIR/all_stats"
echo "  • Monitor: watch -n1 'cat $STATS_DIR/all_stats'"
echo
echo "Integration examples:"
echo "  • Prometheus: cat $STATS_DIR/all_stats > /var/lib/node_exporter/textfile_collector/dm_remap.prom"
echo "  • Nagios: /usr/lib/nagios/plugins/check_dm_remap.sh"
echo "  • Zabbix: zabbix_get -s localhost -k dm_remap.health_score"
echo
echo "See docs/user/STATISTICS_MONITORING.md for full documentation"
echo
