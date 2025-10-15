# dm-remap v4.0 - Statistics Monitoring Guide

**Date**: October 15, 2025  
**Version**: 4.0.1+  
**Purpose**: Simple statistics export for monitoring tools

---

## Overview

dm-remap v4.0.1+ exports statistics via **sysfs** at `/sys/kernel/dm_remap/`, making it trivial to integrate with existing monitoring tools like Prometheus, Nagios, Grafana, Zabbix, etc.

### Design Philosophy

✅ **Simple**: Just read files from `/sys/kernel/dm_remap/`  
✅ **Standard**: Prometheus-style metrics format  
✅ **Efficient**: No complex parsing required  
✅ **Honest**: Exposes what we track, doesn't pretend to do AI/ML  

---

## Available Statistics

### Basic I/O Counters

- **`total_reads`** - Total read operations since module load
- **`total_writes`** - Total write operations since module load
- **`total_remaps`** - Total bad sectors remapped
- **`total_errors`** - Total I/O errors detected

### Remap Activity

- **`active_mappings`** - Currently active sector remaps
- **`last_remap_time`** - Unix timestamp of last remap operation
- **`last_error_time`** - Unix timestamp of last I/O error
- **`remapped_sectors`** - Total number of sectors remapped
- **`spare_sectors_used`** - Spare device capacity used

### Performance Metrics

- **`avg_latency_us`** - Average I/O latency in microseconds
- **`remap_rate_per_hour`** - Recent remap activity (remaps/hour)
- **`error_rate_per_hour`** - Recent error activity (errors/hour)

### Health Indicator

- **`health_score`** - Simple health score (0-100, where 100 = healthy)
  - Calculation: `100 - (active_mappings / max_mappings * 100)`
  - Drops as more sectors are remapped
  - **Not AI/ML** - just a simple percentage

---

## Quick Start Examples

### Read Individual Stats

```bash
# Check health score
cat /sys/kernel/dm_remap/health_score

# Check total remaps
cat /sys/kernel/dm_remap/total_remaps

# Check if any errors occurred recently
cat /sys/kernel/dm_remap/last_error_time
```

### Get All Stats (Prometheus Format)

```bash
# Perfect for Prometheus node_exporter textfile collector
cat /sys/kernel/dm_remap/all_stats

# Output:
# dm-remap v4.0 statistics
dm_remap_total_reads 1234567
dm_remap_total_writes 2345678
dm_remap_total_remaps 42
dm_remap_total_errors 5
dm_remap_active_mappings 42
dm_remap_last_remap_time 1729012345
dm_remap_last_error_time 1729012300
dm_remap_avg_latency_us 150
dm_remap_remapped_sectors 42
dm_remap_spare_sectors_used 215040
dm_remap_remap_rate_per_hour 2
dm_remap_error_rate_per_hour 1
dm_remap_health_score 95
```

---

## Integration Examples

### Prometheus (node_exporter textfile collector)

```bash
#!/bin/bash
# /usr/local/bin/dm-remap-exporter.sh
# Run via cron every minute: * * * * * /usr/local/bin/dm-remap-exporter.sh

OUTPUT_FILE="/var/lib/node_exporter/textfile_collector/dm_remap.prom"
cat /sys/kernel/dm_remap/all_stats > "$OUTPUT_FILE.tmp"
mv "$OUTPUT_FILE.tmp" "$OUTPUT_FILE"
```

Add to `/etc/prometheus/prometheus.yml`:
```yaml
scrape_configs:
  - job_name: 'node'
    static_configs:
      - targets: ['localhost:9100']
```

### Nagios / Icinga

```bash
#!/bin/bash
# /usr/lib/nagios/plugins/check_dm_remap.sh

HEALTH=$(cat /sys/kernel/dm_remap/health_score)
REMAPS=$(cat /sys/kernel/dm_remap/total_remaps)
ERRORS=$(cat /sys/kernel/dm_remap/total_errors)

if [ "$HEALTH" -lt 70 ]; then
    echo "CRITICAL: dm-remap health=$HEALTH, remaps=$REMAPS, errors=$ERRORS"
    exit 2
elif [ "$HEALTH" -lt 90 ]; then
    echo "WARNING: dm-remap health=$HEALTH, remaps=$REMAPS, errors=$ERRORS"
    exit 1
else
    echo "OK: dm-remap health=$HEALTH, remaps=$REMAPS, errors=$ERRORS"
    exit 0
fi
```

### Zabbix

```xml
<!-- /etc/zabbix/zabbix_agentd.d/dm-remap.conf -->
UserParameter=dm_remap.health_score,cat /sys/kernel/dm_remap/health_score
UserParameter=dm_remap.total_remaps,cat /sys/kernel/dm_remap/total_remaps
UserParameter=dm_remap.total_errors,cat /sys/kernel/dm_remap/total_errors
UserParameter=dm_remap.remap_rate,cat /sys/kernel/dm_remap/remap_rate_per_hour
```

### Simple Bash Monitoring

