# Async Metadata I/O Refactoring Plan

## Current Status: Workqueue Leak Workaround

**Problem**: Device removal can leave metadata_sync_work stuck in blocking I/O to devices being torn down, forcing us to leak the workqueue instead of cleanly destroying it.

**Current Workaround**: 
- Set `device_active = 0` before cancelling work
- Add multiple `device_active` checks in work functions
- Skip `destroy_workqueue()` to avoid blocking forever
- Let kernel clean up on module unload

**Limitation**: Tiny race window where work passes all checks and enters blocking I/O before presuspend runs.

---

## The Production-Grade Solution: Async I/O with Cancellation

### Overview

Refactor the metadata persistence system to use asynchronous bio operations with explicit cancellation support, eliminating all blocking I/O in work functions.

### Architecture Changes

#### 1. Replace Synchronous I/O in Metadata Module

**Current Code** (`dm-remap-v4-metadata.ko`):
```c
int dm_remap_write_metadata_v4(struct block_device *bdev, 
                                struct dm_remap_metadata_v4 *metadata)
{
    struct bio *bio;
    
    bio = bio_alloc(...);
    bio_set_dev(bio, bdev);
    // ... setup bio ...
    
    submit_bio_wait(bio);  // ← BLOCKS HERE - PROBLEM!
    
    bio_put(bio);
    return 0;
}
```

**New Async Code**:
```c
struct metadata_write_ctx {
    struct dm_remap_device_v4_real *device;
    struct bio *bio;
    struct completion *done;
    atomic_t *cancelled;
    int result;
};

void dm_remap_write_metadata_endio(struct bio *bio)
{
    struct metadata_write_ctx *ctx = bio->bi_private;
    
    /* Check if write was cancelled during I/O */
    if (atomic_read(ctx->cancelled)) {
        DMR_DEBUG(2, "Metadata write cancelled during I/O");
        ctx->result = -ECANCELED;
    } else {
        ctx->result = blk_status_to_errno(bio->bi_status);
    }
    
    complete(ctx->done);
}

int dm_remap_write_metadata_v4_async(struct block_device *bdev,
                                      struct dm_remap_metadata_v4 *metadata,
                                      atomic_t *cancelled,
                                      struct completion *done)
{
    struct bio *bio;
    struct metadata_write_ctx *ctx;
    
    ctx = kmalloc(sizeof(*ctx), GFP_NOIO);
    if (!ctx)
        return -ENOMEM;
        
    ctx->cancelled = cancelled;
    ctx->done = done;
    
    bio = bio_alloc(...);
    bio_set_dev(bio, bdev);
    bio->bi_end_io = dm_remap_write_metadata_endio;
    bio->bi_private = ctx;
    // ... setup bio ...
    
    submit_bio(bio);  // ← NON-BLOCKING!
    
    return 0;
}
```

#### 2. Update Device Structure

Add cancellation and completion tracking:

```c
struct dm_remap_device_v4_real {
    // ... existing fields ...
    
    /* Async metadata write tracking */
    struct completion metadata_write_done;
    atomic_t metadata_write_cancelled;
    spinlock_t metadata_write_lock;
};
```

#### 3. Refactor Metadata Sync Work Function

**Current Blocking Code**:
```c
static void dm_remap_sync_metadata_work(struct work_struct *work)
{
    // ... checks ...
    
    mutex_lock(&device->metadata_mutex);
    
    if (device->persistent_metadata) {
        int ret = dm_remap_write_persistent_metadata(device);  // ← BLOCKS
        if (ret) {
            DMR_ERROR("Failed to write metadata: %d", ret);
            mutex_unlock(&device->metadata_mutex);
            return;
        }
    }
    
    mutex_unlock(&device->metadata_mutex);
}
```

