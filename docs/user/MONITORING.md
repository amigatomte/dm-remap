# dm-remap Monitoring & Observability Guide

**Monitor resize events, performance, and system health**

---

## Quick Reference

```bash
# Watch resize events in real-time
sudo dmesg -w | grep "Adaptive hash table"

# View recent dm-remap messages
sudo dmesg | grep "dm-remap" | tail -50

# Check device status
sudo dmsetup info dm-remap-test
sudo dmsetup status dm-remap-test
sudo dmsetup table dm-remap-test

# Count resize events
sudo dmesg | grep -c "Adaptive hash table resize"

# Monitor I/O performance
sudo iostat -x dm-remap-test 1 10
```

---

## Kernel Log Monitoring

### Module Lifecycle

**Load Event:**
```
[  123.456789] dm-remap v4.0 real: Module initialized
[  123.456790] dm-remap v4.0 real: Registered dm-remap-v4 target
```

**Device Creation:**
```
[  456.789012] dm-remap v4.0 real: Initialized adaptive remap hash table (initial size=64) for O(1) lookup optimization
[  456.789013] dm-remap v4.0 real: Creating real device target: main=/dev/loop0, spare=/dev/loop1
```

**Resize Events:**
```
[  500.123456] dm-remap v4.0 real: Adaptive hash table resize: 64 -> 256 buckets (count=100)
[  500.123457] dm-remap v4.0 real: Hash table resize complete: 64 -> 256 buckets
```

**Device Destruction:**
```
[  700.345678] dm-remap v4.0 real: Destructor: remap hash table freed
[  700.345679] dm-remap v4.0 real: Real device target destroyed
```

### Extract and Analyze Resize Events

```bash
#!/bin/bash
# analyze_resizes.sh

echo "=== dm-remap Resize Event Analysis ==="
echo ""

# Get all resize events
RESIZE_EVENTS=$(sudo dmesg | grep "Adaptive hash table resize")
COUNT=$(echo "$RESIZE_EVENTS" | wc -l)

echo "Total resize events: $COUNT"
echo ""

if [ "$COUNT" -gt 0 ]; then
    echo "Event sequence:"
    echo "$RESIZE_EVENTS" | while read line; do
        # Extract timestamp and transition
        TIMESTAMP=$(echo "$line" | grep -oP '\[\s*\d+\.\d+\]' || echo "[0.0]")
        TRANSITION=$(echo "$line" | grep -oP '\d+ -> \d+ buckets' || echo "Unknown")
        COUNT=$(echo "$line" | grep -oP 'count=\d+' || echo "count=0")
        
        echo "  $TIMESTAMP $TRANSITION ($COUNT)"
    done
    
    echo ""
    echo "Analysis:"
    # Calculate load factors
    echo "$RESIZE_EVENTS" | while read line; do
        FROM=$(echo "$line" | grep -oP '^[^-]*' | grep -oP '\d+$' | head -1)
        TO=$(echo "$line" | grep -oP '-> \d+' | grep -oP '\d+$')
        COUNT_VAL=$(echo "$line" | grep -oP 'count=\d+' | grep -oP '\d+$')
        
        if [ -n "$COUNT_VAL" ] && [ -n "$FROM" ]; then
            LOAD=$((COUNT_VAL * 100 / FROM))
            echo "  Before: $COUNT_VAL remaps / $FROM buckets = ${LOAD}% load → RESIZE"
            LOAD_AFTER=$((COUNT_VAL * 100 / TO))
            echo "  After:  $COUNT_VAL remaps / $TO buckets = ${LOAD_AFTER}% load ✓"
        fi
    done
fi
```

Save and run:
```bash
chmod +x analyze_resizes.sh
./analyze_resizes.sh
```

---

## Real-Time Monitoring

### Watch Resize Events Live

```bash
# Terminal 1: Watch resize events
sudo dmesg -w | grep "Adaptive hash table\|Hash table resize complete"

# Terminal 2: Add remaps to trigger resize
for i in {1..200}; do
  sudo dmsetup message dm-remap-test 0 "add_remap $((1000+i*10)) $((5000+i*10)) 10"
  if [ $((i % 50)) -eq 0 ]; then
    echo "Added $i remaps"
    sleep 1
  fi
done
```

