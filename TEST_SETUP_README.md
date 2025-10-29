# dm-remap Test Setup Script

A comprehensive shell script for creating test environments to exercise dm-remap's block remapping functionality with simulated bad sectors.

## Overview

The `setup-dm-remap-test.sh` script creates a complete test infrastructure:

1. **Image Files** - Sparse or regular files to act as block device storage
2. **Loopback Devices** - Virtual block devices backed by image files
3. **dm-linear Device** - Maps sectors with configurable error zones for bad blocks
4. **dm-remap Device** - Remaps I/O from bad sectors to spare pool storage

## Features

✅ **Flexible Bad Sector Configuration**
- Text file with specific sector IDs
- Fixed random count (e.g., 100 random bad sectors)
- Percentage-based distribution (e.g., 5% of device is bad)

✅ **Sparse File Support**
- Create sparse files for fast setup and low initial disk usage
- Optional regular files for performance testing

✅ **Fully Configurable**
- Custom image file names
- Device sizes (supports K, M, G, T suffixes)
- Sector size (default 4096 bytes)
- Dry-run mode to preview operations

✅ **User-Friendly**
- Comprehensive help and examples
- Verbose logging
- Detailed error messages
- Automatic cleanup

✅ **Non-Destructive**
- Works with test files, not real devices
- Complete cleanup available
- Dry-run mode for safe preview

## Installation

```bash
# Copy script to repository
cp setup-dm-remap-test.sh /path/to/dm-remap/

# Make executable
chmod +x /path/to/dm-remap/setup-dm-remap-test.sh
```

## Basic Usage

### Quick Start (100 Random Bad Sectors)

```bash
cd /path/to/dm-remap
sudo ./setup-dm-remap-test.sh -c 100
```

### Create with 5% Bad Sectors

```bash
sudo ./setup-dm-remap-test.sh -p 5
```

### Use Specific Bad Sectors from File

```bash
sudo ./setup-dm-remap-test.sh -f example_bad_sectors.txt
```

### Preview Without Making Changes

```bash
sudo ./setup-dm-remap-test.sh -c 100 --dry-run -v
```

### Custom Sizes and Options

```bash
sudo ./setup-dm-remap-test.sh \
  -M 1G \
  -S 256M \
  --sparse yes \
  -c 500 \
  -v
```

### Clean Up After Testing

```bash
sudo ./setup-dm-remap-test.sh --cleanup
```

## Command-Line Options

### Image Configuration

```
-m, --main FILE           Main image filename (default: main.img)
-s, --spare FILE          Spare image filename (default: spare.img)
-M, --main-size SIZE      Main device size (default: 100M)
                          Examples: 100M, 1G, 512K, 2T
-S, --spare-size SIZE     Spare device size (default: 20M)
```

### File Type

```
--sparse yes|no           Use sparse files (default: yes)
                          Sparse: Fast creation, low disk initially
                          Regular: May perform better, preallocates space
```

### Bad Sector Configuration (Choose One)

```
-f, --bad-sectors-file F  Text file with sector IDs (one per line)
                          # Comments supported
-c, --bad-sectors-count N Create N random bad sectors
                          Example: 100 (creates 100 random bad sectors)
-p, --bad-sectors-percent P Create bad sectors for P% of device
                          Example: 5 (5% of sectors are bad)
```

### Other Options

```
--sector-size SIZE        Sector size in bytes (default: 4096)
                          Usually matches filesystem block size
--dry-run                 Preview commands without executing
-v, --verbose             Verbose output (debug information)
--cleanup                 Remove all test artifacts and exit
-h, --help               Show help message with examples
```

## Examples

### Example 1: Basic Setup

```bash
sudo ./setup-dm-remap-test.sh -c 100

# Creates:
# - main.img (100M sparse)
# - spare.img (20M sparse)
# - /dev/loop0 pointing to main.img
# - /dev/loop1 pointing to spare.img
# - /dev/mapper/dm-test-linear (with 100 error zones)
# - /dev/mapper/dm-test-remap (remapping device)
```

### Example 2: Large Device with Few Bad Sectors

```bash
sudo ./setup-dm-remap-test.sh -M 10G -S 1G -c 50 -v

# 10GB main device with 50 random bad sectors
# 1GB spare pool
# Verbose output
```

### Example 3: Percentage-Based Bad Sectors

```bash
sudo ./setup-dm-remap-test.sh -M 500M -S 100M -p 2 --verbose

# 500MB main device with 2% bad sectors (10MB worth)
# 100MB spare pool
# Verbose output showing calculated sector count
```

### Example 4: Clustered Bad Sectors from File

```bash
sudo ./setup-dm-remap-test.sh -M 1G -S 200M -f example_bad_sectors.txt

# Uses predefined bad sector pattern:
# - Early device bad sectors (metadata area)
# - Scattered mid-device sectors
# - Clustered sectors (fragmentation testing)
# - Late device sectors
```

