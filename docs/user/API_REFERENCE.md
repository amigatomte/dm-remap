# dm-remap v4.2.2 - API Reference & Command Reference

## Table of Contents

1. [Device Creation](#device-creation)
2. [Runtime Commands (dmsetup message)](#runtime-commands-dmsetup-message)
3. [Status & Information](#status--information)
4. [Examples](#examples)
5. [Return Codes](#return-codes)
6. [Kernel Module Parameters](#kernel-module-parameters)
7. [sysfs Interface](#sysfs-interface)
8. [Troubleshooting](#troubleshooting)

---

## Device Creation

### dmsetup create

**Syntax:**
```bash
echo "0 <sectors> dm-remap-v4 <main_device> <spare_device>" | \
  dmsetup create <device_name>
```

**Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| 0 | number | Yes | Sector offset (start at 0) |
| sectors | number | Yes | Total sectors from main device (use blockdev --getsz) |
| dm-remap-v4 | string | Yes | Target type (must be exact) |
| main_device | path | Yes | Primary device (e.g., /dev/sdb, /dev/loop0) |
| spare_device | path | Yes | Spare device for remaps (e.g., /dev/sdc, /dev/loop1) |
| device_name | string | Yes | Name for new dm device (e.g., my-remap) |

**Result:** Creates `/dev/mapper/<device_name>`

**Example:**
```bash
# Calculate sectors
SECTORS=$((500 * 1024 * 1024 / 512))  # 500MB in sectors

# Create device
echo "0 $SECTORS dm-remap-v4 /dev/sdb /dev/sdc" | \
  dmsetup create my-remap

# Verify
ls -l /dev/mapper/my-remap
```

**Common Errors:**

| Error | Cause | Solution |
|-------|-------|----------|
| "Unknown target type" | Module not loaded | `sudo modprobe dm_remap_v4_real` |
| "Invalid argument count" | Wrong parameters | Verify: offset, sectors, type, devices |
| "Device not found" | Main/spare device missing | Check `/dev/sdb`, `/dev/sdc` exist |
| "No space left" | Spare device too small | Ensure spare ≥ 5% of main |

---

## Runtime Commands (dmsetup message)

### General Syntax

```bash
sudo dmsetup message <device_name> <sector> "<command> [args]"
```

**Parameters:**

| Parameter | Meaning |
|-----------|---------|
| device_name | Name used in dmsetup create (e.g., my-remap) |
| sector | Sector offset (always 0 for dm-remap) |
| command | Command name (add_remap, status, etc.) |
| args | Command-specific arguments |

**Note:** Command output appears in kernel log (`dmesg`), not stdout.

### help - List Available Commands

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "help"
```

**Output:** Kernel log shows:
```
dm-remap v4.0 real: Available commands:
  help              - This message
  status            - Device configuration
  stats             - I/O statistics
  health            - Health metrics
  add_remap         - add_remap <main_sector> <spare_sector> <length>
  remove_remap      - remove_remap <main_sector>
  clear_all         - Remove all remaps (WARNING: destructive)
```

**Time:** Immediate  
**Impact:** None (read-only)

---

### status - Device Configuration

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "status"
```

**Output:** Kernel log shows:
```
dm-remap v4.0 real: Device status:
  Main device: /dev/sdb (sectors: 1048576)
  Spare device: /dev/sdc (sectors: 524288)
  Remaps active: 0
  Hash table size: 64
  Load factor: 0.0
  Resize count: 0
```

**Fields:**

| Field | Meaning |
|-------|---------|
| Main device | Primary storage device |
| Spare device | Where remapped sectors go |
| Remaps active | Current number of active remaps |
| Hash table size | Current bucket count (power of 2) |
| Load factor | (remaps / buckets), optimal 0.5-1.5 |
| Resize count | Number of table resizes performed |

**Time:** < 1 ms  
**Impact:** None (read-only)

**Example Use Cases:**
```bash
# Check current state
sudo dmsetup message my-remap 0 "status"
sudo dmesg -w | tail -10

# Verify device ready
status=$(sudo dmsetup status my-remap)
echo "$status" | grep -q "ACTIVE" && echo "Ready"
```

---

### stats - I/O Statistics

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "stats"
```

**Output:** Kernel log shows:
```
dm-remap v4.0 real: I/O Statistics:
  Read operations:     1000
  Write operations:    500
  Remap hits:         250
  Remap misses:       1250
  I/O errors:         0
  Average lookup time: 8 μs
```

**Fields:**

| Field | Unit | Meaning |
|-------|------|---------|
| Read operations | count | Total read I/O requests |
| Write operations | count | Total write I/O requests |
| Remap hits | count | I/O to remapped sectors |
| Remap misses | count | I/O to normal sectors |
| I/O errors | count | Failed I/O operations |
| Average lookup time | microseconds | O(1) performance |

**Interpretation:**
```bash
# Low error rate (< 0.1%)
error_rate = (I/O errors / (reads + writes)) * 100
if error_rate < 0.1:
    echo "Device healthy"
else
    echo "ERROR: High error rate!"

# Load factor check
load = Remaps / Hash_table_size
if 0.5 <= load <= 1.5:
    echo "Optimal load factor"
else:
    echo "Resize may be needed"
```

**Time:** < 1 ms  
**Impact:** Counters reset on each read (see clear_stats)

---

### health - Health Metrics

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "health"
```

**Output:** Kernel log shows:
```
dm-remap v4.0 real: Health Status:
  Health score: 98/100
  Error rate (last min): 0.1%
  Max consecutive errors: 2
  Last error: 45 seconds ago
  Trend: STABLE
```

**Fields:**

| Field | Range | Meaning |
|-------|-------|---------|
| Health score | 0-100 | Overall device health (100 = perfect) |
| Error rate | 0-100% | Errors in last minute |
| Max consecutive errors | count | Longest error streak |
| Last error | timestamp | When last error occurred |
| Trend | STABLE/WARNING/CRITICAL | Health trend |

**Health Score Calculation:**
```
Score starts at 100
- Each error in last hour: -5 points
- Consecutive errors: -10 additional
- No errors in 1 hour: +2 points (recovery)
```

**Monitoring Integration:**
```bash
# Nagios check
health=$(sudo dmsetup message my-remap 0 health | grep "Health score:" | awk '{print $NF}')
if [ $health -lt 70 ]; then
    echo "CRITICAL: Health score $health"
    exit 2
elif [ $health -lt 85 ]; then
    echo "WARNING: Health score $health"
    exit 1
else
    echo "OK: Health score $health"
    exit 0
fi
```

**Time:** < 1 ms  
**Impact:** None (read-only)

---

### add_remap - Add Remap Entry

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "add_remap <main_sector> <spare_sector> <length>"
```

**Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| main_sector | number | Yes | Sector on main device to remap |
| spare_sector | number | Yes | Destination sector on spare device |
| length | number | Yes | Length of remap range in sectors |

**Constraints:**
- `main_sector + length ≤ main_device_size`
- `spare_sector + length ≤ spare_device_size`
- `length > 0`
- No overlap with existing remaps on main device

**Output:** Kernel log shows:
```
dm-remap v4.0 real: Added remap: 1000 → 5000 (length: 100)
```

**Example:**
```bash
# Add remap (100 sectors starting at 1000)
sudo dmsetup message my-remap 0 "add_remap 1000 5000 100"

# Verify in logs
sudo dmesg | tail -5
```

**Behavior:**
1. Check if remap already exists (replace if yes)
2. Allocate new remap_entry (~156 bytes)
3. Insert into hash table
4. Check load factor
5. Trigger resize if needed

**Triggers Resize When:**
```
Load factor = remaps / hash_table_size
After this insertion:
  if load_factor > 1.5:
      Schedule resize to double table size
      Log: "Adaptive hash table resize: 64 → 128 buckets"
```

**Common Errors:**

| Error | Cause | Solution |
|-------|-------|----------|
| "Out of memory" | Kernel memory pressure | Try again, reduce remaps, check `free` |
| "Invalid sector range" | Sector out of bounds | Check main/spare device sizes |
| "Table resize failed" | Resize operation failed | Check available memory |

**Time:** < 100 μs (plus ~5-10ms if resize triggered)  
**Impact:** I/O may stall briefly during resize

---

### remove_remap - Remove Remap Entry

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "remove_remap <main_sector>"
```

**Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| main_sector | number | Yes | Sector on main device to remove |

**Output:** Kernel log shows:
```
dm-remap v4.0 real: Removed remap: 1000 (was mapping to 5000)
```

**Example:**
```bash
# Remove specific remap
sudo dmsetup message my-remap 0 "remove_remap 1000"

# Verify removed
sudo dmsetup message my-remap 0 "stats"
sudo dmesg | tail -3
```

**Behavior:**
1. Hash table lookup for remap
2. Remove from bucket chain
3. Free remap_entry memory
4. Update counters
5. Check if shrink needed

**Triggers Shrink When:**
```
After removal:
  load_factor = remaps / hash_table_size
  if load_factor < 0.5 AND table_size > 64:
      Schedule resize to half table size
      Log: "Adaptive hash table resize: 256 → 128 buckets"
```

**Common Errors:**

| Error | Cause | Solution |
|-------|-------|----------|
| "Remap not found" | Sector never remapped | Use correct sector number |
| "Invalid sector" | Sector out of range | Check main device size |

**Time:** < 50 μs (plus ~5-10ms if shrink triggered)  
**Impact:** Minimal (shrink less common than grow)

---

### clear_all - Remove All Remaps

**Syntax:**
```bash
sudo dmsetup message my-remap 0 "clear_all"
```

**⚠️ WARNING: Destructive operation**
- Removes ALL remaps
- Frees all memory
- Resets to initial state
- **Not reversible without re-adding**

**Output:** Kernel log shows:
```
dm-remap v4.0 real: Cleared all remaps (1000 entries removed)
```

**Example:**
```bash
# Clear all remaps
echo "Are you sure? (type 'yes')"
read confirm
if [ "$confirm" = "yes" ]; then
    sudo dmsetup message my-remap 0 "clear_all"
    sudo dmesg | tail -2
fi
```

**Behavior:**
1. Iterate through hash table
2. Free all remap_entry structures
3. Clear hash table
4. Reset counters
5. Resize to minimum (64 buckets)

**Time:** O(n) where n = current remaps  
- 1,000 remaps: ~5 ms
- 10,000 remaps: ~50 ms
- 100,000 remaps: ~500 ms

**Impact:** I/O stalls during operation  
**Recovery:** None (data loss)

---

## Status & Information

### dmsetup status

**Syntax:**
```bash
sudo dmsetup status my-remap
```

**Output:**
```
0 1048576 dm-remap-v4 1 0
```

**Fields:**
- `0`: Start sector
- `1048576`: Size in sectors
- `dm-remap-v4`: Target type
- `1`: Number of remaps
- `0`: Errors

**Example:**
```bash
# Parse status
status=$(sudo dmsetup status my-remap)
sectors=$(echo $status | awk '{print $2}')
remaps=$(echo $status | awk '{print $4}')
errors=$(echo $status | awk '{print $5}')

echo "Device size: $sectors sectors"
echo "Active remaps: $remaps"
echo "Errors: $errors"
```

---

### dmsetup info

**Syntax:**
```bash
sudo dmsetup info my-remap
```

**Output:**
```
Name:              my-remap
State:             ACTIVE
Read Ahead:        256
Tables present:    LIVE
Open count:        1
Event number:      0
Major, minor:      253, 0
Number of targets: 1
```

---

### Kernel Logging

**View kernel messages:**
```bash
# Real-time log (with filtering)
sudo dmesg -w | grep "dm-remap"

# Last N lines
sudo dmesg | grep "dm-remap" | tail -20

# Search for resize events
sudo dmesg | grep "Adaptive hash table resize"

# Count errors
sudo dmesg | grep -c "dm-remap.*error"
```

**Log Levels:**
```
INFO:     Informational (resize events, statistics)
WARNING:  Non-critical issues (error rate high)
ERROR:    Critical issues (I/O failure, memory issues)
```

---

## Examples

### Example 1: Basic Setup

```bash
#!/bin/bash

# Create 500MB test devices
dd if=/dev/zero of=/tmp/main bs=1M count=500
dd if=/dev/zero of=/tmp/spare bs=1M count=50

# Setup loopback
MAIN=$(sudo losetup -f)
SPARE=$(sudo losetup -f)
sudo losetup $MAIN /tmp/main
sudo losetup $SPARE /tmp/spare

# Verify
sudo losetup -a

# Create dm-remap device
SECTORS=$((500 * 1024 * 1024 / 512))
TABLE="0 $SECTORS dm-remap-v4 $MAIN $SPARE"
echo "$TABLE" | sudo dmsetup create test-remap

# Verify device created
ls -l /dev/mapper/test-remap
```

### Example 2: Add Remaps & Monitor

```bash
#!/bin/bash

DEVICE="test-remap"

# Check initial status
sudo dmsetup message $DEVICE 0 "status"
sudo dmesg | tail -3

# Add some remaps
for i in {1..10}; do
    main=$((i * 100))
    spare=$((i * 50))
    sudo dmsetup message $DEVICE 0 "add_remap $main $spare 50"
done

# View statistics
sudo dmsetup message $DEVICE 0 "stats"
sudo dmesg | tail -5

# Check health
sudo dmsetup message $DEVICE 0 "health"
```

### Example 3: Observe Resize Events

```bash
#!/bin/bash

DEVICE="test-remap"

# Start kernel log monitor in background
sudo dmesg -w &
MONITOR_PID=$!

# Give it a moment to start
sleep 1

# Add many remaps to trigger resize
echo "Adding 200 remaps..."
for i in {0..199}; do
    main=$((i * 100))
    spare=$((i * 50))
    sudo dmsetup message $DEVICE 0 "add_remap $main $spare 50"
    [ $((i % 50)) -eq 0 ] && echo "  ... $i remaps added"
done

echo "Waiting for resize events..."
sleep 2

# Stop monitor
kill $MONITOR_PID 2>/dev/null

# Show resize summary
echo ""
echo "Resize events observed:"
sudo dmesg | grep "Adaptive hash table resize"
```

### Example 4: Monitoring Script

```bash
#!/bin/bash

DEVICE="my-remap"
INTERVAL=5

while true; do
    clear
    echo "dm-remap Monitoring - $(date)"
    echo "================================"
    
    # Status
    echo ""
    echo "Status:"
    sudo dmsetup message $DEVICE 0 "status" 2>&1 | \
        grep -A 10 "Device status:" | tail -6
    
    # Statistics
    echo ""
    echo "Statistics:"
    sudo dmsetup message $DEVICE 0 "stats" 2>&1 | \
        grep -A 6 "Statistics:" | tail -5
    
    # Health
    echo ""
    echo "Health:"
    sudo dmsetup message $DEVICE 0 "health" 2>&1 | \
        grep -A 6 "Health Status:" | tail -5
    
    echo ""
    echo "Refreshing in $INTERVAL seconds (Ctrl+C to exit)..."
    sleep $INTERVAL
done
```

### Example 5: Unmount & Cleanup

```bash
#!/bin/bash

DEVICE="test-remap"
MAIN="/dev/loop0"
SPARE="/dev/loop1"

echo "Cleaning up dm-remap device..."

# Unmount if mounted
if [ -d /mnt/remap ] && mountpoint -q /mnt/remap; then
    sudo umount /mnt/remap
    sudo rmdir /mnt/remap
fi

# Remove dm device
sudo dmsetup remove $DEVICE

# Detach loopback
sudo losetup -d $MAIN $SPARE

# Clean temp files
rm -f /tmp/main /tmp/spare

echo "Cleanup complete"
```

---

## Return Codes

### dmsetup Return Codes

```
0   - Success
1   - Device not found
2   - Invalid parameters
3   - Device busy (in use)
4   - Memory error
5   - Device I/O error
6   - Invalid argument count
```

### Message Command Return Codes

**In kernel log:**
```
0    - Success
-1   - Unknown command
-2   - Invalid parameters
-3   - Out of memory
-4   - Device I/O error
-5   - Operation not supported
-12  - Out of memory (ENOMEM)
-22  - Invalid argument (EINVAL)
```

---

## Kernel Module Parameters

### Loading with Parameters

**Syntax:**
```bash
sudo modprobe dm_remap_v4_real [parameter=value ...]
```

**Available Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| debug | bool | 0 | Enable debug logging |
| initial_hash_size | int | 64 | Initial hash table size (power of 2) |
| gc_interval | int | 60 | Garbage collection interval (seconds) |

**Example:**
```bash
# Load with debug enabled
sudo modprobe dm_remap_v4_real debug=1

# Check parameters
cat /sys/module/dm_remap_v4_real/parameters/debug
```

---

## sysfs Interface

### Export Location

**Base:** `/sys/kernel/dm_remap/`

**Available Metrics:**

| Path | Type | Value | Description |
|------|------|-------|-------------|
| `total_remaps` | read | number | Total remaps ever created |
| `active_remaps` | read | number | Currently active remaps |
| `hash_resizes` | read | number | Total resize operations |
| `io_read_total` | read | number | Total read I/O operations |
| `io_write_total` | read | number | Total write I/O operations |
| `io_errors` | read | number | Total I/O errors |
| `health_score` | read | 0-100 | Current health score |
| `all_stats` | read | text | All metrics (Prometheus format) |

**Reading Metrics:**
```bash
# Single metric
cat /sys/kernel/dm_remap/total_remaps

# All metrics (Prometheus compatible)
cat /sys/kernel/dm_remap/all_stats

# Monitor in real-time
watch -n 1 'cat /sys/kernel/dm_remap/all_stats'
```

**Prometheus Integration:**
```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'dm-remap'
    static_configs:
      - targets: ['localhost:9100']
    metrics_path: '/sys/kernel/dm_remap/all_stats'
```

---

## Troubleshooting

### Device Creation Issues

**"Unknown target type: dm-remap-v4"**
```bash
# Solution: Load module
sudo modprobe dm_remap_v4_real

# Verify
sudo dmsetup targets | grep remap
```

**"Invalid argument count"**
```bash
# Problem: Wrong parameters
# Solution: Check parameter order
echo "0 <SECTORS> dm-remap-v4 <MAIN> <SPARE>" | dmsetup create <NAME>
#     ↑ ↑        ↑                ↑      ↑
#     Must be exact
```

**"Device not found"**
```bash
# Problem: Main or spare device doesn't exist
# Solution: Create first
ls /dev/sdb /dev/sdc  # Verify devices exist

# Or use loopback
sudo losetup -f       # Find free loop device
```

### I/O Performance Issues

**"Slow I/O after many remaps"**
```bash
# Check load factor
sudo dmsetup message my-remap 0 "status" | grep "Load factor"

# If > 1.5, resize is needed
# If < 0.5, shrink is beneficial

# Check resize history
sudo dmesg | grep "Adaptive hash table"
```

**"High error rate"**
```bash
# Check health
sudo dmsetup message my-remap 0 "health"

# Check error details
sudo dmesg | grep -i error | tail -20

# Check device health (if using real devices)
sudo smartctl -a /dev/sdb
```

### Memory Issues

**"Out of memory" errors**
```bash
# Check available memory
free -h

# Check slab cache
cat /proc/slabinfo | grep remap_entry

# Clear remaps to free memory
sudo dmsetup message my-remap 0 "clear_all"
```

### Module Unloading

**"Device or resource busy"**
```bash
# Problem: Device still in use
# Solution: Remove all devices first
sudo dmsetup remove my-remap
sudo dmsetup remove my-remap-2
# ... remove all

# Then unload
sudo modprobe -r dm_remap_v4_real
```

---

**API Reference:** v4.2.2  
**Last Updated:** October 28, 2025  
**Status:** COMPLETE ✅
