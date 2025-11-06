# On-the-Fly Bad Sector Addition

## Overview

The enhanced `setup-dm-remap-test.sh` script now supports:

1. **Clean device creation** - Create devices with 0 bad sectors for baseline testing
2. **On-the-fly injection** - Add bad sectors to existing devices without recreation
3. **Real-time degradation testing** - Simulate gradual media wear on active systems
4. **Active filesystem testing** - Add bad sectors while ZFS/ext4 is mounted and operational
5. **Flexible bad sector specification** - Use the same methods as during device creation

## Getting Started

### Create Clean Device

Start with a clean device and inject bad sectors progressively:

```bash
# Create clean device
sudo ./setup-dm-remap-test.sh --no-bad-sectors -v

# Verify device is clean and operational
dmsetup status dm-test-remap
dmsetup table dm-test-linear

# Then add bad sectors as needed
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 10 -v
```

## Usage

### Add Random Bad Sectors

```bash
# Add 50 random bad sectors to existing dm-linear device
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# Add 100 random bad sectors with verbose output
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 100 -v
```

### Add Bad Sectors from File

Create a text file with sector IDs (one per line):

```bash
# Create file with specific sectors to add
cat > additional_bad_sectors.txt << EOF
# Additional bad sectors to inject
````

### Add Bad Sectors by Percentage

```bash
# Add 10% more bad sectors (calculated from total device size)
sudo ./setup-dm-remap-test.sh --add-bad-sectors -p 10 -v

# Add 5% more bad sectors
sudo ./setup-dm-remap-test.sh --add-bad-sectors -p 5 -v
```

## How It Works

### Step 1: Extract Current Bad Sectors
The script reads the current `dm-linear` table and extracts all existing error zones:
```bash
dmsetup table dm-test-linear
```

### Step 2: Generate New Bad Sectors
Using the specified method (-f, -c, or -p), the script generates a list of new bad sector IDs to add.

### Step 3: Merge and Deduplicate
The existing and new bad sectors are merged into a single sorted, deduplicated list:
- Prevents duplicate bad sectors
- Maintains proper ordering for the device mapper table

### Step 4: Safely Update Device
The update happens in three atomic steps:

1. **Suspend**: Halt I/O to the device (using `dmsetup suspend`)
2. **Load**: Load the new table (using `dmsetup load`)
3. **Resume**: Resume I/O operations (using `dmsetup resume`)

This ensures zero data corruption even with active I/O.

### Step 5: Immediate Remapping
The `dm-remap-v4` device built on top of `dm-linear` immediately begins:
- Detecting I/O errors on newly-added bad sectors
- Creating remap entries in the spare pool
- Persisting metadata

## Example Workflow

### Progressive Degradation: Start Clean, Inject Gradually

```bash
# 1. Create clean device
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./setup-dm-remap-test.sh --no-bad-sectors -v

# 2. Verify device is clean
dmsetup table dm-test-linear

# 3. Create ZFS pool on clean devices
sudo zpool create -f test-pool mirror /dev/loop17 /dev/loop19
sudo zfs create test-pool/data

# 4. Write initial data
sudo bash -c 'echo "test data" > /test-pool/data/file1.txt'
sudo dd if=/dev/urandom of=/test-pool/data/file2.bin bs=1M count=10

# 5. Gradually add bad sectors while system is active
for stage in {1..5}; do
    echo "Stage $stage: Adding 50 bad sectors..."
    sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v
    
    # Monitor remapping
    echo "Remapping status:"
    dmsetup status dm-test-remap
    
    # Continue normal operations
    sudo bash -c "dd if=/dev/urandom of=/test-pool/data/stage_${stage}.bin bs=1M count=5"
    
    sleep 5
done

# 6. Verify data integrity
sudo zfs scrub test-pool
sudo zfs status test-pool
sudo ls -lah /test-pool/data/
```

### Initial Setup: Create with Bad Sectors

```bash
# Create dm-test-linear and dm-remap with 50 bad sectors
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./setup-dm-remap-test.sh -c 50 -v
```

### Legacy: ZFS Pool (Clean Start)

````
# Create ZFS mirror with two clean loopback devices
sudo zpool create -f test-pool mirror /dev/loop17 /dev/loop19

# Create dataset
sudo zfs create test-pool/data

# Mount and verify
mount | grep test-pool
```

### 3. Write Initial Data

```bash
# Create test files
sudo bash -c 'echo "test data" > /test-pool/data/file1.txt'
sudo bash -c 'dd if=/dev/urandom of=/test-pool/data/file2.bin bs=1M count=10'

# Verify data
sudo zfs list
sudo du -sh /test-pool/data
```

### 4. Add Bad Sectors While System is Active

```bash
# Add 50 more bad sectors on-the-fly
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# Verify device is still operational
sudo zfs status test-pool
sudo ls -la /test-pool/data
```

### 5. Monitor Remapping Activity

```bash
# Check dm-remap remapping statistics
dmsetup status dm-test-remap

# View kernel logs for remapping events
sudo tail -f /var/log/kern.log | grep dm-remap

# In another terminal: Add more bad sectors
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v
```

### 6. Verify Data Integrity

```bash
# File operations continue working
sudo bash -c 'echo "more data" >> /test-pool/data/file1.txt'
sudo cat /test-pool/data/file1.txt

# Filesystem integrity check
sudo zfs scrub test-pool
sudo zfs status test-pool

# Verify files weren't corrupted
sudo md5sum /test-pool/data/file2.bin
```

## Advanced Usage

### Staged Bad Sector Introduction

Gradually introduce bad sectors to measure impact:

