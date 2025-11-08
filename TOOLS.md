# dm-remap Tools Documentation

Complete guide to all tools and utilities included with dm-remap.

## Tools Directory Overview

The `tools/` directory contains essential utilities for testing, monitoring, and managing dm-remap deployments:

```
tools/
├── dm-remap-loopback-setup/        # Test environment setup and bad sector injection
│   ├── setup-dm-remap-test.sh      # Main setup script
│   ├── test-zfs-mirror-degradation.sh
│   └── [documentation and guides]
├── dmremap-status/                 # Kernel module status formatter
│   ├── dmremap-status              # Compiled tool
│   ├── dmremap-status.c            # Source code
│   └── [documentation]
└── dmremap-status.mk               # Build configuration
```

---

## 1. dm-remap-loopback-setup Tool

### Purpose

A production-ready bash utility for creating and managing dm-remap test environments with simulated bad sectors and on-the-fly degradation testing.

### Main Script

**File**: `tools/dm-remap-loopback-setup/setup-dm-remap-test.sh`

### Quick Start

#### Create Clean Device (for progressive testing)

```bash
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --no-bad-sectors -v
```

**Creates**:
- 100MB clean main device
- 20MB spare pool
- dm-linear device
- dm-remap device ready for testing

#### Create Device with Initial Bad Sectors

```bash
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh -c 50 -v
```

**Creates**:
- 100MB main device with 50 random bad sectors
- 20MB spare pool for remapping
- dm-linear and dm-remap devices configured

#### Add Bad Sectors On-the-Fly (Live Testing)

```bash
# Add 50 random bad sectors to running device
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# Add from file
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -f sectors.txt -v

# Add 10% more bad sectors
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -p 10 -v
```

#### Clean Up Test Environment

```bash
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --cleanup
```

### Features

✅ **Flexible Device Creation**
- Clean device: `--no-bad-sectors`
- Specific count: `-c N` (N random sectors)
- From file: `-f FILE` (specific sector list)
- Percentage: `-p P` (P% distributed sectors)

✅ **On-the-Fly Operations**
- Add bad sectors while device is active
- Real-time degradation testing
- Zero downtime updates
- Full error recovery

✅ **Safe Operation**
- Atomic updates (suspend/load/resume)
- Zero data loss guarantee
- Transparent to active filesystems
- Comprehensive error handling

✅ **Verbose Output**
- `-v` flag for detailed logging
- Shows all device creation steps
- Clear status messages

### Use Cases

1. **Progressive Degradation Testing**
   - Start with clean device: `--no-bad-sectors`
   - Incrementally add bad sectors: `--add-bad-sectors`
   - Monitor system response to degradation

2. **Performance Benchmarking**
   - Create device with known bad sector count
   - Run filesystem operations
   - Measure remapping performance

3. **ZFS Integration Testing**
   - Create test pool on dm-remap device
   - Inject bad sectors
   - Verify ZFS scrub and healing

4. **Continuous Testing**
   - Automated test harness
   - Add bad sectors programmatically
   - Monitor and log results

### Documentation

Related guides in `tools/dm-remap-loopback-setup/`:
- `README.md` - Main documentation
- `QUICK_START_ZFS_TESTING.md` - ZFS testing guide
- `ZFS_MIRROR_TESTING.md` - Mirror pool testing
- `RUNNING_FIRST_TEST.md` - First test walkthrough

---

## 2. dmremap-status Tool

### Purpose

A userspace utility for parsing and displaying dm-remap kernel module status in multiple formats (human-readable, JSON, CSV).

### Location

**Binary**: `tools/dmremap-status/dmremap-status`
**Source**: `tools/dmremap-status/dmremap-status.c`

### Installation

```bash
# Build the tool
cd tools/dmremap-status
make

# Install (requires sudo)
sudo make install

# Verify installation
dmremap-status --version
```

### Usage

#### Human-Readable Output (Color Formatted)

```bash
dmremap-status --device dm-test-main
```

**Output Example**:
```
dm-remap Status Report
═══════════════════════════════════════════════════════════

Device Information
───────────────────────────────────────────────────────────
Device:                    dm-test-main
Health Status:             ✓ HEALTHY (95/100)
Performance Rating:        ⚡ GOOD (82/100)

Remapping Statistics
───────────────────────────────────────────────────────────
Total Remaps:              185
Active Remaps:             5
Remapped Sectors:          185
Spare Pool Usage:          0.04%

Performance Metrics
───────────────────────────────────────────────────────────
Average Remap Time:        8.2 μs
Max Remap Time:            127.5 μs
Cache Hit Rate:            87.3%
```

