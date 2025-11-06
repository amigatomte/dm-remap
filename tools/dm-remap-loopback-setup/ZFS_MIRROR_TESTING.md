# ZFS Mirror Degradation Testing Guide

## Overview

This guide describes how to use the `test-zfs-mirror-degradation.sh` script to create a ZFS mirror pool with one clean dm-remap device and progressively inject bad sectors to observe ZFS resilience and recovery behavior.

## What This Tests

- **Baseline Performance**: Clean device with no simulated failures
- **Progressive Degradation**: Real-time bad sector injection during active I/O
- **ZFS Resilience**: How ZFS responds to increasing failure rates
- **Data Integrity**: Whether data remains consistent under degradation
- **Recovery Behavior**: ZFS self-healing and error handling mechanisms

## Architecture

```
ZFS Mirror Pool (test-mirror)
├── Device 1: Sparse file loopback (/dev/loop*)
│   └── Baseline for comparison (no dm-remap overhead)
│
└── Device 2: dm-remap device (/dev/mapper/dm-test-remap)
    └── With on-the-fly bad sector injection
    └── Allows real-time degradation testing
```

## Prerequisites

### Required Tools

```bash
# ZFS utilities
sudo apt-get install zfsutils-linux

# Device mapper and loopback utilities (usually pre-installed)
# Verified by the test script

# dm-remap kernel module
# Should be compiled and available in your kernel
```

### Space Requirements

- **Loopback File**: `POOL_SIZE` MB (created in `/tmp`)
- **dm-remap Device**: `POOL_SIZE` MB (in-memory block device)
- **Test Data**: ~500 MB per degradation stage (on ZFS pool)
- **Total Example**: 512 MB device + 512 MB test data + staging = ~2 GB

## Usage

### Basic Usage

```bash
# Run with default settings
sudo ./test-zfs-mirror-degradation.sh

# Default Configuration:
# - Pool size: 1024 MB
# - Bad sectors per stage: 50
# - Number of degradation stages: 5
# - Pool name: test-mirror
```

### With Custom Parameters

```bash
# Smaller pool for faster testing
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 3

# More degradation stages
sudo ./test-zfs-mirror-degradation.sh -s 1024 -n 10 -b 25

# Custom pool name
sudo ./test-zfs-mirror-degradation.sh -p my-test-pool -v

# Dry run to see what will happen
sudo ./test-zfs-mirror-degradation.sh -d -v
```

### Command-Line Options

```
-p, --pool-name NAME        ZFS pool name (default: test-mirror)
-s, --pool-size SIZE        Pool device size in MB (default: 1024)
-b, --bad-sector-count N    Bad sectors per injection (default: 50)
-n, --num-stages N          Number of degradation stages (default: 5)
-d, --dry-run               Show what would be done without executing
-v, --verbose               Verbose output with debug information
-h, --help                  Show help message
```

## Workflow Phases

### Phase 1: Setup

1. **Create Loopback Device**
   - Generates sparse file: `/tmp/zfs-mirror-loopback.img`
   - Creates loopback device (e.g., `/dev/loop0`)
   - Size: `POOL_SIZE` MB

2. **Create dm-remap Device**
   - Uses setup script with `--no-bad-sectors` flag
   - Creates clean device mapper device
   - No initial simulated bad sectors
   - Ready for on-the-fly injection

3. **Create ZFS Mirror Pool**
   - Creates mirror pool from both devices
   - Configures automatic redundancy
   - Ready for testing

### Phase 2: Baseline Testing

Tests the clean pool to establish baseline performance and data integrity:

- **Write Test Data**: 100 MB file with random data
- **Calculate Checksum**: SHA256 of test file
- **Create Multiple Files**: 10 files × 10 MB each
- **Read and Verify**: Confirm checksums match

**Expected Result**: All operations succeed, no errors, data integrity verified.

### Phase 3: Progressive Degradation

For each degradation stage:

1. **Inject Bad Sectors**
   - Adds `BAD_SECTOR_COUNT` bad sectors via dm-remap
   - Uses `--add-bad-sectors` flag
   - Injection happens while pool is active (on-the-fly)

2. **Observe ZFS Response**
   - Check pool status: `zpool status`
   - Check device mapper status: `dmsetup status`
   - Look for errors, faults, or offline devices

3. **Run Filesystem Tests**
   - Try reading baseline test file
   - Verify checksums (should still match)
   - Write new test data for this stage
   - Check directory listing and file count

4. **Record Results**
   - Note any I/O errors
   - Track data integrity violations
   - Document ZFS recovery actions

## Example Output Analysis

### Clean Pool (Stage 0)

```
[INFO] Running baseline tests on clean pool...
[INFO] Writing test data (100MB)...
[INFO] Calculating checksums...
[INFO]   Baseline checksum: a3f5c8d2e1b9...
[✓] Baseline tests passed

Pool status shows:
  pool: test-mirror
  state: ONLINE
  scan: none requested
```

**Interpretation**: Pool is healthy, all data operations successful.

### With Bad Sectors (Stage 1-3)

```
[INFO] Stage 2: Injecting bad sectors (total: ~100 bad sectors)...
[INFO] Pool status:
    pool: test-mirror
    state: ONLINE (or DEGRADED if vdev faulted)
    
[INFO] Reading baseline test file...
[✓] Checksum verified (data integrity maintained)

[✓] Stage 2 tests complete
```

**Interpretation**: 
- If `state: ONLINE`: ZFS is handling errors transparently via redundancy
- If `state: DEGRADED`: One vdev is faulty, but data is still accessible
- Checksum match: Data integrity maintained despite bad sectors
- Checksum mismatch: Data corruption detected (should not happen in mirror with active vdev)

