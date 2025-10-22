# dm-remap Crash Debug State - October 20, 2025

## Problem Summary
VM freezes/crashes immediately when removing a dm-remap device that has remaps present.
- Simple device (no remaps): removal works perfectly
- Device with ONE remap: immediate VM freeze on removal

## Timeline of Fixes Attempted

### 1. Initial Theory: Crash in destructor during metadata write
**Result:** Destructor never reached - crash happens before it's called

### 2. Added presuspend hook to clear remaps early
**Result:** VM still froze

### 3. Added spinlock protection to remap list iteration
**Changes:**
- `dm_remap_find_remap_entry()` - now holds spinlock during iteration
- `dm_remap_sync_persistent_metadata()` - now holds spinlock during iteration
**Result:** VM still froze (just tested)

### 4. Changed shutdown I/O handling
**Change:** Return `DM_MAPIO_KILL` instead of `DM_MAPIO_SUBMITTED` when shutdown flag is set
**Result:** Unknown - may not have been compiled/tested yet

### 5. Current Approach: Disable presuspend remap clearing
**Theory:** Maybe presuspend clearing itself is causing the race condition
**Change:** Commented out remap clearing in presuspend, let destructor handle it
**Module rebuilt:** Yes, just now
**Status:** READY TO TEST - if this crashes, we know presuspend clearing wasn't the issue

## Current Code State

### Presuspend Hook (dm_remap_presuspend_v4_real)
- Sets shutdown flag: `atomic_set(&device->shutdown_in_progress, 1)`
- Remap clearing: **DISABLED** (commented out)
- Logging: Extensive KERN_CRIT messages with ======== borders

### Map Function (dm_remap_map_v4_real)
- Checks shutdown flag at start
- If shutdown: calls `bio_io_error(bio)` and returns `DM_MAPIO_KILL`
- Has spinlock protection when searching remap list

### Destructor (dm_remap_dtr_v4_real)
- Should free remap entries (currently may be skipped - needs verification)
- Extensive logging throughout

### Find Remap Function (dm_remap_find_remap_entry)
- **HAS SPINLOCK PROTECTION** around list iteration
- Returns found entry or NULL

## Files Modified
- src/dm-remap-v4-real-main.c
  - Line ~1280-1320: dm_remap_find_remap_entry() - added spinlock
  - Line ~420-460: dm_remap_sync_persistent_metadata() - added spinlock  
  - Line ~1380: dm_remap_map_v4_real() shutdown check - returns DM_MAPIO_KILL
  - Line ~2184-2230: dm_remap_presuspend_v4_real() - remap clearing DISABLED
  - Line ~1830: dm_remap_dtr_v4_real() - remap freeing status unclear

## Test Configuration
- Loop devices: Will use auto-allocated (loop17, loop18, etc)
- Test script: tests/test_one_remap_remove.sh
- Module: dm-remap-v4-real.ko with real_device_mode=1
- Creates ONE remap at sector 1000, then attempts removal

## Known Issues
1. **Cannot see crash logs** - VM freeze requires hard reboot, kernel log is lost
2. **Working blind** - No way to see what actually causes the freeze
3. **Spinlock fixes didn't work** - List protection alone is not sufficient

## Next Steps After This Test
If still crashes:
1. Try netconsole to capture logs to another machine
2. Try serial console logging to Windows host
3. Consider radically different approach (don't use remaps at all during testing)
4. Add printk at EVERY line in the I/O path and presuspend to narrow down exact freeze location

## How to Resume After VM Crash
1. Reboot VM
2. Read this file: `/home/christian/kernel_dev/dm-remap/CRASH_DEBUG_STATE.md`
3. Check `/home/christian/kernel_dev/dm-remap/TEST_LOG.md` for test history
4. Last module build timestamp: Check with `ls -lh src/dm-remap-v4-real.ko`
5. Rebuild if needed: `cd /home/christian/kernel_dev/dm-remap && make`
6. Run test: `cd tests && sudo ./test_one_remap_remove.sh`

## Critical Code Snippets

### Shutdown Check in Map Function (should be around line 1380)
```c
if (atomic_read(&device->shutdown_in_progress)) {
    bio_io_error(bio);
    return DM_MAPIO_KILL;  // Changed from DM_MAPIO_SUBMITTED
}
```

### Presuspend Hook (around line 2184)
```c
static void dm_remap_presuspend_v4_real(struct dm_target *ti)
{
    // Sets shutdown flag
    atomic_set(&device->shutdown_in_progress, 1);
    
    // Remap clearing: COMMENTED OUT to test if that's causing the issue
    // spin_lock(&device->remap_lock);
    // list_for_each_entry_safe(entry, tmp, &device->remap_list, list) {
    //     list_del(&entry->list);
    //     kfree(entry);
    // }
    // spin_unlock(&device->remap_lock);
}
```

### Find Remap with Spinlock (around line 1280)
```c
static struct dm_remap_entry_v4* dm_remap_find_remap_entry(
    struct dm_remap_device_v4_real *device, sector_t sector)
{
    struct dm_remap_entry_v4 *entry;
    
    spin_lock(&device->remap_lock);
    list_for_each_entry(entry, &device->remap_list, list) {
        if (entry->original_sector == sector) {
            spin_unlock(&device->remap_lock);
            return entry;
        }
    }
    spin_unlock(&device->remap_lock);
    return NULL;
}
```

## Module Info
- Module name: dm-remap-v4-real
- Target name: dm-remap-v4
- Version: 4.0.0
- Stats module: NOT LOADED (disabled, causes symbol errors)

## Important: Changes Since Last Successful State
Last successful state: Device with NO remaps could be created and removed cleanly.
Breaking change: Adding even ONE remap causes immediate freeze on removal.
Root cause: Unknown - possibly bio handling, list corruption, or device-mapper interaction issue.
