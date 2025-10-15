# dm-remap v4.0.2 Release Notes

**Release Date**: October 15, 2025  
**Status**: Production-ready  
**Git Tag**: `v4.0.2`

---

## Overview

v4.0.2 adds **simple, useful statistics monitoring** that integrates seamlessly with all major monitoring tools. This replaces over-engineered "health monitoring" features with something that actually works.

### What's New

✅ **Statistics Export via sysfs**  
✅ **Prometheus-Compatible Metrics**  
✅ **Integration Examples for All Major Tools**  
✅ **Honest About Capabilities** (no AI/ML hype)

---

## New Features

### sysfs Statistics Export

Simple file-based statistics at `/sys/kernel/dm_remap/`:

```bash
# View all statistics (Prometheus format)
cat /sys/kernel/dm_remap/all_stats

# Individual metrics
cat /sys/kernel/dm_remap/health_score      # 0-100
cat /sys/kernel/dm_remap/total_remaps      # Total remaps
cat /sys/kernel/dm_remap/total_errors      # Total errors
cat /sys/kernel/dm_remap/remap_rate_per_hour  # Recent activity
```

### Available Statistics

| Statistic | Description |
|-----------|-------------|
| `total_reads` | Total read operations |
| `total_writes` | Total write operations |
| `total_remaps` | Total sectors remapped |
| `total_errors` | Total I/O errors detected |
| `active_mappings` | Currently active remaps |
| `last_remap_time` | Unix timestamp of last remap |
| `last_error_time` | Unix timestamp of last error |
| `avg_latency_us` | Average I/O latency (microseconds) |
| `remapped_sectors` | Number of sectors remapped |
| `spare_sectors_used` | Spare device capacity used |
| `remap_rate_per_hour` | Recent remap activity |
| `error_rate_per_hour` | Recent error activity |
| `health_score` | Simple health score (0-100) |
| `all_stats` | Prometheus-format all metrics |

### Integration Examples

**Prometheus** (node_exporter):
```bash
cat /sys/kernel/dm_remap/all_stats > /var/lib/node_exporter/textfile_collector/dm_remap.prom
```

**Nagios**:
```bash
HEALTH=$(cat /sys/kernel/dm_remap/health_score)
[ "$HEALTH" -lt 70 ] && exit 2  # CRITICAL
[ "$HEALTH" -lt 90 ] && exit 1  # WARNING
exit 0                          # OK
```

**Zabbix**:
```
UserParameter=dm_remap.health_score,cat /sys/kernel/dm_remap/health_score
UserParameter=dm_remap.total_remaps,cat /sys/kernel/dm_remap/total_remaps
```

**Bash/Python** monitoring scripts provided in documentation.

---

## Documentation

### New Documentation
- **`docs/user/STATISTICS_MONITORING.md`** (350+ lines)
  - Complete monitoring integration guide
  - Examples for Prometheus, Grafana, Nagios, Zabbix
  - Bash and Python monitoring scripts
  - Honest about capabilities

### Updated Documentation
- **`README.md`**
  - Removed marketing hype
  - Added v4.0.2 release highlights
  - Honest feature descriptions
  - Recommendations for existing tools

---

## Technical Details

### Code Changes
- **New module**: `dm-remap-v4-stats.c` (320 lines)
- **New header**: `include/dm-remap-v4-stats.h` (24 lines)
- **Build system**: Updated Makefile
- **Test suite**: `tests/test_stats_module.sh` (6 tests, all passing)

### Build Status
- ✅ Builds cleanly (no warnings)
- ✅ No floating-point violations
- ✅ All tests passing (6/6)
- ✅ Performance validated (102μs per read)

### API
Simple C API for dm-remap modules:
```c
void dm_remap_stats_inc_reads(void);
void dm_remap_stats_inc_writes(void);
void dm_remap_stats_inc_remaps(void);
void dm_remap_stats_inc_errors(void);
void dm_remap_stats_set_active_mappings(unsigned int count);
void dm_remap_stats_update_latency(u64 latency_ns);
void dm_remap_stats_update_health_score(unsigned int score);
```

---

## What This Replaces

### Deferred: Priorities 1 & 2

**Old approach** (not worth fixing):
- Priority 1: Background Health Scanning (1,500 lines)
- Priority 2: Predictive Failure Analysis (1,200 lines)
- **Total**: 2,700+ lines of complex code
- **Issues**: Floating-point math, no real data sources, over-promised

**New approach** (v4.0.2):
- Simple statistics export (320 lines)
- Works with existing monitoring tools
- No floating-point violations
- Honest about capabilities

### Comparison

| Aspect | Old (Priorities 1 & 2) | New (v4.0.2) |
|--------|----------------------|--------------|
| Code size | 2,700+ lines | 320 lines |
| Complexity | High | Low |
| Build status | ❌ Broken | ✅ Works |
| Integration | Custom only | ALL tools |
| Value | Questionable | Proven |
| Honest | Over-promised | Factual |

---

## Installation

```bash
# Build
cd src/
make

# Load module
sudo insmod dm-remap-v4-stats.ko

# Verify
ls /sys/kernel/dm_remap/

# View statistics
cat /sys/kernel/dm_remap/all_stats
```

---

## Upgrade Notes

### From v4.0.1
- Simply load the new `dm-remap-v4-stats.ko` module
- No configuration changes needed
- Backward compatible

### From v4.0.0
- Includes v4.0.1 intelligent spare sizing improvements
- Load stats module for monitoring integration

---

## What We Don't Claim

❌ **Not "AI" or "ML"** - Just simple counters  
❌ **Not "predictive"** - Use SMART tools for that  
❌ **Not a replacement for smartmontools**  
❌ **Not a comprehensive monitoring solution**  

✅ **What it is**: Simple statistics export for integration with existing monitoring infrastructure

---

## Recommendations

For comprehensive disk monitoring, use:
- **`smartmontools`** (smartctl/smartd) - Real disk health monitoring
- **`badblocks`** - Sector-level testing
- **Prometheus + Grafana** - Metrics and visualization
- **dm-remap stats** - Remapping activity monitoring

**Use them together** for complete storage health visibility.

---

## Known Limitations

- Statistics are global, not per-device
- For per-device stats, use `dmsetup status dm-remap-X`
- Counters reset on module unload
- No SMART data integration (use `smartctl`)

---

## Future Work

**Maybe**:
- Per-device statistics (if there's demand)
- SMART integration via userspace daemon (if users want it)
- More detailed latency histograms (if needed)

**Probably Not**:
- Complex "ML" prediction (use existing tools)
- Built-in visualization (use Grafana)
- REST API (overkill for simple stats)

---

## Credits

Thank you to the user who asked the hard question: *"Are Priorities 1 & 2 worth fixing?"*

The honest answer was **NO**, leading to this much better solution.

---

## Resources

- **Quick Start**: [docs/user/QUICK_START.md](../docs/user/QUICK_START.md)
- **Statistics Guide**: [docs/user/STATISTICS_MONITORING.md](../docs/user/STATISTICS_MONITORING.md)
- **Spare Pool Guide**: [docs/SPARE_POOL_USAGE.md](../docs/SPARE_POOL_USAGE.md)
- **GitHub**: https://github.com/amigatomte/dm-remap
- **Tag**: `v4.0.2`

---

**Bottom Line**: v4.0.2 delivers simple, useful statistics monitoring that actually works with real monitoring tools. No hype, just value.