```bash
#!/bin/bash
# Introduce bad sectors in stages

for stage in {1..5}; do
    echo "Stage $stage: Adding 100 bad sectors"
    sudo /home/christian/kernel_dev/dm-remap/tests/loopback-setup/setup-dm-remap-test.sh \
        --add-bad-sectors -c 100 -v
    
    echo "Waiting 30 seconds..."
    sleep 30
    
    # Check status
    echo "Remapping status:"
    dmsetup status dm-test-remap
    
    # Run filesystem operation
    sudo bash -c 'dd if=/dev/urandom of=/test-pool/data/test_'$stage'.bin bs=1M count=5'
    
    echo ""
done
```

### Clustered Bad Sectors

Create a file with clustered bad sectors:

```bash
# Generate 100 consecutive bad sectors starting at sector 10000
cat > clustered_bad_sectors.txt << EOF
EOF

for i in {10000..10099}; do
    echo $i >> clustered_bad_sectors.txt
done

# Add them on-the-fly
sudo ./setup-dm-remap-test.sh --add-bad-sectors -f clustered_bad_sectors.txt -v
```

### Mix Methods

Combine multiple bad sector sources (though requires multiple runs):

```bash
# First: Add 50 random bad sectors
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# Then: Add bad sectors from file
sudo ./setup-dm-remap-test.sh --add-bad-sectors -f my_sectors.txt -v

# Finally: Add 5% more
sudo ./setup-dm-remap-test.sh --add-bad-sectors -p 5 -v
```

## Monitoring Commands

### Real-Time Remapping Status

```bash
# Live remapping statistics
watch -n 1 'dmsetup status dm-test-remap'

# Kernel events
sudo journalctl -f | grep dm-remap

# Device mapper information
dmsetup info dm-test-linear
dmsetup info dm-test-remap
```

### Table Inspection

```bash
# View current dm-linear table
dmsetup table dm-test-linear | head -20

# Count error entries
dmsetup table dm-test-linear | grep -c "error"

# Show first 10 error zones
dmsetup table dm-test-linear | grep "error" | head -10
```

### File System Status

```bash
# ZFS pool status
sudo zpool status test-pool

# ZFS dataset info
sudo zfs list -r test-pool

# File system check
sudo zfs scrub test-pool && sudo zfs status test-pool
```

## Performance Notes

### Suspend/Resume Impact
- **Minimal**: Usually <100ms
- **I/O suspended during**: Table load (~10-50ms)
- **No data loss**: Atomic operation

### Device Mapper Limits
- **Max table entries**: ~1 million (highly unlikely to hit)
- **Our typical**: Few thousand for moderate bad sector counts
- **No practical limit** for on-the-fly additions

### Bad Sector Distribution
- **Random**: More realistic wear patterns
- **Clustered**: Tests sequential failure scenarios
- **Percentage**: Scales with device size automatically

## Troubleshooting

### Device Not Found Error

```bash
# Error: "Device dm-test-linear does not exist"
# Solution: Create the device first

sudo ./setup-dm-remap-test.sh -c 50
```

### Device or Resource Busy

```bash
# Error: "Failed to suspend dm-linear device"
# Solution: Unmount filesystem, close all files using the device

sudo umount /test-pool  # for ZFS
sudo zpool export test-pool  # for ZFS pools

# Retry adding bad sectors
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v
```

### Bad Sector Deduplication

```bash
# Some sectors might already exist - they're automatically skipped
# Example output:
# [INFO] New bad sectors to add: 50
# [INFO] Total bad sectors after merge: 75  # Only 25 new due to duplicates
```

### Viewing Actual Bad Sectors Added

```bash
# Get before state
dmsetup table dm-test-linear | wc -l > /tmp/before.txt

# Add bad sectors
sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 50 -v

# Get after state
dmsetup table dm-test-linear | wc -l > /tmp/after.txt

# Compare
diff /tmp/before.txt /tmp/after.txt
```

## Integration with Testing Workflows

### Automated Testing Script

```bash
#!/bin/bash
# test-with-degradation.sh

POOL_NAME="test-pool"
DATA_DIR="/test-pool/data"

# Phase 1: Clean filesystem test
echo "Phase 1: Testing with clean device..."
for i in {1..100}; do
    dd if=/dev/urandom of=$DATA_DIR/file_$i bs=1K count=10 2>/dev/null
done
sudo zfs scrub $POOL_NAME
echo "Clean phase complete"

# Phase 2: Introduce bad sectors progressively
for stage in {1..10}; do
    echo "Phase 2.$stage: Introducing bad sectors (stage $stage/10)..."
    
    # Add 100 bad sectors
    sudo ./setup-dm-remap-test.sh --add-bad-sectors -c 100 -v
    
    # Let system stabilize
    sleep 5
    
    # Run workload
    for i in {1..50}; do
        dd if=/dev/urandom of=$DATA_DIR/degrade_$stage\_$i bs=1K count=10 2>/dev/null
    done
    
    # Check scrub
    sudo zfs scrub $POOL_NAME
    echo "Stage $stage complete: $(dmsetup status dm-test-remap | awk '{print $3}')"
done
```

## Safety Considerations

- **No data loss**: Device mapper suspend/resume is atomic
- **dm-remap continues**: Remapping continues across table updates
- **ZFS unaware**: ZFS/filesystem doesn't know about the table change
- **I/O transparent**: Existing I/O completes, new I/O uses new table

## Comparison with Cold Setup

| Aspect | Cold Setup | On-the-Fly |
|--------|-----------|-----------|
| Device recreation | Required | No |
| Filesystem impact | Remount needed | Transparent |
| Data loss | No | No |
| Speed | Slower | Faster |
| Active testing | No | Yes |
| Simulation realism | Good | Excellent |