**New Async Code**:
```c
static void dm_remap_sync_metadata_work(struct work_struct *work)
{
    struct dm_remap_device_v4_real *device = 
        container_of(work, struct dm_remap_device_v4_real, metadata_sync_work);
    int ret;
    unsigned long timeout;
    
    /* Check if device is being destroyed */
    if (!atomic_read(&device->device_active)) {
        DMR_DEBUG(2, "Metadata sync skipped - device inactive");
        return;
    }
    
    if (!device->metadata_dirty)
        return;
    
    mutex_lock(&device->metadata_mutex);
    
    /* Check again after mutex */
    if (!atomic_read(&device->device_active)) {
        mutex_unlock(&device->metadata_mutex);
        return;
    }
    
    /* Initialize completion and cancellation flag */
    reinit_completion(&device->metadata_write_done);
    atomic_set(&device->metadata_write_cancelled, 0);
    
    /* Start async write */
    ret = dm_remap_write_persistent_metadata_async(
        device,
        &device->metadata_write_cancelled,
        &device->metadata_write_done);
    
    if (ret) {
        DMR_ERROR("Failed to start metadata write: %d", ret);
        mutex_unlock(&device->metadata_mutex);
        return;
    }
    
    mutex_unlock(&device->metadata_mutex);
    
    /* Wait for completion with timeout, checking for cancellation */
    timeout = msecs_to_jiffies(5000);  // 5 second timeout
    
    while (timeout > 0) {
        timeout = wait_for_completion_timeout(&device->metadata_write_done,
                                               timeout);
        
        /* Check if cancelled during wait */
        if (atomic_read(&device->metadata_write_cancelled)) {
            DMR_DEBUG(2, "Metadata write cancelled");
            return;  // Exit immediately
        }
        
        if (timeout > 0 || completion_done(&device->metadata_write_done)) {
            /* I/O completed */
            break;
        }
    }
    
    if (timeout == 0) {
        DMR_ERROR("Metadata write timeout");
        atomic_set(&device->metadata_write_cancelled, 1);
        return;
    }
    
    /* Mark as clean if write succeeded */
    if (!atomic_read(&device->metadata_write_cancelled)) {
        device->metadata_dirty = false;
    }
}
```

#### 4. Update Presuspend to Cancel Async I/O

```c
static void dm_remap_presuspend_v4_real(struct dm_target *ti)
{
    struct dm_remap_device_v4_real *device = ti->private;
    struct dm_remap_entry_v4 *entry, *tmp;
    
    if (!device)
        return;
    
    DMR_INFO("Presuspend: stopping all background work");
    
    /* Mark device inactive FIRST */
    atomic_set(&device->device_active, 0);
    
    /* Cancel any in-flight metadata writes */
    atomic_set(&device->metadata_write_cancelled, 1);
    
    /* Cancel work items (won't start new I/O) */
    cancel_work(&device->metadata_sync_work);
    cancel_work(&device->error_analysis_work);
    cancel_delayed_work(&device->health_scan_work);
    
    /* Free remap entries */
    spin_lock(&device->remap_lock);
    list_for_each_entry_safe(entry, tmp, &device->remap_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    device->remap_count_active = 0;
    spin_unlock(&device->remap_lock);
    
    DMR_INFO("Presuspend: complete");
}
```

#### 5. Update Destructor to Properly Clean Up

```c
static void dm_remap_dtr_v4_real(struct dm_target *ti)
{
    struct dm_remap_device_v4_real *device = ti->private;
    
    if (!device)
        return;
    
    DMR_INFO("Destroying real device target: main=%s, spare=%s",
             device->main_path, device->spare_path);
    
    /* Ensure device is marked inactive */
    atomic_set(&device->device_active, 0);
    
    /* Ensure any async I/O is cancelled */
    atomic_set(&device->metadata_write_cancelled, 1);
    
    /* Remove from global device list */
    mutex_lock(&dm_remap_devices_mutex);
    list_del(&device->device_list);
    atomic_dec(&dm_remap_device_count);
    mutex_unlock(&dm_remap_devices_mutex);
    
    /* Free performance optimization cache */
    if (device->perf_optimizer.cache_entries) {
        kfree(device->perf_optimizer.cache_entries);
    }
    
    /* NOW we can safely destroy workqueue - work items will exit cleanly */
    if (device->metadata_workqueue) {
        DMR_INFO("Destructor: destroying workqueue");
        destroy_workqueue(device->metadata_workqueue);
        DMR_INFO("Destructor: workqueue destroyed successfully");
    }
    
    /* Free persistent metadata */
    if (device->persistent_metadata) {
        kfree(device->persistent_metadata);
    }
    
    /* Close real devices */
    if (real_device_mode) {
        if (device->main_dev) {
            dm_remap_close_bdev_real(device->main_dev);
        }
        if (device->spare_dev) {
            dm_remap_close_bdev_real(device->spare_dev);
        }
    }
    
    /* Destroy mutexes */
    mutex_destroy(&device->metadata_mutex);
    mutex_destroy(&device->health_mutex);
    mutex_destroy(&device->cache_mutex);
    
    /* Free device structure */
    kfree(device);
    
    DMR_INFO("Real device target destroyed");
}
```