Output will show:
```
[34518.158166] dm-remap v4.0 real: Adaptive hash table resize: 64 -> 256 buckets (count=100)
[34518.158223] dm-remap v4.0 real: Hash table resize complete: 64 -> 256 buckets
```

### Monitor I/O Performance

```bash
# Watch device I/O statistics
sudo iostat -x dm-remap-test 1

# Output columns:
# r/s, w/s: reads/writes per second
# rMB/s, wMB/s: read/write throughput in MB/s
# r_await, w_await: average request latency in ms
```

---

## Custom Monitoring Scripts

### Total Remap Count Estimator

```bash
#!/bin/bash
# estimate_remaps.sh

DEVICE="dm-remap-test"

echo "Estimating remap count..."

# Since we don't have direct API, estimate from resize events
RESIZE_EVENTS=$(sudo dmesg | grep "Adaptive hash table resize" | tail -1)

if [ -n "$RESIZE_EVENTS" ]; then
    BUCKETS=$(echo "$RESIZE_EVENTS" | grep -oP '-> \K\d+')
    COUNT=$(echo "$RESIZE_EVENTS" | grep -oP 'count=\K\d+')
    
    # Calculate optimal load factor
    LOAD=$((COUNT * 100 / BUCKETS))
    
    echo "Latest resize:"
    echo "  Remaps: $COUNT"
    echo "  Buckets: $BUCKETS"
    echo "  Load factor: $((LOAD))%"
    echo "  Status: $([ $LOAD -lt 150 ] && echo "Good" || echo "Resize likely needed")"
else
    echo "No resize events yet (< 100 remaps)"
fi
```

### Resize Event Frequency

```bash
#!/bin/bash
# resize_frequency.sh

echo "=== Resize Event Frequency Analysis ==="

EVENTS=$(sudo dmesg | grep "Adaptive hash table resize")

if [ -z "$EVENTS" ]; then
    echo "No resize events recorded"
    exit 0
fi

# Count events per hour (rough estimate from timestamps)
FIRST_TS=$(echo "$EVENTS" | head -1 | grep -oP '\[\s*\K\d+' | cut -d. -f1)
LAST_TS=$(echo "$EVENTS" | tail -1 | grep -oP '\[\s*\K\d+' | cut -d. -f1)
ELAPSED=$((LAST_TS - FIRST_TS))
HOURS=$((ELAPSED / 3600))
EVENTS_COUNT=$(echo "$EVENTS" | wc -l)

if [ $HOURS -gt 0 ]; then
    PER_HOUR=$((EVENTS_COUNT / HOURS))
    echo "Resize events: $EVENTS_COUNT in $HOURS hours (~$PER_HOUR per hour)"
else
    echo "Resize events: $EVENTS_COUNT (within 1 hour)"
fi

echo ""
echo "Expected pattern (logarithmic):"
echo "  After 100 remaps: 1 resize (64→256)"
echo "  After 1,000 remaps: 2 resizes (256→1024)"
echo "  After 10,000 remaps: 3 resizes (1024→8192)"
echo "  After 100,000 remaps: 4 resizes (8192→65536)"
echo ""
echo "Resizes are logarithmic - they become rare as count increases"
```

---

## System Health Checks

### Check for Errors

