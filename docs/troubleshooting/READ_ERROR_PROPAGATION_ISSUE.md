# dm-remap Read Error Handling: Critical Analysis

**Date**: November 4, 2025  
**Status**: ⚠️ CRITICAL ISSUE IDENTIFIED

## Your Question: "Does dm-remap do no remapping on read errors?"

**Answer: NO - This is the problem!**

dm-remap **DOES attempt to remap on read errors**, but there's a critical flaw in how it handles these errors during initial pool/filesystem creation.

---

## The Issue

### What's Happening

When you run `sudo mkfs.ext4 /dev/mapper/dm-test-remap`:

1. **ext4 tries to write to sector 0 and sector 2**
2. **dm-remap receives the WRITE request**
3. **Write to main device FAILS** (returns I/O error)
4. **dm-remap end_io handler is called** with `error != BLK_STS_OK`
5. **dm-remap attempts to create a remap entry** for that sector
6. **But the remap is still being created** (queued to workqueue)
7. **The original I/O error is PROPAGATED IMMEDIATELY** back to ext4
8. **ext4 sees the error and fails**
9. **Meanwhile, dm-remap is queuing the remap creation in the background**

### The Race Condition

```
Timeline:
t0: ext4 tries to write sector 0
t1: dm-remap gets I/O error
t2: dm-remap queues remap work (non-blocking)
t3: dm-remap returns ERROR to ext4 immediately (no wait!)
t4: ext4 sees error and aborts
t5: (later) dm-remap finally creates the remap entry
t6: But ext4 pool creation has already failed!
```

### Code Evidence

From `dm-remap-v4-real-main.c` line 2342-2385:

```c
static int dm_remap_end_io_v4_real(struct dm_target *ti, struct bio *bio,
                                  blk_status_t *error)
{
    // ...
    if (*error != BLK_STS_OK) {
        sector_t failed_sector = bio->bi_iter.bi_sector;
        int errno_val = blk_status_to_errno(*error);
        
        DMR_WARN("I/O error detected on sector %llu (error=%d)",
                 (unsigned long long)failed_sector, errno_val);
        
        if (device->main_dev) {
            // ...
            /* Queue write-ahead remap creation <-- ASYNCHRONOUS! */
            dm_remap_handle_io_error(device, failed_sector, errno_val);
            //                                               ↑
            //                                      Returns immediately
        }
    }
    
    return DM_ENDIO_DONE;  /* <-- ERROR IS NOT CLEARED! */
}
```

**Key Problem**: The error in `*error` is **NOT CLEARED**. It's returned to the caller.

---

## Why This Fails for ZFS/ext4

### The Design Assumption (Wrong)

The current code assumes:
> "If we encounter a read error, we'll queue a remap. Next I/O to that sector will find the remap and succeed."

### The Reality

During filesystem creation:
- **First I/O attempt fails** and returns error to filesystem
- **Filesystem may retry or abort** depending on configuration
- **Filesystem doesn't know to retry** after remap is created
- **By the time remap is ready**, filesystem has already failed

---

## The Fix Options

### Option 1: ✅ **Clear the Error and Succeed** (Recommended for WRITE)

For WRITE operations on sectors that can be remapped:
- Create remap entry synchronously (or wait for it)
- Clear the error (`*error = BLK_STS_OK`)
- Return success
- The write goes to the spare device now

**When to use**: Write operations to bad sectors

### Option 2: ✅ **Synchronous Remapping**

Instead of queuing async work:
- Perform remap creation **synchronously** in end_io
- Then clear error and return success
- Guarantees remap is ready for next I/O

**Problem**: end_io is in atomic context, might deadlock

### Option 3: ⚠️ **Read Error Handling Only**

For READ errors:
- You can't retry from spare (no data there yet)
- Must fail the read
- This is correct behavior

For WRITE errors:
- Can redirect to spare device
- Should succeed after creating remap

**Current code doesn't distinguish**

### Option 4: ⚠️ **Retry Logic**

Build retry into dm-remap:
- First I/O fails, create remap
- Return error but mark for retry
- Caller retries, finds remap, succeeds

**Problem**: Most filesystems don't retry on I/O error

---

## Why This Works Normally

### With Single Sector Reads/Writes

```
Application reads sector X
  ↓
I/O error occurs
  ↓
dm-remap creates remap
  ↓
Application retries read (internally or next I/O)
  ↓
Remap is ready, read succeeds
```

