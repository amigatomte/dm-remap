# Loopback Device Test Setup

This directory contains the complete test setup system for dm-remap using loopback devices.

## Quick Start

```bash
# View help
sudo ./setup-dm-remap-test.sh --help

# Create a test setup with 20 random bad sectors (dry-run preview)
sudo ./setup-dm-remap-test.sh -c 20 --dry-run -v

# Create an actual test setup with sparse files
sudo ./setup-dm-remap-test.sh -m main.img -s spare.img -c 50 --sparse
```

## Files in This Directory

### Main Script
- **setup-dm-remap-test.sh** - Primary test setup script
  - Creates loopback devices
  - Sets up dm-linear with error zones
  - Configures dm-remap mapping
  - Manages bad sector generation

### Documentation
- **QUICK_START_TESTING.md** - Quick start guide with 5 common scenarios
- **TEST_SETUP_README.md** - Comprehensive reference documentation (40+ examples)
- **TEST_SETUP_COMPLETION.md** - Implementation summary and feature list

### Examples
- **example_bad_sectors.txt** - Example bad sector configuration file

## Configuration Methods

The script supports three methods for specifying bad sectors:

1. **Fixed Count**: `-c N` - Create N random bad sectors
2. **Percentage**: `-p P` - Bad sectors cover P% of device
3. **From File**: `-f FILE` - Load specific sector IDs from file

## Basic Usage

```bash
# Setup with configuration
sudo ./setup-dm-remap-test.sh [OPTIONS]

# Common options:
  -m, --main FILE         Main device image file (default: main.img)
  -s, --spare FILE        Spare device image file (default: spare.img)
  -c, --count N           Number of random bad sectors
  -p, --percent P         Percentage of device as bad sectors
  -f, --file FILE         Load bad sectors from file
  --sparse                Create sparse files (default: regular)
  --main-size SIZE        Main device size (default: 100M)
  --spare-size SIZE       Spare device size (default: 50M)
  --dry-run               Preview configuration without changes
  -v, --verbose           Verbose output
  -h, --help              Show full help message
```

## Device Stack Created

```
┌─────────────────────────┐
│   dm-remap device       │
│   (remaps bad sectors)  │
└────────┬────────────────┘
         │
┌────────┴────────────────────┐
│   dm-linear with errors     │
│   (simulates bad sectors)   │
└────────┬───────────────────┐
         │                   │
    /dev/loop0          /dev/loop1
   (main.img)          (spare.img)
```

## Documentation Structure

1. **Start here**: QUICK_START_TESTING.md (5-minute introduction)
2. **Learn more**: TEST_SETUP_README.md (comprehensive guide)
3. **Reference**: Help output with `./setup-dm-remap-test.sh --help`

## Testing Workflow

1. Review QUICK_START_TESTING.md for examples
2. Run a dry-run to preview: `sudo ./setup-dm-remap-test.sh -c 20 --dry-run -v`
3. Create the test setup: `sudo ./setup-dm-remap-test.sh -c 20 -v`
4. Test dm-remap behavior
5. Clean up: `sudo ./setup-dm-remap-test.sh --cleanup`

## Support Files

- `example_bad_sectors.txt` - Shows how to format custom bad sector files
- Contains comments, ranges, and various patterns

## See Also

- TEST_SETUP_README.md - Full technical documentation
- QUICK_START_TESTING.md - Common scenarios and quick start
- TEST_SETUP_COMPLETION.md - Feature list and implementation details