```bash
#!/bin/bash
# Simple monitoring script

STATS_DIR="/sys/kernel/dm_remap"

echo "=== dm-remap Statistics ==="
echo "Health Score: $(cat $STATS_DIR/health_score)%"
echo "Total Reads: $(cat $STATS_DIR/total_reads)"
echo "Total Writes: $(cat $STATS_DIR/total_writes)"
echo "Total Remaps: $(cat $STATS_DIR/total_remaps)"
echo "Total Errors: $(cat $STATS_DIR/total_errors)"
echo "Active Mappings: $(cat $STATS_DIR/active_mappings)"

# Alert if health drops below 80
HEALTH=$(cat $STATS_DIR/health_score)
if [ "$HEALTH" -lt 80 ]; then
    echo "⚠️  WARNING: Health score below 80%"
    logger -t dm-remap "WARNING: Health score at $HEALTH%"
fi

# Alert if remap rate is high
REMAP_RATE=$(cat $STATS_DIR/remap_rate_per_hour)
if [ "$REMAP_RATE" -gt 10 ]; then
    echo "⚠️  WARNING: High remap rate ($REMAP_RATE/hour)"
    logger -t dm-remap "WARNING: High remap activity: $REMAP_RATE remaps/hour"
fi
```

### Python Monitoring

```python
#!/usr/bin/env python3
# dm-remap-monitor.py

import time
from pathlib import Path

STATS_DIR = Path("/sys/kernel/dm_remap")

def read_stat(name):
    """Read a single statistic."""
    return int((STATS_DIR / name).read_text().strip())

def get_all_stats():
    """Get all statistics as a dictionary."""
    stats = {}
    for stat_file in STATS_DIR.glob("*"):
        if stat_file.name != "all_stats":
            stats[stat_file.name] = int(stat_file.read_text().strip())
    return stats

def monitor_health():
    """Simple health monitoring loop."""
    while True:
        health = read_stat("health_score")
        remaps = read_stat("total_remaps")
        errors = read_stat("total_errors")
        remap_rate = read_stat("remap_rate_per_hour")
        
        print(f"Health: {health}% | Remaps: {remaps} | Errors: {errors} | Rate: {remap_rate}/hr")
        
        if health < 80:
            print(f"⚠️  WARNING: Health score low ({health}%)")
        
        if remap_rate > 10:
            print(f"⚠️  WARNING: High remap rate ({remap_rate}/hour)")
        
        time.sleep(60)  # Check every minute

if __name__ == "__main__":
    monitor_health()
```

---

## What This Is NOT

❌ **Not "AI" or "ML"** - Just simple counters and percentages  
❌ **Not "predictive"** - Doesn't predict failures (use SMART tools for that)  
❌ **Not a replacement for smartmontools** - Use `smartctl` for real disk health  

---

## What This IS

✅ **Simple statistics export** - What dm-remap sees and tracks  
✅ **Easy integration** - Works with all standard monitoring tools  
✅ **Honest metrics** - Real data, no hype  
✅ **Useful for alerts** - Know when remapping activity is high  

---

## Useful Monitoring Alerts

### High Remap Activity
```bash
# Alert if more than 10 remaps per hour
if [ $(cat /sys/kernel/dm_remap/remap_rate_per_hour) -gt 10 ]; then
    # Send alert
fi
```

### Decreasing Health
```bash
# Alert if health drops below 70%
if [ $(cat /sys/kernel/dm_remap/health_score) -lt 70 ]; then
    # Send alert
fi
```

### Recent Errors
```bash
# Alert if errors occurred in last hour
LAST_ERROR=$(cat /sys/kernel/dm_remap/last_error_time)
NOW=$(date +%s)
if [ $((NOW - LAST_ERROR)) -lt 3600 ]; then
    # Error within last hour
fi
```

---

## Grafana Dashboard Example

```json
{
  "dashboard": {
    "title": "dm-remap Monitoring",
    "panels": [
      {
        "title": "Health Score",
        "type": "graph",
        "targets": [
          {
            "expr": "dm_remap_health_score"
          }
        ]
      },
      {
        "title": "Remap Rate",
        "type": "graph",
        "targets": [
          {
            "expr": "rate(dm_remap_total_remaps[5m])"
          }
        ]
      },
      {
        "title": "I/O Operations",
        "type": "graph",
        "targets": [
          {
            "expr": "rate(dm_remap_total_reads[5m])",
            "legendFormat": "Reads"
          },
          {
            "expr": "rate(dm_remap_total_writes[5m])",
            "legendFormat": "Writes"
          }
        ]
      }
    ]
  }
}
```

---

## FAQ

**Q: Why not use existing SMART monitoring?**  
A: You should! dm-remap stats complement SMART data. SMART shows disk health, dm-remap shows remapping activity.

**Q: Can I reset the counters?**  
A: No, they're read-only. Unload and reload the module to reset.

**Q: Why is the health score so simple?**  
A: Because simple works. Complex "AI health scores" based on insufficient data are misleading.

**Q: Can I query statistics per dm-remap device?**  
A: Currently no - stats are global. If you need per-device stats, use `dmsetup status dm-remap-X`.

---

## See Also

- **SMART monitoring**: `smartctl -a /dev/sda`
- **dmsetup status**: `dmsetup status dm-remap-0`
- **Prometheus**: https://prometheus.io/
- **Node Exporter**: https://github.com/prometheus/node_exporter

---

**Bottom Line**: dm-remap now exports simple, useful statistics that work with ALL standard monitoring tools. No fancy claims, just facts.