#### JSON Output (Machine Parseable)

```bash
dmremap-status --device dm-test-main --json
```

**Output**:
```json
{
  "device": "dm-test-main",
  "status": {
    "health": 95,
    "performance": 82,
    "total_remaps": 185,
    "active_remaps": 5,
    "avg_latency_us": 8.2
  }
}
```

#### CSV Output (Logging/Graphing)

```bash
dmremap-status --device dm-test-main --csv > status.csv
```

#### Watch Mode (Real-Time Monitoring)

```bash
# Update every 2 seconds
dmremap-status --device dm-test-main --watch 2

# Update every 0.5 seconds (500ms)
dmremap-status --device dm-test-main --watch 0.5
```

#### Compact Summary

```bash
dmremap-status --device dm-test-main --summary
```

**Output**:
```
dm-test-main: HEALTHY(95) | Remaps: 185 | Spare: 0.04% | Lat: 8.2μs
```

### Output Formats

| Format | Use Case | Command |
|--------|----------|---------|
| Human-Readable | Terminal viewing, operators | (default) |
| JSON | Scripts, automation, APIs | `--json` |
| CSV | Logging, graphing, analysis | `--csv` |
| Summary | Compact status line | `--summary` |
| Raw | Kernel output passthrough | `--raw` |

### Features

✅ **Multiple Output Formats**
- Human-readable with colors
- JSON for scripting
- CSV for data logging
- Raw kernel output

✅ **Smart Analysis**
- Health ratings (0-100 scale)
- Performance analysis
- Cache efficiency
- Automatic conversions (μs, MB, %)

✅ **Real-Time Monitoring**
- Watch mode with configurable refresh
- Hotspot detection
- Health degradation alerts

✅ **Offline Analysis**
- Read from saved snapshots
- No kernel module required
- Perfect for CI/CD

### Use Cases

1. **Operational Monitoring**
   ```bash
   dmremap-status --device dm-test-main --watch 1
   ```

2. **Automated Health Checks**
   ```bash
   dmremap-status --device dm-test-main --json | jq '.status.health > 80'
   ```

3. **Performance Logging**
   ```bash
   dmremap-status --device dm-test-main --csv >> performance.log
   ```

4. **Production Dashboards**
   - Parse JSON output for display
   - Track health and performance metrics
   - Alert on degradation

### Documentation

Related files:
- `tools/dmremap-status/README.md` - Detailed documentation
- `tools/dmremap-status/INTEGRATION.md` - System integration guide
- `tools/dmremap-status/PROJECT_SUMMARY.md` - Project overview

---

## 3. ZFS Integration Testing

### Combined Usage with ZFS

#### Create Test Environment

```bash
# 1. Create clean dm-remap device
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --no-bad-sectors -v

# 2. Get device status
dmremap-status --device dm-remap-main

# 3. Create ZFS pool on device
sudo zpool create zfs-test /dev/mapper/dm-remap-main

# 4. Create ZFS dataset with compression
sudo zfs create -o compression=lz4 -o atime=off zfs-test/data

# 5. Write test files
dd if=/dev/urandom of=/zfs-test/data/testfile bs=1M count=100

# 6. Inject bad sectors (simulating drive degradation)
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# 7. Monitor remapping performance
dmremap-status --device dm-remap-main --watch 1

# 8. Run ZFS scrub
sudo zpool scrub zfs-test

# 9. Check ZFS health
sudo zpool status zfs-test
```

### Complete Test Script

The test suite includes:
- `tests/dm_remap_zfs_integration_test.sh` - Comprehensive integration test
- `tools/dm-remap-loopback-setup/test-zfs-mirror-degradation.sh` - Mirror pool testing

---

## Usage Workflows

### Workflow 1: Basic Testing

```bash
# 1. Create test device
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh -c 10

# 2. Monitor status
dmremap-status --device dm-remap-main --watch 2

# 3. Run workload
dd if=/dev/urandom of=/tmp/testfile bs=1M count=50

# 4. Check final status
dmremap-status --device dm-remap-main --summary

# 5. Cleanup
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --cleanup
```

### Workflow 2: Progressive Degradation