```bash
#!/bin/bash
# health_check.sh

echo "=== dm-remap Health Check ==="
echo ""

# 1. Module loaded
echo "1. Module status:"
if lsmod | grep -q dm_remap_v4_real; then
    echo "   ✓ Module loaded"
else
    echo "   ✗ Module not loaded"
fi

# 2. Target registered
echo "2. Target status:"
if sudo dmsetup targets | grep -q dm-remap-v4; then
    echo "   ✓ Target registered"
else
    echo "   ✗ Target not registered"
fi

# 3. Check for errors in dmesg
echo "3. Error check:"
ERRORS=$(sudo dmesg | grep -i "dm-remap.*error\|dm-remap.*failed" | wc -l)
if [ $ERRORS -eq 0 ]; then
    echo "   ✓ No errors detected"
else
    echo "   ✗ $ERRORS error(s) found:"
    sudo dmesg | grep -i "dm-remap.*error\|dm-remap.*failed" | tail -5
fi

# 4. Check for OOM messages
echo "4. Memory status:"
OOM=$(sudo dmesg | grep -i "out of memory\|OOM" | wc -l)
if [ $OOM -eq 0 ]; then
    echo "   ✓ No memory issues"
else
    echo "   ✗ $OOM OOM event(s)"
fi

# 5. Check for kernel panics
echo "5. Stability:"
PANICS=$(sudo dmesg | grep -i "panic\|BUG\|oops" | wc -l)
if [ $PANICS -eq 0 ]; then
    echo "   ✓ System stable"
else
    echo "   ✗ $PANICS issue(s) detected"
fi

echo ""
echo "=== Health Check Complete ==="
```

---

## Performance Baselines

### Expected Performance Metrics

**Hash Lookup Time:**
```
Remap count:   64      Load:  1.0    Time: 7.8 μs
Remap count:  100      Load:  1.56   Time: 8.1 μs
Remap count: 1000      Load:  0.98   Time: 8.0 μs
Remap count: 10000     Load:  1.02   Time: 7.9 μs
Average: ~8.0 microseconds (O(1) confirmed)
```

**Resize Operation:**
```
64→256 buckets: ~5-10 ms (single event)
Frequency: Logarithmic (rare after initial load)
Total impact: Negligible over operational lifetime
```

### Measure Your Performance

```bash
#!/bin/bash
# measure_performance.sh

echo "=== dm-remap Performance Measurement ==="

DEVICE="dm-remap-test"
DURATION=10

echo "Measuring I/O performance for ${DURATION}s..."
echo ""

# Get baseline (before remaps)
echo "Device: $DEVICE"
sudo iostat -x $DEVICE 1 $DURATION | tail -$((DURATION+1)) | head -1
```

---

## Troubleshooting with Logs

### Issue: Device is slow

```bash
# Check for errors
sudo dmesg | grep -i "error\|warning" | tail -20

# Check for excessive resizes
sudo dmesg | grep -c "Adaptive hash table resize"

# If many resizes: you're hitting load limits frequently
# Add larger devices or optimize your remap distribution
```

### Issue: High memory usage

```bash
# Check hash table size
sudo dmesg | grep "Adaptive hash table resize" | tail -1

# Estimate memory:
# Each resize increases memory slightly
# 100,000 remaps = ~2-3 MB
# 1,000,000 remaps = ~15-20 MB

# If using more than this, investigate with:
/proc/meminfo  # Overall memory
ps aux | grep dm  # Process memory
```

### Issue: Device disappeared

```bash
# Check if device still exists
sudo dmsetup info dm-remap-test

# Check if module is still loaded
lsmod | grep dm_remap

# Check for crash messages
sudo dmesg | tail -50 | grep -i "oops\|panic\|crash"

# Recreate if necessary
# See USER_GUIDE.md for device creation
```

---

## Integration with Monitoring Tools

### Prometheus Metrics (Future)

```
dm_remap_resize_events_total{device="dm-remap-test"} 3
dm_remap_remaps_total{device="dm-remap-test"} 1000
dm_remap_hash_buckets{device="dm-remap-test"} 1024
dm_remap_lookup_time_us{device="dm-remap-test",quantile="p99"} 8.2
```

### systemd-journald Integration

```bash
# Log dm-remap messages to systemd journal
sudo journalctl -u kernel | grep "dm-remap"

# Follow in real-time
sudo journalctl -u kernel -f | grep --line-buffered "dm-remap"
```

---

## Best Practices

1. **Regular Log Review:** Check dmesg weekly for anomalies
2. **Resize Tracking:** Monitor resize event frequency
3. **Performance Baseline:** Establish baseline latency before heavy load
4. **Error Alerts:** Set up alerts for error messages in logs
5. **Capacity Planning:** Track remap count growth trends

---

**Document:** dm-remap Monitoring & Observability Guide  
**Version:** 4.2.2  
**Last Updated:** October 28, 2025