### Why It Fails with ZFS/ext4

```
ZFS tries to create pool (bulk I/O)
  ↓
Multiple sectors fail (0, 2, etc.)
  ↓
First error seen, pool creation aborts
  ↓
ZFS doesn't retry
  ↓
Remaps never used
```

---

## Why Sectors 0 and 2 Are Failing

Likely causes:

1. **Spare device not initialized/readable**
   - dm-remap can't verify spare device is working
   - Assumes it will work on first remap
   - Spare device actually fails when needed

2. **Loop device size mismatch**
   - Main and spare devices have different sizes
   - Sector alignment issues
   - Remap can't map to spare properly

3. **Spare pool exhaustion**
   - Spare device isn't large enough
   - Remap metadata takes space
   - No space left to remap to

4. **Physical block size mismatch**
   - Kernel sees different physical block sizes
   - dm-remap can't redirect I/O correctly

---

## Verification Steps

### 1. Check if Remaps Are Actually Created

```bash
sudo dmsetup message dm-test-remap 0 status
# Look at "active_mappings" count

# Should see increase if remaps are being created
```

### 2. Check Spare Device Accessibility

```bash
# Before creating pool
sudo dd if=/dev/loop1 of=/dev/null bs=512 count=10
# Should succeed

# Try to write to spare
sudo dd if=/dev/zero of=/dev/loop1 bs=512 count=10
# Should succeed
```

### 3. Check Kernel Logs

```bash
sudo dmesg | grep -i "remap\|error" | tail -50
# Look for remap creation messages
# Look for "write-ahead remap queued"
```

### 4. Check Device Properties

```bash
# Logical sector size
blockdev --getss /dev/mapper/dm-test-remap
blockdev --getss /dev/loop1
blockdev --getss /dev/loop2
# All should be 512

# Physical block size
blockdev --getpbsz /dev/mapper/dm-test-remap
blockdev --getpbsz /dev/loop1
blockdev --getpbsz /dev/loop2
# All should match
```

---

## Solution: Immediate Workaround

### For WRITE operations that fail

Modify `dm_remap_end_io_v4_real()` to:

```c
if (*error != BLK_STS_OK && bio_data_dir(bio) == WRITE) {
    // For WRITE errors to remappable sectors:
    if (dm_remap_can_remap(device, failed_sector)) {
        // Create remap immediately (synchronously if possible)
        // OR: wait for async remap to complete
        
        // Clear the error - write will go to spare device
        *error = BLK_STS_OK;  // <-- KEY ADDITION
        
        DMR_INFO("Cleared write error on sector %llu - will use spare device",
                 failed_sector);
    }
}
```

---

## Permanent Fix

### Architecture Change Needed

1. **Distinguish READ vs WRITE errors**
   - READ: Keep error (can't fix)
   - WRITE: Clear error if remappable

2. **Synchronous remap on first write error**
   - Or wait for async remap to complete
   - Guarantee remap is ready before I/O returns

3. **Better spare device validation**
   - Test spare device during setup
   - Verify before accepting remaps

4. **Metadata persistence guarantee**
   - Ensure remap metadata written before returning success
   - Crash recovery support

---

## Testing the Theory

```bash
# Create fresh dm-remap
sudo ./setup-dm-remap-test.sh

# Try mkfs.ext4
sudo mkfs.ext4 /dev/mapper/dm-test-remap

# Check if errors appear
sudo dmesg | grep "I/O error"

# Check remap count
sudo dmsetup message dm-test-remap 0 status
```

---

## Summary

**Your Diagnosis**: ✅ **CORRECT**

dm-remap is getting read/write errors on sectors 0 and 2. These are **not being suppressed or remapped transparently**. Instead:

1. ❌ Error is returned to filesystem
2. ❌ Filesystem aborts/fails
3. ✓ (Meanwhile) Remap is being created in background
4. ❌ But too late - filesystem already failed

**The Fix**: dm-remap needs to:
- Distinguish WRITE errors (can be fixed) from READ errors (cannot)
- For WRITE errors: Clear error and redirect to spare device
- Ensure remap is ready before returning success
- Validate spare device can handle remaps before accepting them

This is a **critical bug** for production use with filesystems/pools that don't retry on I/O errors.