```bash
# 1. Create clean device
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --no-bad-sectors -v

# 2. Create filesystem
sudo mkfs.ext4 /dev/mapper/dm-remap-main
sudo mount /dev/mapper/dm-remap-main /mnt/test

# 3. Write baseline data
dd if=/dev/urandom of=/mnt/test/baseline bs=1M count=50

# 4. Begin monitoring
dmremap-status --device dm-remap-main --watch 1 &

# 5. Inject bad sectors incrementally
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -c 10 -v
sleep 5
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -c 20 -v
sleep 5
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# 6. Analyze results
```

### Workflow 3: Performance Benchmarking

```bash
# 1. Create baseline (no bad sectors)
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --no-bad-sectors
dmremap-status --device dm-remap-main --csv > baseline.csv

# 2. Run baseline workload
dd if=/dev/urandom of=/tmp/test bs=1M count=500 of=/dev/mapper/dm-remap-main

# 3. Add bad sectors
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --add-bad-sectors -c 100

# 4. Run degraded workload
dmremap-status --device dm-remap-main --csv > degraded.csv
dd if=/dev/urandom of=/tmp/test bs=1M count=500 of=/dev/mapper/dm-remap-main

# 5. Compare results
diff baseline.csv degraded.csv
```

---

## Getting Help

### Tool Help

```bash
# setup-dm-remap-test.sh help
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --help

# dmremap-status help
dmremap-status --help
dmremap-status --version
```

### Man Pages

```bash
# After installation
man dmremap-status
```

### Documentation

All tools include comprehensive documentation:
- Main README files
- Implementation guides
- Integration documentation
- Testing guides

---

## Tool Matrix

| Task | Tool | Command |
|------|------|---------|
| Create test device | setup-dm-remap-test.sh | `setup-dm-remap-test.sh -c 50` |
| Add bad sectors | setup-dm-remap-test.sh | `setup-dm-remap-test.sh --add-bad-sectors` |
| View device status | dmremap-status | `dmremap-status --device dm-remap-main` |
| Monitor in real-time | dmremap-status | `dmremap-status --watch 2` |
| Export data | dmremap-status | `dmremap-status --json` or `--csv` |
| Cleanup devices | setup-dm-remap-test.sh | `setup-dm-remap-test.sh --cleanup` |

---

## Integration with CI/CD

### Automated Testing

```bash
#!/bin/bash
# In CI pipeline

# Create test environment
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --no-bad-sectors

# Run tests
bash tests/dm_remap_zfs_integration_test.sh

# Collect metrics
dmremap-status --device dm-remap-main --json > metrics.json

# Analyze results
jq '.status.health > 80' metrics.json || exit 1

# Cleanup
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --cleanup
```

### Monitoring & Alerts

```bash
#!/bin/bash
# Continuous monitoring

dmremap-status --device dm-remap-main --watch 5 |
while read line; do
  health=$(echo "$line" | grep -oP 'HEALTH.*?\K\d+' || echo 100)
  if [ "$health" -lt 70 ]; then
    echo "ALERT: Device health degraded: $health"
    # Send alert
  fi
done
```

---

## Building from Source

### Prerequisites

```bash
sudo apt-get install build-essential linux-headers-$(uname -r)
```

### Build All Tools

```bash
cd tools/dmremap-status
make clean
make
sudo make install
```

### Verify Installation

```bash
which dmremap-status
dmremap-status --version
```

---

## Troubleshooting

### dmremap-status Command Not Found

```bash
# Build and install
cd tools/dmremap-status
make
sudo make install PREFIX=/usr/local

# Add to PATH if needed
export PATH=$PATH:/usr/local/bin
```

### setup-dm-remap-test.sh Permission Denied

```bash
# Make script executable
chmod +x tools/dm-remap-loopback-setup/setup-dm-remap-test.sh

# Run with sudo
sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh
```

### Device Not Found

```bash
# Ensure dm-remap module is loaded
sudo modprobe dm_remap

# Verify
lsmod | grep dm_remap
```

---

## Related Documentation

- **Main README**: `README.md` - Project overview and quick start
- **Build System**: `BUILD_SYSTEM.md` - Build configuration
- **Packaging**: `PACKAGING.md` - Deployment packaging
- **DKMS**: `docs/development/DKMS.md` - Multi-kernel support
- **Testing**: `TESTING_SUMMARY.md` - Test results
- **Commands**: `COMMANDS_LOGGING.md` - Test command logging

---

**Last Updated**: November 8, 2025
**Status**: ✅ Production Ready
**Version**: 1.0.0