### With Severe Degradation

```
[WARN] Pool status:
    state: FAULTED
    action: Failed device has been removed...

[WARN] Could not read baseline test file (may be due to bad sectors)
[WARN] Could not write stage test data (device may have I/O errors)
```

**Interpretation**:
- Pool is no longer accessible
- dm-remap device has too many bad sectors to function
- Mirror redundancy has failed (both devices affected or one offline)

## Interpreting Results

### Key Metrics to Monitor

1. **Data Integrity**
   - Baseline checksum should match throughout
   - Mismatches indicate corruption
   - Should not happen in healthy mirror

2. **ZFS State**
   - `ONLINE`: All vdevs healthy
   - `DEGRADED`: One vdev faulty, but usable via redundancy
   - `FAULTED`: Mirror is broken

3. **I/O Performance**
   - Success/failure rate of operations
   - Number of errors reported
   - Retry behavior of ZFS

4. **Error Recovery**
   - Whether ZFS automatically handles failures
   - Whether dmsetup shows remap statistics
   - Number of sectors remapped vs. failed

## Advanced Usage

### Testing Specific Degradation Levels

```bash
# Create clean pool and run only baseline
sudo ./test-zfs-mirror-degradation.sh -n 0

# Test with exactly 500 bad sectors (10 stages of 50)
sudo ./test-zfs-mirror-degradation.sh -n 10 -b 50

# Aggressive degradation (250 bad sectors per stage)
sudo ./test-zfs-mirror-degradation.sh -b 250 -n 5 -s 2048
```

### Custom Monitoring

While the test is running, in another terminal:

```bash
# Watch ZFS pool status
watch -n 1 'zpool status test-mirror'

# Watch dm-remap statistics
watch -n 1 'dmsetup status dm-test-remap'

# Monitor I/O activity
iostat -x 1 /dev/mapper/dm-test-remap

# Check system logs for errors
tail -f /var/log/kern.log | grep -E "dm-remap|zfs|I/O error"
```

### Repeat Testing

To run multiple tests with different configurations:

```bash
# Test 1: Light degradation
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 3 -b 25

# Test 2: Medium degradation
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 6 -b 50

# Test 3: Heavy degradation
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 10 -b 100
```

## Troubleshooting

### ZFS Not Installed

```bash
# Install ZFS utilities
sudo apt-get update
sudo apt-get install zfsutils-linux

# Verify installation
zfs --version
zpool --version
```

### dm-remap Module Not Available

```bash
# Check if module is loaded
lsmod | grep dm_remap

# If not loaded, compile and install the module
cd /path/to/kernel_dev/dm-remap
sudo make install
sudo modprobe dm_remap
```

### Pool Creation Fails

```bash
# Make sure loopback devices are available
losetup -f

# Check device mapper setup
dmsetup version
dmsetup ls

# Try creating pool manually
sudo zpool create -f test-mirror /dev/loop0 /dev/mapper/dm-test-remap
```

### Script Exits Unexpectedly

```bash
# Run with verbose output
sudo ./test-zfs-mirror-degradation.sh -v

# Try dry run first
sudo ./test-zfs-mirror-degradation.sh -d -v

# Check system logs
dmesg | tail -50
```

### Permission Denied Errors

```bash
# Ensure running as root
sudo ./test-zfs-mirror-degradation.sh

# Or use sudo bash to run in shell context
sudo bash -c './test-zfs-mirror-degradation.sh'
```

## Files and Directories

### Created During Testing

- `/tmp/zfs-mirror-loopback.img`: Sparse file loopback device
- `/$POOL_NAME`: ZFS mount point (e.g., `/test-mirror`)
- `/dev/loop*`: Loopback device
- `/dev/mapper/dm-test-linear`: dm-remap's linear device
- `/dev/mapper/dm-test-remap`: dm-remap's remap device

### Automatic Cleanup

On script exit (successful or error), cleanup procedures:

1. Destroy ZFS pool
2. Clean up dm-remap devices
3. Remove loopback file

**Note**: In case of interruption (Ctrl+C), cleanup still runs via trap handler.

## Performance Expectations

### Baseline (Clean Device)

- Write 100 MB: < 1 second
- Checksum verify: < 2 seconds
- Read operations: < 1 second

### With Moderate Bad Sectors (50-150 sectors)

- Operations may have slight latency increase
- I/O errors should be transparent to application
- ZFS handles via mirror redundancy

### With Severe Bad Sectors (250+ sectors)

- Some operations may fail
- Device might be marked as offline
- Pool may degrade or fault

## Real-World Applications

### Use Cases

1. **Device Reliability Testing**
   - Verify ZFS behavior with degrading storage
   - Test RAID reconstruction under load
   - Validate error handling

2. **Application Resilience**
   - Test application behavior on unreliable storage
   - Verify data consistency checks
   - Evaluate recovery mechanisms

3. **Performance Analysis**
   - Measure impact of bad sectors on I/O
   - Compare degradation curves
   - Identify failure thresholds

4. **Driver Development**
   - Validate dm-remap error handling
   - Test on-the-fly injection mechanism
   - Verify statistics reporting

## See Also

- `setup-dm-remap-test.sh`: Device setup and configuration
- `ON_THE_FLY_BAD_SECTORS.md`: Bad sector injection details
- `README.md`: General dm-remap tool documentation

## Support and Feedback

For issues or improvements, check:

1. Script verbose output: `--verbose`
2. System logs: `dmesg`, `/var/log/kern.log`
3. ZFS status: `zpool status`, `zfs list`
4. Device mapper: `dmsetup status`, `dmsetup info`
