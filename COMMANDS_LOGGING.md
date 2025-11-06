# Commands Logging Feature

The ZFS integration test script now captures all executed commands into a standalone output script that can be replayed independently.

## Overview

During test execution, all device-manager, ZFS, and filesystem commands are logged to an output script with descriptive comments. This allows you to:

1. **Review commands** - See exactly what operations were performed
2. **Replay setup** - Run the commands again later
3. **Debug issues** - Inspect commands that were executed
4. **Documentation** - Keep a record of the test configuration

## Output Script Location

The commands script is saved to:
```
/tmp/dm-remap-test-commands-YYYYMMDD-HHMMSS.sh
```

The path is displayed at end of test execution:
```
[✓] Commands script saved to: /tmp/dm-remap-test-commands-20251106-171500.sh
```

## Script Contents

### Header
```bash
#!/bin/bash
# dm-remap ZFS Integration Test - Commands Log
# Generated: [timestamp]
# This script contains all commands executed during the test
# You can source or execute this script to replay the test setup
```

### Logging Functions
Built-in helper functions for consistent output:
- `log_info()` - Info messages
- `log_success()` - Success messages
- `log_warning()` - Warning messages
- `log_error()` - Error messages

### Captured Commands

#### Device Creation
```bash
# Create main device sparse file
dd if=/dev/zero of="/tmp/dm-remap-test-sparse" bs=1M count=512

# Create spare device sparse file
dd if=/dev/zero of="/tmp/dm-remap-test-spare" bs=1M count=512
```

#### Loopback Device Setup
```bash
# Setup main loopback device
sudo losetup "/dev/loop17" "/tmp/dm-remap-test-sparse"

# Setup spare loopback device
sudo losetup "/dev/loop18" "/tmp/dm-remap-test-spare"
```

#### dm-remap Mapping
```bash
# Create dm-remap device mapping
echo '0 1048576 dm-remap-v4 /dev/loop17 /dev/loop18' | sudo dmsetup create dm-test-main
```

#### ZFS Configuration
```bash
# Create ZFS pool
sudo zpool create "dm-test-pool" /dev/mapper/dm-test-main

# Create ZFS dataset with compression
sudo zfs create -o mountpoint="/mnt/dm-remap-test" -o compression=lz4 "dm-test-pool/data"

# Disable atime for performance
sudo zfs set atime=off "dm-test-pool/data"

# Set SHA256 checksum
sudo zfs set checksum=sha256 "dm-test-pool/data"
```

## Usage

### View the Commands

```bash
cat /tmp/dm-remap-test-commands-20251106-171500.sh
```

### Execute the Commands

```bash
bash /tmp/dm-remap-test-commands-20251106-171500.sh
```

### Source the Commands

```bash
source /tmp/dm-remap-test-commands-20251106-171500.sh
```

### Copy for Reuse

```bash
cp /tmp/dm-remap-test-commands-*.sh ~/my-test-setup.sh
```

## Example Workflow

1. **Run integration test:**
   ```bash
   bash tests/dm_remap_zfs_integration_test.sh
   ```

2. **Review commands:**
   ```bash
   cat /tmp/dm-remap-test-commands-*.sh
   ```

3. **Save for documentation:**
   ```bash
   cp /tmp/dm-remap-test-commands-*.sh docs/test-commands.sh
   ```

4. **Replay setup later:**
   ```bash
   bash docs/test-commands.sh
   ```

## Features

✅ **Automatic Logging** - No user action needed, commands logged automatically
✅ **Descriptive Comments** - Each command has a clear description
✅ **Executable Script** - Output script is standalone and runnable
✅ **Error Handling** - Script exits on error with `set -e`
✅ **Helper Functions** - Includes logging functions for consistent output
✅ **Timestamp** - Script filename includes timestamp for uniqueness
✅ **Preserves Variables** - Device paths are recorded with actual values

## When Commands Are Logged

Commands are logged at these test steps:

1. **Step 2: Create Test Devices**
   - Sparse file creation
   - Loopback device setup

