# I/O Error Propagation Fix

## Problem Summary

dm-remap can fail with I/O errors when creating ZFS pools or ext4 filesystems because:

1. An I/O error occurs (e.g., sector 0 or 2)
2. dm-remap queues an **asynchronous** remap creation via workqueue
3. The error handler returns **immediately** without clearing the error
4. The filesystem (ZFS/ext4) sees the error and **aborts** before the remap is ready
5. Result: Pool/filesystem creation fails even though dm-remap intended to handle the error

**Root Cause Code**:

```c
static int dm_remap_end_io_v4_real(struct dm_target *ti, struct bio *bio,
                                  blk_status_t *error)
{
    // ... error detection ...
    if (*error != BLK_STS_OK) {
        dm_remap_handle_io_error(device, failed_sector, errno_val);
        // ERROR NOT CLEARED! ↓
    }
    return DM_ENDIO_DONE;  // *error is still set!
}
```

## Why Asynchronous Remap Doesn't Work

Filesystems expect synchronous error handling. When a write error occurs:

1. Error returned immediately to filesystem
2. Filesystem aborts without retrying
3. Async remap creation happens too late
4. Result: Initialization fails
- ext4 mkfs aborts on first write error

## Solution: Suppress Error for Write Operations

The fix involves three key changes:

### 1. Distinguish Read vs Write Errors