### Example 5: Regular (Non-Sparse) Files

```bash
sudo ./setup-dm-remap-test.sh \
  -M 500M -S 100M \
  --sparse no \
  -c 100

# Allocates 500MB + 100MB immediately
# May perform better than sparse for I/O testing
# Takes more disk space initially
```

### Example 6: Dry Run Preview

```bash
sudo ./setup-dm-remap-test.sh \
  -M 1G -S 200M \
  -c 250 \
  --dry-run -v

# Shows all operations that would be performed
# Creates no actual devices or files
# Useful for planning and verification
```

### Example 7: Non-Standard Sector Size

```bash
sudo ./setup-dm-remap-test.sh \
  --sector-size 512 \
  -M 1G -S 200M \
  -c 100

# Uses 512-byte sectors (older filesystems)
# Adjusts total sector count accordingly
```

## Bad Sectors File Format

Create a text file with one sector ID per line. Comments start with `#`.

```
# Example bad_sectors.txt
# Early sectors (metadata area)
0
5
10

# Mid-device scattered
256
1024
2048

# Clustered sectors (fragmentation)
5000
5001
5002
5003
5004

# Late device
100000
150000
```

## Theory of Operation

### Architecture

```
Application
    ↓
/dev/mapper/dm-test-remap  (dm-remap device)
    ↓
/dev/mapper/dm-test-linear  (dm-linear with error zones)
    ├─→ normal sectors → linear target → main.img
    ├─→ bad sectors   → error target  → I/O errors
    └─→ remainder     → linear target → main.img
    ↓
/dev/loop0 (main.img backing storage)
/dev/loop1 (spare.img backing storage)
```

### Workflow

1. **Write to dm-remap**
   ```bash
   dd if=/dev/urandom of=/dev/mapper/dm-test-remap bs=4K count=1000
   ```

2. **dm-remap intercepts**
   - Recognizes bad sector access attempts
   - Remaps I/O to spare pool
   - Updates metadata about remapped sectors

3. **dm-linear processes**
   - Normal sectors: passes through to main.img
   - Bad sectors: returns error (caught by dm-remap)
   - Remainder: passes through to main.img

4. **Application sees**
   - Normal I/O completing successfully
   - Bad sectors handled transparently
   - Data preserved in spare pool

### Sector Calculation

With default 4KB sector size:
- Sector 0 = bytes 0-4095 (0x0000-0x0FFF)
- Sector 1 = bytes 4096-8191 (0x1000-0x1FFF)
- Sector N = bytes N×4096 to (N×4096)+4095

To convert byte offset to sector ID:
```bash
sector_id=$((byte_offset / 4096))
```

## Testing

### Check Setup

```bash
# List all device mappings
dmsetup ls

# View dm-linear table
dmsetup table dm-test-linear

# View dm-remap table
dmsetup table dm-test-remap

# Check loopback devices
losetup -a

# Check image file sizes
ls -lh main.img spare.img
```

### Test I/O

```bash
# Write random data (triggers remapping on bad sectors)
dd if=/dev/urandom of=/dev/mapper/dm-test-remap bs=4K count=1000

# Read test
dd if=/dev/mapper/dm-test-remap of=/dev/null bs=4K count=1000

# Monitor device status
dmsetup status dm-test-remap

# Watch real-time activity
dmsetup monitor
```

### Performance Testing

```bash
# Sequential write
dd if=/dev/zero of=/dev/mapper/dm-test-remap bs=1M count=100

# Random write with fio
fio --name=random-write \
    --filename=/dev/mapper/dm-test-remap \
    --rw=randwrite \
    --bs=4K \
    --size=1G \
    --numjobs=4
```

## Cleanup

### Automatic Cleanup

```bash
sudo ./setup-dm-remap-test.sh --cleanup

# Removes:
# - dm-remap device
# - dm-linear device
# - Loopback devices
# - Image files
```

### Manual Cleanup

```bash
# Remove dm-remap device
sudo dmsetup remove dm-test-remap

# Remove dm-linear device
sudo dmsetup remove dm-test-linear

# Detach loopback devices
sudo losetup -d /dev/loop0
sudo losetup -d /dev/loop1

# Remove image files
rm -f main.img spare.img
```

## Requirements

- **Root/sudo access** - Device mapper and loopback setup requires root
- **Linux kernel** - dm-linear and dm-remap targets required
- **dm-remap module** - Must be built and available (modprobe dm-remap)
- **Standard utilities** - dd, losetup, dmsetup, bc, blockdev

## Troubleshooting

### "No free loopback device available"

```bash
# Add more loopback devices
sudo modprobe loop max_loop=16

# Or check existing
losetup -a
```

