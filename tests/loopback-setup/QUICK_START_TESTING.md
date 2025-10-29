# Test Setup Quick Start

Comprehensive test environment for dm-remap with configurable bad sectors.

## Installation

```bash
# The script is already in the repository
ls -lh setup-dm-remap-test.sh
```

## Quick Examples

### 1. Create test setup with 100 random bad sectors

```bash
sudo ./setup-dm-remap-test.sh -c 100
```

**Creates:**
- `main.img` (100MB sparse file)
- `spare.img` (20MB sparse file)  
- `/dev/loop0` → main.img
- `/dev/loop1` → spare.img
- `/dev/mapper/dm-test-linear` (dm-linear with 100 error zones)
- `/dev/mapper/dm-test-remap` (dm-remap device)

### 2. Preview what would happen (safe, no changes)

```bash
sudo ./setup-dm-remap-test.sh -c 50 --dry-run -v
```

**Shows:**
- All operations that would be performed
- Number of bad sectors and their IDs
- Table entries that would be created
- No actual files or devices created

### 3. Use 5% bad sectors on 1GB device

```bash
sudo ./setup-dm-remap-test.sh -M 1G -S 200M -p 5
```

**Creates:**
- 1GB main device with ~5% bad sectors
- 200MB spare pool
- Calculates exact number: ~128 sectors

### 4. Use specific bad sectors from file

```bash
sudo ./setup-dm-remap-test.sh -f example_bad_sectors.txt
```

**Uses predefined patterns:**
```
# File: example_bad_sectors.txt
0       # Sector 0 (metadata area)
5       # Sector 5
1000    # Sector 1000
...
```

### 5. Clean up after testing

```bash
sudo ./setup-dm-remap-test.sh --cleanup
```

**Removes:**
- `/dev/mapper/dm-test-remap`
- `/dev/mapper/dm-test-linear`
- Loopback devices
- Image files

## Testing the Setup

After creating the test environment:

### Check what was created

```bash
# List device mappings
dmsetup ls

# View device table
dmsetup table dm-test-remap
dmsetup table dm-test-linear

# Check loopback devices
losetup -a

# View image files
ls -lh main.img spare.img
```

### Test I/O

```bash
# Write test data (triggers remapping on bad sectors)
dd if=/dev/urandom of=/dev/mapper/dm-test-remap bs=4K count=1000

# Check device status
dmsetup status dm-test-remap

# Monitor activity
watch -n 1 'dmsetup status dm-test-remap'
```

### Verify Bad Sectors

```bash
# Read device info
modinfo dm_remap

# Check if module is loaded
lsmod | grep dm_remap

# View kernel logs
dmesg | tail -20 | grep -i dm-remap
```

## Common Scenarios

### Scenario 1: Test Metadata Robustness

```bash
# Create bad sectors in early device area (metadata zone)
cat > metadata_bad_sectors.txt << EOF
0
1
5
10
50
100
EOF

sudo ./setup-dm-remap-test.sh -f metadata_bad_sectors.txt
```

### Scenario 2: Stress Test

```bash
# Large device with many bad sectors
sudo ./setup-dm-remap-test.sh -M 10G -S 2G -c 5000

# Run intensive I/O
fio --name=stress \
    --filename=/dev/mapper/dm-test-remap \
    --rw=randrw \
    --bs=4K \
    --size=5G \
    --runtime=3600 \
    --direct=1
```

### Scenario 3: Fragmentation Testing

```bash
# Create clustered bad sectors
cat > fragmented_bad_sectors.txt << EOF
# Cluster 1: sectors 1000-1010
1000
1001
1002
1003
1004
1005
1006
1007
1008
1009
1010

# Cluster 2: sectors 5000-5020
5000
5001
5002
...
5020
EOF

sudo ./setup-dm-remap-test.sh -f fragmented_bad_sectors.txt
```

### Scenario 4: Different Sector Sizes

```bash
# 512-byte sectors (older filesystems)
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 100

# 4096-byte sectors (modern default)
sudo ./setup-dm-remap-test.sh --sector-size 4096 -c 100

# 8192-byte sectors (some RAID systems)
sudo ./setup-dm-remap-test.sh --sector-size 8192 -c 100
```

## Architecture Overview

```
Application
    ↓
/dev/mapper/dm-test-remap
    ↓ (dm-remap intercepts)
/dev/mapper/dm-test-linear
    ├─ Normal sectors → passes through
    ├─ Bad sectors    → returns error (caught by dm-remap)
    └─ Remainder      → passes through
    ↓
/dev/loop0 ← main.img
/dev/loop1 ← spare.img
    ↓
Image Files (sparse or regular)
```

## Troubleshooting

### "No free loopback device available"

```bash
# Add more loopback devices
sudo modprobe loop max_loop=16

# Check available
losetup -a
```

### "Device or resource busy"

```bash
# Check what has files open
lsof +D .

# Flush filesystem cache
sync; sleep 1

# Remove loopback devices more forcefully
sudo dmsetup remove dm-test-remap
sudo dmsetup remove dm-test-linear
sudo losetup -D  # Detach all
```

### "dm-remap module not found"

```bash
# Build and load module
cd /path/to/dm-remap
make
sudo insmod src/dm-remap.ko

# Or use DKMS package
sudo apt install dm-remap-dkms
```

### "Permission denied"

```bash
# Must run with sudo
sudo ./setup-dm-remap-test.sh -c 100
```

## Parameters Reference

### Image Configuration

| Parameter | Default | Example |
|-----------|---------|---------|
| --main | main.img | -m custom_main.img |
| --spare | spare.img | -s custom_spare.img |
| --main-size | 100M | -M 1G |
| --spare-size | 20M | -S 500M |
| --sparse | yes | --sparse no |

### Bad Sectors

| Method | Option | Example |
|--------|--------|---------|
| Random count | -c, --bad-sectors-count | -c 100 |
| Percentage | -p, --bad-sectors-percent | -p 5 |
| From file | -f, --bad-sectors-file | -f sectors.txt |

### Other

| Parameter | Effect |
|-----------|--------|
| --sector-size | Set sector size in bytes (default 4096) |
| --dry-run | Preview only, no changes |
| -v, --verbose | Verbose output with debug info |
| --cleanup | Remove all artifacts |
| -h, --help | Show help message |

## Performance Considerations

### Sparse vs. Regular Files

- **Sparse (default)**: Fast creation, low initial disk usage, slightly slower I/O
- **Regular**: Slower creation, preallocated space, better I/O performance

### Bad Sector Density

- **Few (1-10)**: Quick testing, edge cases
- **Many (1000+)**: Stress testing, worst-case behavior
- **Random**: Realistic failure patterns
- **Clustered**: Fragmentation and data recovery

## Next Steps

1. **Run dry-run**: `sudo ./setup-dm-remap-test.sh -c 50 --dry-run -v`
2. **Create test environment**: `sudo ./setup-dm-remap-test.sh -c 100`
3. **Run tests**: `dd if=/dev/urandom of=/dev/mapper/dm-test-remap ...`
4. **Monitor**: `dmsetup status dm-test-remap`
5. **Clean up**: `sudo ./setup-dm-remap-test.sh --cleanup`

## Documentation

For detailed information, see:
- `TEST_SETUP_README.md` - Full documentation (800+ lines)
- `example_bad_sectors.txt` - Example bad sector patterns
- `setup-dm-remap-test.sh -h` - Built-in help

## Support

The script provides:
- Comprehensive logging
- Error handling and recovery
- Dry-run validation
- Automatic cleanup
- Verbose debug output
- Built-in help and examples
