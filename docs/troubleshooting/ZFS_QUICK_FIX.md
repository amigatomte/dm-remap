# ZFS Pool Creation with dm-remap - Troubleshooting Guide

## Your Issue

```bash
$ sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2
cannot create 'mynewpool': I/O error
```

**Short Answer**: You likely did **nothing wrong**. This is probably a **dm-remap device property mismatch** that prevents ZFS from using it.

---

## Quick Diagnosis

Run this to identify the issue:

```bash
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh
```

This will check:
- ✅ Sector size match
- ✅ Device size match
- ✅ Physical block size match
- ✅ Kernel logs for errors

---

## Most Likely Problem: Sector Size Mismatch

### What's Happening

ZFS requires all mirror devices to have **identical sector sizes**. 

**Expected**:
- Loop devices typically report **512 bytes** per sector
- dm-remap should report the **same** 512 bytes

**What probably happened**:
- Your setup script created dm-remap with **4096 bytes** sectors
- Loop devices have **512 bytes** sectors
- ZFS sees the mismatch and rejects dm-remap

### How to Fix It

**Step 1**: Clean up current setup

```bash
sudo dmsetup remove dm-test-remap 2>/dev/null
sudo dmsetup remove dm-test-linear 2>/dev/null
sudo losetup -d /dev/loop0 /dev/loop1 /dev/loop2 2>/dev/null
rm -f ~/src/dm-remap/tests/loopback-setup/{main,spare}.img
```

**Step 2**: Recreate with correct sector size

```bash
cd ~/src/dm-remap/tests/loopback-setup
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10 -M 100M -S 20M
```

**Step 3**: Verify sectors match

```bash
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap
# Both should report: 512
```

**Step 4**: Create ZFS pool

```bash
sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2
```

---

## If That Doesn't Work

### Option 1: Explicit ashift Value

ZFS uses `ashift` to specify sector size (in power-of-2 form):
- ashift=9 means 2^9 = **512 bytes** (standard)
- ashift=12 means 2^12 = **4096 bytes** (modern)

Try creating the pool with explicit ashift:

```bash
sudo zpool create -o ashift=9 mynewpool mirror \
    /dev/mapper/dm-test-remap /dev/loop2
```

### Option 2: Test dm-remap Alone

If the mirror fails, test dm-remap in a single-device pool:

```bash
sudo zpool create testpool /dev/mapper/dm-test-remap
sudo zfs list
sudo zpool destroy testpool
```

If this works, the issue is mirror-specific compatibility. If it fails, dm-remap has a deeper problem.

### Option 3: Check Kernel Logs

```bash
sudo dmesg | grep -i "remap\|error" | tail -20
```

Look for:
- I/O errors from dm-remap
- Module load failures
- Device capability errors

---

## Device Properties You Should Check

### 1. Sector Sizes
```bash
# Logical sector size (what ZFS sees)
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap

# Physical block size (what filesystem uses)
blockdev --getpbsz /dev/loop2
blockdev --getpbsz /dev/mapper/dm-test-remap
```

All four should be identical (typically 512 or 4096).

### 2. Device Sizes
```bash
blockdev --getsz /dev/loop2
blockdev --getsz /dev/mapper/dm-test-remap
```

Should be equal or very close (ZFS uses the smaller size).

### 3. Queue Parameters
```bash
cat /sys/block/loop2/queue/logical_block_size
cat /sys/block/loop2/queue/physical_block_size

# Find dm device (e.g., dm-0, dm-1, etc.)
dmsetup ls | grep dm-test-remap

cat /sys/block/dm-0/queue/logical_block_size  # adjust dm-0 as needed
cat /sys/block/dm-0/queue/physical_block_size
```

These are what kernel/ZFS see internally.

---

## Understanding the Setup Script

Your setup script creates dm-remap in these layers:

```
Actual Disk/Loop
      ↓
/dev/loop0 (main.img) → dm-linear → dm-remap → /dev/mapper/dm-test-remap
/dev/loop1 (spare.img) ← remapped sectors
```

The `--sector-size` parameter in the setup script controls how dm-linear interprets the device:

```bash
# Default (4096-byte sectors - for modern filesystems)
./setup-dm-remap-test.sh -c 10

# For ZFS (512-byte sectors - traditional)
./setup-dm-remap-test.sh --sector-size 512 -c 10
```

**This is critical**: The sector size parameter must match what loop devices report!

---

## Why ZFS Cares About Sector Size

ZFS uses sector information for:
- **RAID alignment** - Ensures data isn't split across sector boundaries
- **Performance** - Optimizes I/O to natural sector boundaries
- **Data integrity** - Knows exact unit of hardware atomicity
- **Pool creation** - Rejects incompatible device combinations

If sector sizes don't match, ZFS can't guarantee these properties, so it fails with "I/O error".

---

## Complete Workflow to Test

```bash
# 1. Verify current setup
losetup -l

# 2. Run diagnostic
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh

# 3. If fixes needed, clean up
sudo dmsetup remove dm-test-remap dm-test-linear 2>/dev/null
sudo losetup -d /dev/loop{0,1,2} 2>/dev/null
rm -f tests/loopback-setup/*.img

# 4. Recreate with correct sector size
cd tests/loopback-setup
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10

# 5. Verify
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap

# 6. Create pool
sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2

# 7. Verify pool
sudo zfs list
sudo zpool status mynewpool
```

---

## FAQ

**Q: Did I do something wrong?**  
A: Probably not. You used the tool correctly. This is likely a dm-remap configuration issue.

**Q: Why does it only fail with mirrors?**  
A: Single-device pools are more lenient. Mirrors require device compatibility.

**Q: Can I use different sector sizes?**  
A: Not in a mirror/raid config. All devices must match. Single devices can differ.

**Q: What if the diagnostic says everything is correct?**  
A: Then the issue is deeper - possibly in dm-remap's I/O handling or ZFS integration. File a bug report with the diagnostic output.

**Q: Can I use dm-remap with ZFS at all?**  
A: Yes! Once device properties align, it should work fine.

---

## Files Created for You

**Diagnostic Script**: `/home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh`

**Troubleshooting Guide**: `/home/christian/kernel_dev/dm-remap/docs/troubleshooting/ZFS_COMPATIBILITY_ISSUE.md`

---

## Next Steps

1. **Run the diagnostic**: `sudo scripts/diagnose-zfs-compat.sh`
2. **Follow its recommendations**
3. **Report back** with the diagnostic output if issues persist

The diagnostic will tell you exactly what needs to be fixed!
