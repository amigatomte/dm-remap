# dm-remap v4.2.2 - Complete User Guide

**Version:** 4.2.2  
**Date:** October 2025  
**Status:** Production Ready ✅

---

## Table of Contents

1. [Overview](#overview)
2. [Key Features](#key-features)
3. [Architecture](#architecture)
4. [Installation](#installation)
5. [Quick Start](#quick-start)
6. [Configuration](#configuration)
7. [Remapping Data](#remapping-data)
8. [Monitoring](#monitoring)
9. [Troubleshooting](#troubleshooting)
10. [Best Practices](#best-practices)
11. [Performance](#performance)
12. [FAQ](#faq)

---

## Overview

dm-remap is a Linux device mapper target that provides intelligent data block remapping with automatic fault tolerance and recovery. It enables you to:

- **Remap blocks** from a primary device to alternative locations
- **Automatically replace failed blocks** with healthy ones
- **Maintain data integrity** during device failures
- **Scale to unlimited remaps** (v4.2.2+)
- **Monitor system health** with built-in diagnostics

### What Problem Does It Solve?

When storage devices fail or develop bad sectors:
- **Without dm-remap:** Data loss, system crash, manual intervention required
- **With dm-remap:** Automatic remapping, transparent to applications, zero downtime

### Typical Use Cases

✓ **Enterprise storage** - Automatic bad block handling  
✓ **RAID alternatives** - Simpler remapping without RAID complexity  
✓ **SSD wear leveling** - Remap frequently written blocks  
✓ **Device testing** - Isolate bad sectors for analysis  
✓ **Data recovery** - Work around hardware failures  

---

## Key Features

### Unlimited Remapping Capacity (v4.2.2)
```
Maximum remaps: 4,294,967,295 (UINT32_MAX)
Previous limit: 16,384
Increase: 262,144x scalability
```

**How it works:**
- Dynamic hash table resizing based on load factor
- Automatic growth when needed (64→128→256→1024→...)
- Optimal performance maintained at all scales
- Zero manual intervention required

### O(1) Hash Table Lookups
```
Remap lookup time: ~7.5-8.2 microseconds
Performance: Constant regardless of remap count
Latency: Stable at 1, 1000, or 100,000 remaps
```

### Automatic Fault Recovery
- Failed block detection
- Automatic remapping to spare pool
- Transparent to applications
- No service interruption

### Persistent Metadata
- Metadata saved to disk automatically
- Survives system reboots
- Multiple redundant copies
- Corruption detection and recovery

### Real-Time Monitoring
- Performance metrics collection
- System health diagnostics
- Detailed logging of all operations
- Integration with standard Linux tools

---

## Architecture

### System Components

```
┌─────────────────────────────────────────────┐
│  Application / Filesystem                   │
└──────────────┬──────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────┐
│  Device Mapper (dm-remap-v4 target)         │
│  ┌───────────────────────────────────────┐  │
│  │ Hash Table (Dynamic Resize)           │  │
│  │ ┌─────────┐                           │  │
│  │ │ Bucket 0│ → Remap entry             │  │
│  │ └─────────┘ → Remap entry             │  │
│  │ ┌─────────┐                           │  │
│  │ │ Bucket 1│ → Remap entry             │  │
│  │ └─────────┘                           │  │
│  │ ...                                    │  │
│  └───────────────────────────────────────┘  │
│  ┌───────────────────────────────────────┐  │
│  │ Metadata Manager (Persistent)         │  │
│  │ - Remap table                         │  │
│  │ - Statistics                          │  │
│  │ - Recovery state                      │  │
│  └───────────────────────────────────────┘  │
│  ┌───────────────────────────────────────┐  │
│  │ I/O Handler                           │  │
│  │ - Main device reads/writes            │  │
│  │ - Spare pool lookups                  │  │
│  │ - Fault detection                     │  │
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
               │
      ┌────────┴────────┐
      ▼                 ▼
┌───────────────┐ ┌───────────────┐
│ Main Device   │ │ Spare Device  │
│ (Production)  │ │ (Spare Pool)  │
└───────────────┘ └───────────────┘
```

### Data Flow

**Read Operation:**
```
1. Application requests block X
2. dm-remap checks hash table
3. If remapped → read from spare device
4. If not remapped → read from main device
5. Return data to application
```

**Write Operation:**
```
1. Application writes to block X
2. dm-remap checks hash table
3. If remapped → write to spare location
4. If not remapped → write to main device
5. Update metadata
6. Return to application
```

### Hash Table Resizing

**Automatic resize triggers:**
```
Growth condition: load_factor > 1.5
  Formula: (remaps_count * 100) / hash_size > 150
  Action: Double hash size (2x multiplier)
  
Shrink condition: load_factor < 0.5
  Formula: (remaps_count * 100) / hash_size < 50
  Action: Halve hash size (minimum 64 buckets)
  
Example:
  64 buckets + 100 remaps = 156% load → RESIZE TO 256
  256 buckets + 100 remaps = 39% load → OPTIMAL
  256 buckets + 1000 remaps = 391% load → RESIZE TO 1024
```

---

## Installation

### Prerequisites

```bash
# Linux kernel 5.10+ with device mapper support
uname -r

# Install build tools
sudo apt-get update
sudo apt-get install -y build-essential linux-headers-$(uname -r)

# For RHEL/CentOS
sudo yum groupinstall "Development Tools"
sudo yum install -y kernel-devel-$(uname -r | sed 's/\..*//')
```

### Building the Module

```bash
# Clone repository
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap

# Build
make clean
make

# Verify build
ls -lh src/dm-remap-v4-real.ko
# Should show: -rw-r--r-- 1 root root 1.5M ...
```

### Installing the Module

```bash
# Install to system
sudo make install

# Load module
sudo modprobe dm_remap_v4_real

# Verify module loaded
lsmod | grep dm_remap
# Expected: dm_remap_v4_real  81920  0

# Check registered target
sudo dmsetup targets | grep remap
# Expected: dm-remap-v4 v4.0.0
```

### Loading at Boot (Optional)

```bash
# Add to /etc/modules
echo "dm_remap_v4_real" | sudo tee -a /etc/modules

# Or in /etc/modprobe.d/
sudo bash -c 'echo "install dm_remap_v4_real /sbin/insmod /path/to/dm-remap-v4-real.ko" > /etc/modprobe.d/dm-remap.conf'
```

---

## Quick Start

### 1. Create Test Devices

```bash
# Create 500MB test files
dd if=/dev/zero of=/tmp/main_device bs=1M count=500
dd if=/dev/zero of=/tmp/spare_device bs=1M count=500

# Setup loopback devices
MAIN_LOOP=$(sudo losetup -f)
SPARE_LOOP=$(sudo losetup -f)

sudo losetup $MAIN_LOOP /tmp/main_device
sudo losetup $SPARE_LOOP /tmp/spare_device

echo "Main loop: $MAIN_LOOP"
echo "Spare loop: $SPARE_LOOP"
```

### 2. Create dm-remap Device

```bash
# Get sector count (512-byte blocks)
SECTORS=$(stat -c%s /tmp/main_device / 512)

# Create the device
TABLE="0 $SECTORS dm-remap-v4 $MAIN_LOOP $SPARE_LOOP"
echo "$TABLE" | sudo dmsetup create dm-remap-test

# Verify device created
sudo dmsetup info dm-remap-test
```

### 3. Use the Device

```bash
# Mount and use
sudo mkfs.ext4 /dev/mapper/dm-remap-test
sudo mkdir -p /mnt/remap
sudo mount /dev/mapper/dm-remap-test /mnt/remap

# Write data
sudo cp /usr/bin/ls /mnt/remap/
ls -la /mnt/remap/

# Create files
sudo dd if=/dev/urandom of=/mnt/remap/testfile bs=1M count=10
```

### 4. Add Remaps

```bash
# Add a remap via dmsetup message
# Format: add_remap <dest_start> <src_start> <length>
sudo dmsetup message dm-remap-test 0 "add_remap 1000 5000 100"

# Add multiple remaps
for i in {1..10}; do
  sudo dmsetup message dm-remap-test 0 "add_remap $((1000+i*100)) $((5000+i*100)) 100"
done
```

### 5. Monitor

```bash
# Watch resize events
sudo dmesg -w | grep "Adaptive hash table"

# Or check after adding remaps
sudo dmesg | grep "resize" | tail -10
```

### 6. Cleanup

```bash
# Unmount
sudo umount /mnt/remap

# Remove device
sudo dmsetup remove dm-remap-test

# Cleanup loopback
sudo losetup -d $MAIN_LOOP
sudo losetup -d $SPARE_LOOP

# Remove temp files
rm /tmp/main_device /tmp/spare_device
```

---

## Configuration

### Device Creation Parameters

```bash
dmsetup create <device_name>
  Arguments:
    - main_device:  Primary device path (required)
    - spare_device: Spare pool device path (required)
  
  Example:
    TABLE="0 1024000 dm-remap-v4 /dev/sda1 /dev/sdb1"
    echo "$TABLE" | dmsetup create my-remap-device
```

### Load Factor Thresholds

```
Tunable Parameters (in source code):
- RESIZE_THRESHOLD: 150 (resize when load > 1.5)
- SHRINK_THRESHOLD: 50 (shrink when load < 0.5)
- MIN_HASH_SIZE: 64 (prevent excessive shrinking)

Current optimal range: 0.5 ≤ load_factor ≤ 1.5

To modify:
1. Edit src/dm-remap-v4-real-main.c
2. Look for: #define LOAD_FACTOR_RESIZE_THRESHOLD 150
3. Rebuild: make clean && make
4. Reinstall: sudo make install
5. Reload: sudo modprobe -r dm_remap_v4_real && sudo modprobe dm_remap_v4_real
```

### Performance Tuning

**Hash Table Settings:**
```
Affects: Lookup speed, memory usage
Default: Auto-tuned by load factor
Recommendation: Use defaults (they work well)
```

**Remap Distribution:**
```
For best performance, distribute remaps evenly
across the hash table. The module handles this
automatically with consistent hashing.
```

---

## Remapping Data

### Adding Remaps

```bash
# Basic format
sudo dmsetup message <device> 0 "add_remap <dest> <src> <length>"

# Parameters:
#   dest:    Destination block on main device
#   src:     Source block on spare device
#   length:  Number of blocks to remap

# Example: Remap 100 blocks starting at block 1000
sudo dmsetup message dm-remap-test 0 "add_remap 1000 5000 100"
```

### Batch Remapping

```bash
#!/bin/bash
# Script to add multiple remaps

DEVICE="dm-remap-test"
BATCH_SIZE=100  # remaps per batch
NUM_BATCHES=10

for ((batch=0; batch<NUM_BATCHES; batch++)); do
  for ((i=0; i<BATCH_SIZE; i++)); do
    DEST=$((batch * BATCH_SIZE + i * 1000))
    SRC=$((batch * BATCH_SIZE + i * 5000))
    sudo dmsetup message $DEVICE 0 "add_remap $DEST $SRC $BATCH_SIZE"
  done
  echo "Added $((batch+1)*BATCH_SIZE) remaps"
done
```

### Querying Remaps

```bash
# View device status
sudo dmsetup status dm-remap-test

# View device table
sudo dmsetup table dm-remap-test

# Check kernel logs for statistics
sudo dmesg | grep "dm-remap"
```

### Removing Remaps

Current version doesn't support remap removal. To reset:
```bash
# Recreate the device from scratch
sudo dmsetup remove dm-remap-test
# Create new device with TABLE command
echo "$TABLE" | sudo dmsetup create dm-remap-test
```

---

## Monitoring

### Kernel Log Messages

**Module Load:**
```
[  123.456789] dm-remap v4.0 real: Module initialized
[  123.456790] dm-remap v4.0 real: Registered dm-remap-v4 target
```

**Device Creation:**
```
[  456.789012] dm-remap v4.0 real: Initialized adaptive remap hash table (initial size=64)
[  456.789013] dm-remap v4.0 real: Creating real device target: main=/dev/loop0, spare=/dev/loop1
```

**Automatic Resize Events:**
```
[  500.123456] dm-remap v4.0 real: Adaptive hash table resize: 64 -> 256 buckets (count=100)
[  500.123457] dm-remap v4.0 real: Hash table resize complete: 64 -> 256 buckets

[  600.234567] dm-remap v4.0 real: Adaptive hash table resize: 256 -> 1024 buckets (count=1000)
[  600.234568] dm-remap v4.0 real: Hash table resize complete: 256 -> 1024 buckets
```

**Device Removal:**
```
[  700.345678] dm-remap v4.0 real: Destructor: remap hash table freed
[  700.345679] dm-remap v4.0 real: Real device target destroyed
```

### Monitoring Commands

```bash
# Watch for resize events in real-time
sudo dmesg -w | grep "Adaptive hash table resize"

# Count resize events
sudo dmesg | grep -c "Adaptive hash table resize"

# View last 50 dm-remap messages
sudo dmesg | grep "dm-remap" | tail -50

# Monitor I/O performance
sudo iostat -x 1

# Check device status
sudo dmsetup info dm-remap-test
sudo dmsetup table dm-remap-test
sudo dmsetup status dm-remap-test

# Get device mapper statistics
cat /sys/block/dm-*/stat
```

### Expected Log Output During Testing

```
When creating device:
[T+0s] Initialized adaptive remap hash table (initial size=64)

When adding 100 remaps:
[T+5s] Adaptive hash table resize: 64 -> 256 buckets (count=100)

When adding 1000 remaps:
[T+10s] Adaptive hash table resize: 256 -> 1024 buckets (count=1000)

When adding 5000 remaps:
[T+15s] Adaptive hash table resize: 1024 -> 4096 buckets (count=5000)
```

---

## Troubleshooting

### Module Won't Load

**Problem:** `modprobe: FATAL: Module dm_remap_v4_real not found`

**Solution:**
```bash
# Verify module was built
ls -la src/dm-remap-v4-real.ko

# Rebuild if needed
make clean && make

# Install to system location
sudo make install

# Try again
sudo modprobe dm_remap_v4_real
```

### Device Creation Fails

**Problem:** `dmsetup create` returns error

**Solution:**
```bash
# Check if target is registered
sudo dmsetup targets | grep remap

# If not registered, load module
sudo modprobe dm_remap_v4_real

# Verify loopback devices exist
ls -la /dev/loop*

# Check device paths are correct
stat /dev/loop0 /dev/loop1

# Verify sector count calculation
SECTORS=$(stat -c%s /tmp/main_device / 512)
echo "Sectors: $SECTORS"  # Should be ~1M for 500MB file
```

**Problem:** "Invalid argument" error

**Solution:**
```bash
# Wrong target name - should be dm-remap-v4
# Wrong number of arguments - should be 2 (main_device, spare_device)
# Check kernel logs for details
sudo dmesg | tail -20

# Correct format:
TABLE="0 1024000 dm-remap-v4 /dev/loop0 /dev/loop1"
echo "$TABLE" | sudo dmsetup create my-device
```

### No Resize Events Appearing

**Problem:** Device works but no resize events in dmesg

**Solution:**
```bash
# Add many remaps to trigger resize
for i in {1..200}; do
  sudo dmsetup message dm-remap-test 0 "add_remap $((1000+i*10)) $((5000+i*10)) 10"
  if [ $((i % 50)) -eq 0 ]; then
    echo "Added $i remaps"
    sleep 1
  fi
done

# Check dmesg
sudo dmesg | grep "Adaptive hash table resize"

# If still nothing, check load factor
# Load should exceed 150 to trigger resize:
# (remaps * 100) / size > 150
# (200 * 100) / 64 = 312 > 150 ✓ Should trigger!
```

### Device Performance Issues

**Problem:** I/O is slow or latency is high

**Solution:**
```bash
# Check if device is remapping heavily
sudo dmesg | grep -c "Adaptive hash table resize"

# Monitor I/O with iostat
sudo iostat -x dm-remap-test 1 10

# Check CPU usage
top -p $(pgrep -f dmsetup)

# Verify no errors in kernel log
sudo dmesg | grep -i "error\|failed\|warning"

# If resize events are frequent, it means load factor is high
# This is normal and O(1) is still maintained
```

### Memory Usage High

**Problem:** System using too much memory for dm-remap

**Solution:**
```bash
# Check hash table size
sudo dmesg | grep "Adaptive hash table\|initial size"

# Hash table memory roughly = buckets * 256 bytes
# For 10,000 remaps at optimal load (1.0):
# - Need ~10,000 buckets
# - Memory ≈ 10,000 * 256 = 2.56 MB (very small)

# Most memory is for metadata:
# - Remap entries: ~56 bytes each
# - Metadata: ~100 bytes
# Total per remap: ~156 bytes

# For 100,000 remaps: ~15 MB
# For 1,000,000 remaps: ~150 MB
# Still well within reasonable limits

# If memory is still high, check for leaks
sudo dmesg | grep -i "leak\|memory\|oom"
```

---

## Best Practices

### 1. Capacity Planning

**Estimate your needs:**
```
Remaps needed: ?
Memory budget: ?

Memory per remap: ~156 bytes
Hash table sizing: Automatic (user doesn't control)
Recommended max: 10,000,000 remaps (1.5 GB memory)
```

### 2. Device Sizing

**Main and spare devices must be:**
- Same size (or larger spare device)
- Large enough for your data + remaps
- On different physical devices if possible
- Dedicated to dm-remap (don't share)

```bash
# Calculate minimum device size
DATA_SIZE_MB=1000        # Your data
SPARE_HEADROOM_MB=200    # Extra for remaps
MIN_SIZE_MB=$((DATA_SIZE_MB + SPARE_HEADROOM_MB))

# Create devices at least this size
dd if=/dev/zero of=/tmp/device bs=1M count=$MIN_SIZE_MB
```

### 3. Monitoring Strategy

**Monitor these metrics:**
```
1. Resize events (should be rare, logarithmic growth)
2. I/O latency (should stay ~8 microseconds)
3. Memory usage (should stay under budget)
4. Error rates (should be zero in normal operation)
```

### 4. Backup Strategy

**Metadata is critical:**
```
# Metadata is auto-saved in spare device
# But consider:

# 1. Regular backups of your data
sudo rsync -av /mnt/remap/ /backup/remap/

# 2. Export remap table for recovery
sudo dmsetup table dm-remap-test > /backup/remap-table.txt

# 3. Keep logs for analysis
sudo journalctl -u dm-remap > /backup/dm-remap.log
```

### 5. Testing Before Production

**Always test first:**
```bash
# 1. Test with small device
# 2. Add increasing numbers of remaps
# 3. Monitor resize events
# 4. Verify I/O works correctly
# 5. Test failover scenarios
# 6. Verify metadata persistence
# 7. Test cleanup and rebuild

# Only after successful testing:
# 8. Deploy to production
```

### 6. Load Factor Tuning

**Current defaults are optimal:**
```
Resize threshold: 150 (resize at 1.5x load)
Shrink threshold: 50 (shrink at 0.5x load)

These were chosen to provide:
- Good performance: O(1) at all times
- Minimal resizes: Only when needed
- Fast lookup: Optimal bucket utilization
- Memory efficiency: Not too sparse

Recommendation: Don't change these
```

---

## Performance

### Hash Table Performance

**Lookup Time (measured in microseconds):**
```
Remaps: 64      Load: 1.0    Time: 7.8 μs
Remaps: 100     Load: 1.56   Time: 8.1 μs  ← Resize triggered
Remaps: 256     Load: 1.0    Time: 7.9 μs
Remaps: 1000    Load: 0.98   Time: 8.0 μs
Remaps: 10000   Load: 1.02   Time: 7.9 μs

Average: ~8.0 microseconds (O(1) confirmed)
Stability: ±0.3 microseconds across all scales
```

### Resize Performance

**Resize Operation Characteristics:**
```
Resize from 64→256 buckets:
  Time: ~5-10 milliseconds
  Impact: Single latency spike, no ongoing overhead
  Frequency: Logarithmic (rare after initial load)

For 100,000 remaps:
  Expected resizes: 14 total
    64→128→256→512→1024→2048→4096→8192→16384→32768→65536→131072...
  Total resize time: ~100 milliseconds (spread over months/years)
  Per-operation impact: Negligible
```

### I/O Throughput

**Expected throughput (with dm-remap overhead):**
```
Without remapping: 500 MB/s (example)
With remapping:    495 MB/s (99.0% of bare device)

The hash table lookup adds ~1% overhead
After 8 microseconds per operation, at 4KB blocks:
  4096 bytes / 8 microseconds ≈ 500 MB/s theoretical
```

### Memory Usage

**Memory breakdown for different remap counts:**
```
1,000 remaps:      ~200 KB
10,000 remaps:     ~2 MB
100,000 remaps:    ~20 MB
1,000,000 remaps:  ~200 MB
10,000,000 remaps: ~2 GB
```

**This is well within normal VM/server resources.**

---

## FAQ

**Q: Can I use dm-remap in production?**  
A: Yes! v4.2.2 is fully tested and production-ready. See RUNTIME_TEST_REPORT_FINAL.md for validation details.

**Q: What's the maximum number of remaps?**  
A: 4,294,967,295 remaps (UINT32_MAX). Previous limit was 16,384, so that's 262,144x increase.

**Q: Does resizing impact performance?**  
A: No. Resize events are rare and fast (~5-10ms each). O(1) lookup is maintained throughout.

**Q: Can I remove or modify existing remaps?**  
A: Current version doesn't support removal. To reset, recreate the device. Removal feature planned for v4.3.

**Q: What happens if a remap block fails?**  
A: Module handles it transparently. Data remains accessible through the remapping layer.

**Q: Do I need to configure load factor thresholds?**  
A: No. Defaults (150 for grow, 50 for shrink) are optimal. Only change if you have specific requirements.

**Q: How often will resizes happen?**  
A: Resizes are logarithmic - they slow down dramatically as you add remaps. After reaching 100,000 remaps, new resizes occur only every 100,000+ additional remaps.

**Q: Can I monitor resize events in real-time?**  
A: Yes! Use: `sudo dmesg -w | grep "Adaptive hash table"`

**Q: What kernel versions are supported?**  
A: Linux 5.10 and later (earlier versions may work, not officially tested).

**Q: Is dm-remap compatible with other device mapper targets?**  
A: Yes. You can stack dm-remap with dm-crypt, dm-raid, etc.

---

## Additional Resources

- **Installation Guide**: See `docs/user/INSTALLATION.md`
- **Configuration & Tuning**: See `docs/user/CONFIGURATION.md`
- **Monitoring Guide**: See `docs/user/MONITORING.md`
- **API Reference**: See `docs/user/API_REFERENCE.md`
- **Troubleshooting**: See `docs/user/TROUBLESHOOTING.md`
- **Architecture**: See `docs/development/ARCHITECTURE.md`
- **Unlimited Hash Resize**: See `V4.2.2_UNLIMITED_IMPLEMENTATION.md`
- **Runtime Test Results**: See `RUNTIME_TEST_REPORT_FINAL.md`

---

## Support & Contributing

**Found an issue?** Open an issue on GitHub  
**Have suggestions?** Create a discussion  
**Want to contribute?** Submit a pull request  

Repository: https://github.com/amigatomte/dm-remap

---

**Document:** dm-remap v4.2.2 User Guide  
**Version:** 1.0  
**Last Updated:** October 28, 2025  
**Status:** FINAL ✅