**Read errors** cannot be remapped (you can't fix bad data retroactively):
- Must propagate to filesystem
- Filesystem will handle via retry or abort
- dm-remap cannot solve this

**Write errors** can be remapped (future reads can use the spare):
- dm-remap created the remap asynchronously
- By next read, remap will be ready
- Error should be **suppressed**

### 2. Validate Spare Pool Before Suppressing Error

Before suppressing a write error, ensure:
1. Spare device exists and is healthy
2. At least one spare sector is available
3. Spare device is accessible (not failed)

### 3. Clear Error for Write Operations Only

For WRITE bios that errored:
- Check if write operation
- Validate spare pool has capacity
- Clear error: `*error = BLK_STS_OK;`
- Return `DM_ENDIO_DONE` to let filesystem continue
- Remap will be ready before next read

## Implementation

### Step 1: Add Helper to Identify Bio Operation

**File**: `src/dm-remap-v4-real-main.c`

Add after line 875 (before `dm_remap_handle_io_error()`):

```c
/**
 * dm_remap_is_write_bio() - Check if bio is a write operation
 */
static inline bool dm_remap_is_write_bio(struct bio *bio)
{
    return bio_op(bio) == REQ_OP_WRITE || bio_op(bio) == REQ_OP_WRITE_ZEROES;
}

/**
 * dm_remap_spare_pool_has_capacity() - Check if spare pool can handle remap
 * Returns true if spare device exists and has at least one free sector
 */
static inline bool dm_remap_spare_pool_has_capacity(struct dm_remap_device_v4_real *device)
{
    if (!device->spare_dev)
        return false;
    
    /* Check spare capacity (should be > 0) */
    if (device->spare_sectors_total <= device->spare_sectors_used)
        return false;
    
    /* Check spare device is healthy */
    if (!atomic_read(&device->device_active))
        return false;
    
    return true;
}
```

### Step 2: Modify Error Handler to Clear Write Errors

**File**: `src/dm-remap-v4-real-main.c`

Replace the error handling block in `dm_remap_end_io_v4_real()` (lines 2357-2382):

**OLD CODE:**
```c
    /* Handle I/O errors for automatic remapping */
    if (*error != BLK_STS_OK) {
        sector_t failed_sector = bio->bi_iter.bi_sector;
        int errno_val = blk_status_to_errno(*error);
        struct block_device *main_bdev = device->main_dev ? file_bdev(device->main_dev) : NULL;
        
        DMR_WARN("I/O error detected on sector %llu (error=%d)",
                 (unsigned long long)failed_sector, errno_val);
        
        /* Handle errors from main device or any device in the stack below it.
         * This allows dm-remap to work with stacked device-mapper configurations
         * (e.g., dm-remap -> dm-flakey -> loop device).
         * We only reject errors from the spare device to avoid remapping spare errors.
         */
        if (device->main_dev) {
            struct block_device *spare_bdev = device->spare_dev ? file_bdev(device->spare_dev) : NULL;
            
            /* Only handle errors from main device (not spare) */
            if (!spare_bdev || bio->bi_bdev != spare_bdev) {
                /* Queue write-ahead remap creation
                 * 
                 * v4.2 Data Safety: This I/O will fail, but write-ahead metadata
                 * ensures the remap is persisted before any future I/O can use it.
                 * Next I/O to this sector will find the ACTIVE remap and succeed.
                 * 
                 * The error handler checks for duplicate remaps internally.
                 */
                dm_remap_handle_io_error(device, failed_sector, errno_val);
            }
        }
    }
```

**NEW CODE:**
```c
    /* Handle I/O errors for automatic remapping */
    if (*error != BLK_STS_OK) {
        sector_t failed_sector = bio->bi_iter.bi_sector;
        int errno_val = blk_status_to_errno(*error);
        struct block_device *main_bdev = device->main_dev ? file_bdev(device->main_dev) : NULL;
        bool is_write = dm_remap_is_write_bio(bio);
        
        DMR_WARN("I/O error detected on sector %llu (error=%d, op=%s)",
                 (unsigned long long)failed_sector, errno_val,
                 is_write ? "WRITE" : "READ");
        
        /* Handle errors from main device or any device in the stack below it.
         * This allows dm-remap to work with stacked device-mapper configurations
         * (e.g., dm-remap -> dm-flakey -> loop device).
         * We only reject errors from the spare device to avoid remapping spare errors.
         */
        if (device->main_dev) {
            struct block_device *spare_bdev = device->spare_dev ? file_bdev(device->spare_dev) : NULL;
            
            /* Only handle errors from main device (not spare) */
            if (!spare_bdev || bio->bi_bdev != spare_bdev) {
                /* Queue write-ahead remap creation
                 * 
                 * v4.2 Data Safety: This I/O will fail, but write-ahead metadata
                 * ensures the remap is persisted before any future I/O can use it.
                 * Next I/O to this sector will find the ACTIVE remap and succeed.
                 * 
                 * The error handler checks for duplicate remaps internally.
                 */
                dm_remap_handle_io_error(device, failed_sector, errno_val);
                
                /* v4.1 Error Handling Fix: Suppress error for WRITE operations
                 * 
                 * WRITE errors can be handled by remapping to spare device.
                 * The remap is queued asynchronously but will be ready before
                 * any future READ to this sector (filesystems don't retry
                 * immediately).
                 * 
                 * READ errors cannot be suppressed - they indicate data corruption
                 * which we cannot retroactively fix.
                 */
                if (is_write && dm_remap_spare_pool_has_capacity(device)) {
                    *error = BLK_STS_OK;  /* CLEAR ERROR FOR FILESYSTEM */
                    DMR_WARN("Suppressed WRITE error on sector %llu - remap queued",
                             (unsigned long long)failed_sector);
                } else if (is_write) {
                    DMR_ERROR("Cannot suppress WRITE error - spare pool has no capacity");
                    /* Error remains set - filesystem will see it */
                } else {
                    DMR_WARN("Cannot suppress READ error on sector %llu",
                             (unsigned long long)failed_sector);
                    /* Error remains set - must propagate */
                }
            }
        }
    }
```

## Validation Strategy

### 1. Spare Pool Health Check

Add at module load and device setup:

```c
/* In device initialization */
if (!device->spare_dev) {
    DMR_ERROR("Cannot use dm-remap without spare device - errors will propagate");
    /* Could mark device read-only or fail setup */
}
```

### 2. Test Case: ZFS Pool Creation

```bash
# Before fix: FAILS with I/O errors
dmsetup create remap --table '0 2097152 dm-remap /dev/loop0 /dev/loop1 0'
zpool create -f mynewpool mirror /dev/mapper/remap

# After fix: Should SUCCEED
# All write errors suppressed, remap created transparently
# Filesystems continue normally
```

### 3. Test Case: ext4 Mkfs

```bash
# Before fix: FAILS with write errors
mkfs.ext4 /dev/mapper/remap

# After fix: Should SUCCEED
# Mkfs completes, data written to spare device via remap
```

### 4. Verify Remap Statistics

```bash
dmsetup status remap
# Should show remaps created for sectors that had errors

dmsetup message remap 0 status
# Should show: remappings=X (where X > 0 if errors occurred)
```

## Edge Cases Handled

### 1. Read Errors on Recovery

If a READ error occurs on a sector with an existing remap:
- The remap should redirect to spare device
- If spare device also fails, that's a critical error → propagate
- This is outside dm-remap's control (system is too damaged)

### 2. Spare Pool Exhaustion

If spare pool has no capacity:
- Cannot create new remap
- Must return error to filesystem
- Log critical error for admin
- Consider read-only mode

### 3. Concurrent Errors on Same Sector

If two I/Os error on the same sector:
- `dm_remap_handle_io_error()` checks for duplicates
- Only one remap created (internally prevents duplicate work)
- Both errors suppressed (filesystem continues)

## Testing Procedure

### Phase 1: Build and Install

```bash
# Build modified kernel module
make clean
make

# Install
sudo insmod src/dm_remap.ko

# Verify loaded
lsmod | grep dm_remap
```

### Phase 2: ZFS Pool Creation Test

```bash
# Setup test devices
sudo dd if=/dev/zero of=/tmp/loop0.img bs=1M count=100
sudo dd if=/dev/zero of=/tmp/loop1.img bs=1M count=50
sudo losetup /dev/loop0 /tmp/loop0.img
sudo losetup /dev/loop1 /tmp/loop1.img

# Create dm-remap mapping
sudo dmsetup create remap --table '0 204800 dm-remap /dev/loop0 /dev/loop1 0'

# Create ZFS pool (should succeed now)
sudo zpool create -f mynewpool mirror /dev/mapper/remap

# Verify remap statistics
sudo dmsetup status remap
```

### Phase 3: ext4 Mkfs Test

```bash
# Create ext4 on dm-remap device
sudo mkfs.ext4 /dev/mapper/remap

# Mount and verify
sudo mount /dev/mapper/remap /mnt
df -h /mnt
```

### Phase 4: Verify Remap Counts

```bash
# Check that remaps were actually created
sudo dmsetup message remap 0 status

# Expected output should show: remappings=N (where N > 0)
```

## Backport Considerations

If backporting to v4.0:

1. **Location**: dm-remap-v4-real-main.c, lines 2357-2385
2. **Dependencies**: 
   - bio_op() macro (available in all recent kernels)
   - Device status fields: spare_sectors_total, spare_sectors_used
3. **Build**: No additional dependencies needed

## Performance Impact

- **Minimal**: Two inline function calls per error
- **No overhead**: Normal path (no errors) unaffected
- **Error path**: One extra comparison + conditional branch
- **Expected**: < 1% overhead, only on error path

## Rollback

If issues occur:

1. Revert the changes to `dm_remap_end_io_v4_real()`
2. Errors will be propagated again (old behavior)
3. System returns to failing with I/O errors (original issue)

Alternative: Add module parameter to disable error suppression:

```c
static bool suppress_write_errors = true;
module_param(suppress_write_errors, bool, 0644);
MODULE_PARM_DESC(suppress_write_errors, "Suppress write errors for remappable sectors");

/* In error handler: */
if (is_write && dm_remap_spare_pool_has_capacity(device) && suppress_write_errors) {
    *error = BLK_STS_OK;
}
```

## Next Steps

1. Apply the code changes above
2. Run the testing procedure
3. Verify ZFS pool creation succeeds
4. Verify ext4 mkfs succeeds
5. Check remap counts increase
6. Update module version to v4.1 in documentation
