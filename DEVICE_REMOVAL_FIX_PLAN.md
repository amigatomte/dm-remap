# Device Removal Fix Plan
**Date:** October 22, 2025  
**Status:** Device removal hangs - need to fix properly

## The Problem

Device removal hangs when remaps are present. This has existed since the beginning.

**Symptom:** `dmsetup remove` hangs indefinitely, process stuck in 'D' state

**Last known state before hang:**
```
dm-remap v4.0 real: Destroying real device target: main=/dev/mapper/linear_main, spare=/dev/loop19
[HUNG]
```

## Investigation Plan (After Reboot)

### Step 1: Compare with Working dm-targets

Look at how dm-cache and dm-thin handle device removal properly:

```bash
# Extract dm-cache teardown sequence
cd /usr/src/linux-source-*/drivers/md
grep -A 50 "dm_cache_dtr" dm-cache-target.c
grep -A 30 "dm_cache_presuspend" dm-cache-target.c

# Extract dm-thin teardown sequence  
grep -A 50 "pool_dtr" dm-thin.c
grep -A 30 "pool_presuspend" dm-thin.c
```

**Look for:**
- How they cancel workqueues
- How they flush pending work
- How they close device references
- Order of operations in teardown

### Step 2: Check Our Current Teardown Code

Examine our presuspend and destructor:

```bash
# Find our teardown functions
grep -A 100 "dm_remap_presuspend_v4_real" src/dm-remap-v4-real-main.c
grep -A 100 "dm_remap_dtr_v4_real" src/dm-remap-v4-real-main.c
```

**Questions to answer:**
1. Do we have a presuspend hook? (We should!)
2. Do we cancel/flush workqueues before freeing resources?
3. Do we wait for in-flight work items to complete?
4. Do we close device files in the right order?

### Step 3: Likely Root Causes

Based on common device-mapper mistakes:

**Cause 1: Workqueue Not Cancelled**
```c
// WRONG - workqueue still running when we free device
static void dm_remap_dtr(...)
{
    kfree(device);  // BAD - workqueue may still reference this!
}

// RIGHT - cancel workqueue first
static void dm_remap_dtr(...)
{
    if (device->metadata_wq) {
        cancel_work_sync(&device->metadata_sync_work);
        cancel_work_sync(&device->error_analysis_work);
        destroy_workqueue(device->metadata_wq);
    }
    kfree(device);
}
```

**Cause 2: Work Items Not Flushed**
```c
// WRONG - work may still be queued
cancel_work_sync(&device->work);

// RIGHT - flush then cancel
flush_work(&device->work);
cancel_work_sync(&device->work);
```

**Cause 3: Device Files Not Closed**
```c
// WRONG - close files after freeing list
list_for_each_entry_safe(...) { kfree(entry); }
fput(device->main_dev);  // BAD - may still have pending I/O

// RIGHT - ensure no I/O pending before closing
// (device-mapper should handle this via presuspend)
fput(device->main_dev);
fput(device->spare_dev);
```

### Step 4: Add Diagnostic Logging

Add logging to find EXACT hang point:

```c
static void dm_remap_presuspend_v4_real(struct dm_target *ti)
{
    printk(KERN_CRIT "dm-remap: PRESUSPEND ENTRY\n");
    
    atomic_set(&device->shutdown_in_progress, 1);
    printk(KERN_CRIT "dm-remap: shutdown flag set\n");
    
    // Cancel workqueue items
    printk(KERN_CRIT "dm-remap: cancelling work items\n");
    cancel_work_sync(&device->metadata_sync_work);
    printk(KERN_CRIT "dm-remap: metadata_sync_work cancelled\n");
    
    cancel_work_sync(&device->error_analysis_work);
    printk(KERN_CRIT "dm-remap: error_analysis_work cancelled\n");
    
    // Flush workqueue
    if (device->metadata_wq) {
        printk(KERN_CRIT "dm-remap: flushing workqueue\n");
        flush_workqueue(device->metadata_wq);
        printk(KERN_CRIT "dm-remap: workqueue flushed\n");
    }
    
    printk(KERN_CRIT "dm-remap: PRESUSPEND EXIT\n");
}

static void dm_remap_dtr_v4_real(struct dm_target *ti)
{
    printk(KERN_CRIT "dm-remap: DESTRUCTOR ENTRY\n");
    
    // Destroy workqueue
    if (device->metadata_wq) {
        printk(KERN_CRIT "dm-remap: destroying workqueue\n");
        destroy_workqueue(device->metadata_wq);
        printk(KERN_CRIT "dm-remap: workqueue destroyed\n");
    }
    
    // Free remaps
    printk(KERN_CRIT "dm-remap: freeing remaps\n");
    list_for_each_entry_safe(entry, tmp, &device->remap_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    printk(KERN_CRIT "dm-remap: remaps freed\n");
    
    // Close devices
    printk(KERN_CRIT "dm-remap: closing devices\n");
    if (device->main_dev)
        fput(device->main_dev);
    printk(KERN_CRIT "dm-remap: main device closed\n");
    
    if (device->spare_dev)
        fput(device->spare_dev);
    printk(KERN_CRIT "dm-remap: spare device closed\n");
    
    printk(KERN_CRIT "dm-remap: freeing device structure\n");
    kfree(device);
    
    printk(KERN_CRIT "dm-remap: DESTRUCTOR EXIT\n");
}
```

**The last message printed before hang = the exact hang point!**

### Step 5: Expected Proper Sequence

Based on working dm-targets:

```
1. device-mapper calls presuspend()
   → Set shutdown flag
   → Wait for in-flight I/O to drain
   → Cancel all work items (cancel_work_sync)
   → Flush workqueue
   → Free remaps (now safe - no work can access them)

2. device-mapper calls postsuspend()
   → Optional cleanup

3. device-mapper calls destructor()
   → Destroy workqueue (already flushed/cancelled)
   → Close device files (no I/O pending)
   → Free device structure
   → DONE
```

## Implementation Checklist

After reboot, implement proper teardown:

- [ ] Check current presuspend/destructor code
- [ ] Compare with dm-cache teardown sequence
- [ ] Add presuspend hook if missing
- [ ] Add cancel_work_sync() for all work items
- [ ] Add flush_workqueue() before destroying
- [ ] Add destroy_workqueue() in destructor
- [ ] Test with diagnostic logging
- [ ] Verify device removal works
- [ ] Remove excessive logging once working

## Testing After Fix

```bash
# Simple test
cd /home/christian/kernel_dev/dm-remap
sudo tests/test_v4.0.5_baseline.sh

# Watch for:
# - All PRESUSPEND log messages appear
# - All DESTRUCTOR log messages appear  
# - dmsetup remove completes
# - No hang!
```

## Reference: dm-cache Teardown Pattern

```c
// From dm-cache-target.c (working example)

static void cache_presuspend(struct dm_target *ti)
{
    struct cache *cache = ti->private;
    
    cancel_delayed_work_sync(&cache->waker);
    // Wait for I/O to drain
}

static void cache_dtr(struct dm_target *ti)
{
    struct cache *cache = ti->private;
    
    if (cache->prison)
        dm_bio_prison_destroy(cache->prison);
        
    if (cache->wq)
        destroy_workqueue(cache->wq);
        
    // Close devices, free memory
    kfree(cache);
}
```

**Key insight:** Work items cancelled in presuspend, workqueue destroyed in destructor.

## Expected Outcome

After fixing:
- ✅ Device removal completes in < 1 second
- ✅ No hung processes
- ✅ Clean dmesg output
- ✅ Module can be unloaded
- ✅ No kernel warnings

---

**Start here after reboot!**
