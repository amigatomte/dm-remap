# dm-remap Troubleshooting Guide

This directory contains comprehensive troubleshooting documentation and diagnostic tools for common issues with dm-remap.

## Quick Start

If dm-remap isn't working as expected:

```bash
# 1. Run the automated diagnostic
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh

# 2. Read the relevant guide based on your issue (see below)

# 3. Follow the recommended fixes
```

---

## I/O Errors During Pool/Filesystem Creation

If you're experiencing **ZFS pool or ext4 filesystem creation failures**, these guides explain the issue and solution:

| Document | Purpose |
|----------|---------|
| **[READ_ERROR_PROPAGATION_ISSUE.md](READ_ERROR_PROPAGATION_ISSUE.md)** | Root cause analysis |
| **[FIX_ERROR_PROPAGATION.md](FIX_ERROR_PROPAGATION.md)** | Implementation guide |
| **[ERROR_SUPPRESSION_VISUAL_GUIDE.md](ERROR_SUPPRESSION_VISUAL_GUIDE.md)** | Visual explanation with diagrams |
| **[IMPLEMENTATION_CHECKLIST.md](IMPLEMENTATION_CHECKLIST.md)** | Step-by-step testing procedures |

**Symptoms:**
- ZFS pool creation fails with "cannot create 'mynewpool': I/O error"
- ext4 mkfs fails with "Warning: could not erase sector 2: Input/output error"
- Kernel logs show persistent I/O errors on sectors 0, 2, etc.
- dm-remap status shows errors being counted but not remapped

See `READ_ERROR_PROPAGATION_ISSUE.md` for complete technical analysis.

---

## Available Guides

### I/O Errors During Pool/Filesystem Creation (Continued)

**Root Cause:**
Write errors are returned immediately to the filesystem while the async remap creation is still in progress. The filesystem doesn't wait and aborts before the remap is ready.

**Solution:**
Suppress write errors and allow async remap creation. Filesystems continue normally, and future I/O uses the remap transparently.

**Implementation:**
Refer to `FIX_ERROR_PROPAGATION.md` for:
1. Helper functions to identify write operations
2. Spare pool capacity validation
3. Error suppression when safe to do so

---

### 🔴 ZFS Pool Creation Fails with "I/O error" (Old)

**File**: [`ZFS_QUICK_FIX.md`](ZFS_QUICK_FIX.md)

**Note**: The main issue is actually the I/O error propagation bug above, not sector size.

**Secondary Causes** (also check):
- Sector size mismatch between dm-remap and loop devices
- Device property incompatibility
- Physical block size misalignment

**Diagnostic Script**:
```bash
sudo ./scripts/diagnose-zfs-compat.sh
```

**Full Documentation**: Read [`ZFS_QUICK_FIX.md`](ZFS_QUICK_FIX.md) for additional checks.

### 📋 Deep Dive: ZFS Compatibility Analysis

**File**: [`ZFS_COMPATIBILITY_ISSUE.md`](ZFS_COMPATIBILITY_ISSUE.md)

For detailed technical analysis including:
- 5 potential root causes of ZFS failures
- Device property interactions
- Kernel-level diagnostics
- Extended troubleshooting procedures
- Prevention strategies

---

## Diagnostic Tool

### Running the Diagnostic Script

```bash
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh
```

**What it checks**:
- ✅ Device existence
- ✅ Sector size matching
- ✅ Physical block sizes
- ✅ Device sizes compatibility
- ✅ Kernel queue properties
- ✅ Loopback device configuration
- ✅ ZFS installation status
- ✅ dm-remap device status
- ✅ I/O performance
- ✅ Kernel error logs

**Output**:
- Color-coded results (green=pass, yellow=warning, red=fail)
- Specific remediation steps for each issue
- Actionable recommendations

---

## Common Issues at a Glance

| Issue | Symptoms | Reference |
|-------|----------|-----------|
| **Filesystem creation fails with I/O errors** | mkfs.ext4 or zpool fails on errors | `READ_ERROR_PROPAGATION_ISSUE.md` |
| **I/O errors on sectors 0, 2, etc.** | Specific sectors show errors | `FIX_ERROR_PROPAGATION.md` |
| **Device property incompatibility** | ZFS rejects device with warnings | `ZFS_COMPATIBILITY_ISSUE.md` |
| **dm-remap device not found** | Module fails to load | Run `setup-dm-remap-test.sh` |
| **Poor performance** | Unexpectedly high latency | Check physical block size alignment |

---

## Device Property Reference

### Key Properties ZFS Checks

**Logical Sector Size** (what ZFS sees for data operations)
- Standard: 512 bytes
- Modern: 4096 bytes
- **Must match** across mirror devices

**Physical Block Size** (hardware alignment)
- Typical: 512 or 4096 bytes
- Affects performance
- **Should ideally match** logical size

**Device Size** (total capacity)
- **Can differ slightly** - ZFS uses smallest device
- Must be large enough for pool data

**Queue Parameters** (kernel-level I/O configuration)
- Logical block size
- Physical block size
- Read ahead size
- I/O scheduler

---

## Testing Your Fix

### After applying a fix:

```bash
# 1. Verify device properties
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap
# Both should report same value

# 2. Verify pool creation works
sudo zpool create testpool /dev/mapper/dm-test-remap

# 3. Verify pool is accessible
sudo zfs list
sudo zpool status testpool

# 4. Clean up test pool
sudo zpool destroy testpool

# 5. Try mirror again
sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2
```

---

## Getting Help

### If the diagnostic script says "ALL CHECKS PASSED" but ZFS still fails:

1. **Try with explicit ashift**:
   ```bash
   sudo zpool create -o ashift=9 mynewpool mirror \
       /dev/mapper/dm-test-remap /dev/loop2
   ```

2. **Check kernel logs**:
   ```bash
   sudo dmesg | grep -i "error\|remap" | tail -50
   ```

3. **Try with single device first**:
   ```bash
   sudo zpool create testpool /dev/mapper/dm-test-remap
   ```

4. **File a bug report** with:
   - Output of diagnostic script
   - Your setup command
   - Kernel logs (dmesg)
   - Expected vs actual behavior

---

## Files in This Directory

- **`README.md`** (this file) - Navigation and overview
- **`ZFS_QUICK_FIX.md`** - Quick fix guide for ZFS issues
- **`ZFS_COMPATIBILITY_ISSUE.md`** - Detailed technical analysis

---

## Related Resources

- **Diagnostic Script**: `/home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh`
- **Setup Script**: `/home/christian/kernel_dev/dm-remap/tests/loopback-setup/setup-dm-remap-test.sh`
- **dm-remap Documentation**: `/home/christian/kernel_dev/dm-remap/docs/`
- **ZFS Documentation**: https://openzfs.github.io/openzfs-docs/

---

## Version Info

- **Last Updated**: November 4, 2024
- **dm-remap Version**: v4.0-phase1.4
- **Tested With**: Linux kernel 5.x+, ZFS 2.x