### "Device or resource busy"

```bash
# Check what's using the device
lsof +D .

# Ensure no processes have files open
sync; sleep 1
```

### "dm-remap module not found"

```bash
# Build and load dm-remap
cd /path/to/dm-remap
make
sudo insmod dm-remap.ko

# Or use DKMS package
sudo apt install dm-remap-dkms
```

### "Permission denied"

```bash
# Run with sudo
sudo ./setup-dm-remap-test.sh -c 100
```

### "Invalid size format"

```bash
# Use supported suffixes: K, M, G, T
# Correct: -M 1G
# Wrong:   -M 1GB
```

### Sector Size Mismatch

```bash
# Check current sector size
blockdev --getss /dev/loop0

# Use matching size
./setup-dm-remap-test.sh --sector-size 512 ...
```

## Performance Notes

### Sparse vs. Regular Files

**Sparse Files** (default)
- Pros: Fast creation, low disk usage initially
- Cons: May be slower on some I/O patterns
- Use: Testing, development

**Regular Files**
- Pros: Consistent performance, preallocated space
- Cons: Slower creation, uses more disk immediately
- Use: Performance testing, production scenarios

### Bad Sector Distribution

**Few Sectors** (1-10)
- Use: Targeted functionality testing
- Time: Minimal overhead
- Pattern: Edge cases

**Many Sectors** (1000+)
- Use: Stress testing
- Time: Tests worst-case behavior
- Pattern: System limits

**Random Distribution**
- Use: Realistic failure patterns
- Algorithm: Ensures uniqueness
- Pattern: Natural failure modes

**Clustered Sectors**
- Use: Fragmentation testing
- Method: Text file specification
- Pattern: Physical damage simulation

## Example Workflows

### Development Testing

```bash
# Create minimal test setup
sudo ./setup-dm-remap-test.sh -c 10 -M 512M -S 100M

# Run quick tests
dd if=/dev/urandom of=/dev/mapper/dm-test-remap bs=4K count=100

# Check results
dmsetup status dm-test-remap

# Cleanup
sudo ./setup-dm-remap-test.sh --cleanup
```

### Stress Testing

```bash
# Create large setup with many bad sectors
sudo ./setup-dm-remap-test.sh -M 10G -S 2G -c 5000 --verbose

# Run extended tests
fio --name=stress \
    --filename=/dev/mapper/dm-test-remap \
    --rw=randrw \
    --bs=4K \
    --size=5G \
    --runtime=3600

# Monitor
watch -n 1 'dmsetup status dm-test-remap'

# Cleanup
sudo ./setup-dm-remap-test.sh --cleanup
```

### CI/CD Integration

```bash
#!/bin/bash
set -e

# Setup test environment
sudo ./setup-dm-remap-test.sh -c 100 --quiet

# Run tests
cd tests
./run-dm-remap-tests.sh

# Cleanup
sudo ./setup-dm-remap-test.sh --cleanup
```

## Common Use Cases

### Case 1: Test Metadata Robustness

```bash
# Create bad sectors in early device area
cat > metadata_bad_sectors.txt << EOF
# Metadata area (first 1000 sectors)
0
1
5
10
50
100
500
1000
EOF

sudo ./setup-dm-remap-test.sh -f metadata_bad_sectors.txt -v
```

### Case 2: Test Fragmentation Handling

```bash
# Create fragmented bad sector pattern
cat > fragmented_bad_sectors.txt << EOF
# Fragmented pattern
0
2
4
6
8
10
100
102
104
106
1000
1002
1004
EOF

sudo ./setup-dm-remap-test.sh -f fragmented_bad_sectors.txt
```

### Case 3: Test Sparse Failures

```bash
# Test with 1% bad sectors (minimal impact)
sudo ./setup-dm-remap-test.sh -p 1 -M 5G -S 1G

# Test with 10% bad sectors (significant impact)
sudo ./setup-dm-remap-test.sh -p 10 -M 5G -S 1G
```

### Case 4: Compatibility Testing

```bash
# Different sector sizes for various filesystems
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 100   # Older filesystems
sudo ./setup-dm-remap-test.sh --sector-size 4096 -c 100  # Modern ext4/btrfs
sudo ./setup-dm-remap-test.sh --sector-size 8192 -c 100  # Some RAID systems
```

## Contributing

Suggestions for improvements:
- Additional bad sector patterns (hot-spots, wear patterns)
- Performance profiling output
- Automated test suite integration
- Support for multiple dm devices
- Graphical visualization

## License

This script is part of dm-remap and follows the same license (GPL).

## See Also

- dm-remap documentation in `/docs/development/`
- Linux Device Mapper documentation: `man dmsetup`
- Linux loopback devices: `man losetup`
- Block device utilities: `man blockdev`