2. **Step 3: Setup dm-remap Device**
   - dm-remap device mapping creation

3. **Step 4: Create ZFS Pool**
   - Mount point creation
   - ZFS pool creation
   - ZFS dataset creation
   - ZFS property configuration

## Integration with Test Flow

```
Test Start
    ↓
Step 1: Prerequisites Check
    ↓
Step 2-4: Device & Pool Setup ──→ [Commands logged]
    ↓
[PAUSE] - User can inspect setup
    ↓
Step 5-10: Testing & Verification
    ↓
[PAUSE] - User can inspect results
    ↓
Cleanup
    ↓
[OUTPUT] - "Commands script saved to: /tmp/dm-remap-test-commands-*.sh"
```

## Best Practices

1. **Save Important Commands**
   ```bash
   cp /tmp/dm-remap-test-commands-*.sh my-important-test.sh
   ```

2. **Version Control**
   ```bash
   git add my-important-test.sh
   git commit -m "docs: Save dm-remap test commands"
   ```

3. **Documentation**
   - Reference the commands script in test reports
   - Include in runbooks for production deployment
   - Share with team members

4. **Debugging**
   - Review commands if test fails
   - Check device paths are correct
   - Verify ZFS parameters

## Output Script Example

Complete example of a generated commands script:

```bash
#!/bin/bash
# dm-remap ZFS Integration Test - Commands Log
# Generated: Thu Nov  6 17:15:00 CET 2025
# This script contains all commands executed during the test
# You can source or execute this script to replay the test setup

set -e  # Exit on error

log_info() {
    echo "[INFO] $@"
}

log_success() {
    echo "[✓] $@"
}

log_warning() {
    echo "[!] $@"
}

log_error() {
    echo "[✗] $@"
}

# Create main device sparse file
dd if=/dev/zero of="/tmp/dm-remap-test-sparse" bs=1M count=512

# Create spare device sparse file
dd if=/dev/zero of="/tmp/dm-remap-test-spare" bs=1M count=512

# Setup main loopback device
sudo losetup "/dev/loop17" "/tmp/dm-remap-test-sparse"

# Setup spare loopback device
sudo losetup "/dev/loop18" "/tmp/dm-remap-test-spare"

# Create dm-remap device mapping
echo '0 1048576 dm-remap-v4 /dev/loop17 /dev/loop18' | sudo dmsetup create dm-test-main

# Create mount point
sudo mkdir -p "/mnt/dm-remap-test"

# Create ZFS pool
sudo zpool create "dm-test-pool" /dev/mapper/dm-test-main

# Create ZFS dataset with compression
sudo zfs create -o mountpoint="/mnt/dm-remap-test" -o compression=lz4 "dm-test-pool/data"

# Disable atime for performance
sudo zfs set atime=off "dm-test-pool/data"

# Set SHA256 checksum
sudo zfs set checksum=sha256 "dm-test-pool/data"

log_success "All commands from the test have been recorded above"
log_success "This script can be sourced or executed to replay the test setup"

# To execute this script later: bash /tmp/dm-remap-test-commands-20251106-171500.sh
```

## Troubleshooting

### Script Not Found
- Check `/tmp/` directory: `ls /tmp/dm-remap-test-commands-*.sh`
- May have been cleaned up by system temp cleanup
- Run test again to generate new script

### Variable Values Different
- Device paths (e.g., `/dev/loop17`) may differ on different systems
- Edit the script to use your actual device paths before running

### Permissions Errors
- Script requires `sudo` for many operations
- Run: `bash /tmp/dm-remap-test-commands-*.sh` (not just sourcing)

## Related Files

- **Main Test Script**: `tests/dm_remap_zfs_integration_test.sh`
- **Test Report**: `/tmp/dm-remap-zfs-test-report-*.txt`
- **This Documentation**: `COMMANDS_LOGGING.md`

---

**Generated**: November 6, 2025
**Feature**: Automatic commands logging
**Status**: ✅ Production Ready