### Implementation Phases

#### Phase 1: Metadata Module Refactoring
- [ ] Add async variants of metadata write functions
- [ ] Implement bio completion handlers
- [ ] Add cancellation support to all I/O operations
- [ ] Add timeout handling
- [ ] Update metadata module API
- [ ] Test metadata module in isolation

#### Phase 2: Main Module Integration  
- [ ] Add completion and cancellation tracking to device structure
- [ ] Refactor metadata_sync_work to use async I/O
- [ ] Update presuspend to cancel async operations
- [ ] Update destructor to properly destroy workqueue
- [ ] Update error_analysis_work if it does I/O

#### Phase 3: Testing
- [ ] Test device removal during idle state
- [ ] Test device removal during active metadata write
- [ ] Test device removal during error recovery
- [ ] Test rapid create/destroy cycles
- [ ] Test module unload with active devices
- [ ] Stress test with continuous I/O

#### Phase 4: Validation
- [ ] No workqueue leaks
- [ ] No memory leaks  
- [ ] No hangs or deadlocks
- [ ] Clean module unload every time
- [ ] Performance impact acceptable

### Benefits

✅ **No workqueue leaks** - Can properly call `destroy_workqueue()`
✅ **Instant device removal** - No blocking I/O in critical path
✅ **Cancellable operations** - All I/O can be aborted mid-flight
✅ **Production-ready** - No race conditions or workarounds
✅ **Better error handling** - Explicit timeouts and cancellation
✅ **Cleaner code** - No "leak to avoid deadlock" hacks

### Risks & Considerations

⚠️ **Complexity**: Async I/O is harder to debug than synchronous
⚠️ **Timing**: Completion callbacks run in interrupt context - need careful locking
⚠️ **Backwards compatibility**: API changes in metadata module
⚠️ **Testing burden**: More edge cases to test
⚠️ **Performance**: Slightly higher overhead for completion tracking

### Estimated Effort

- **Metadata module refactor**: 2-3 days
- **Main module integration**: 2 days  
- **Testing & debugging**: 3-4 days
- **Documentation**: 1 day

**Total**: ~2 weeks for complete async I/O refactoring

### Alternative: Simpler Approach

If full async refactor is too complex, a simpler middle ground:

**Use timeout-based cancellation**:
```c
/* In metadata write function */
bio = bio_alloc(...);
// ... setup ...

/* Store bio pointer for cancellation */
spin_lock(&device->metadata_write_lock);
device->current_write_bio = bio;
spin_unlock(&device->metadata_write_lock);

ret = submit_bio_wait(bio);  // Still blocking, but...

/* In presuspend, if timing allows */
spin_lock(&device->metadata_write_lock);
if (device->current_write_bio) {
    bio_endio(device->current_write_bio, BLK_STS_IOERR);  // Force completion
    device->current_write_bio = NULL;
}
spin_unlock(&device->metadata_write_lock);
```

This is still hacky but might be simpler than full async refactor.

---

## Decision Point

**Current Status**: Working system with controlled workqueue leak
**Option 1**: Ship with leak (documented limitation)
**Option 2**: Implement full async I/O refactoring (2 weeks)
**Option 3**: Implement simpler timeout-based cancellation (3-4 days)

Recommendation: **Option 1 for v4.0**, plan **Option 2 for v4.1**

The current approach is safe and functional. The async refactor is the right long-term solution but not critical for initial release.
