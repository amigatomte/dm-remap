# v4.1 Development - Reboot Required #2

**Date:** October 22, 2025  
**Branch:** v4.1-async-io  
**Reason:** Fixed deadlock issues, need clean reboot to test

## What happened - Multiple deadlock issues found and fixed

### Iteration 1: Workqueue not cancelled
- Used `cancel_work()` instead of `cancel_work_sync()`
- Destructor hung at `destroy_workqueue()`

### Iteration 2: Deadlock on cancel
- Work function held mutex while waiting for async I/O
- Presuspend called `dm_remap_cancel_metadata_write()` which ALSO waits
- Result: presuspend waits for work, work waits for cancel, cancel waits for completion
- **DEADLOCK!**

## Final Fix - Three critical changes

### 1. Release mutex before waiting (in work function)
```c
// Submit async I/O
dm_remap_write_metadata_v4_async(...);

// CRITICAL: Release mutex BEFORE waiting
mutex_unlock(&device->metadata_mutex);

// Wait for async I/O (cancellable)
dm_remap_wait_metadata_write(...);

// Cleanup and re-acquire
mutex_lock(&device->metadata_mutex);
```

### 2. Just signal cancellation (in presuspend)
```c
// DON'T call dm_remap_cancel_metadata_write() - it waits!
// Just set the atomic flag directly:
atomic_set(&device->async_metadata_ctx.write_cancelled, 1);
```

### 3. Use cancel_work_sync (in presuspend)
```c
// Now safe because work can exit from wait
cancel_work_sync(&device->metadata_sync_work);
```

## After reboot

Run the test again:
```bash
cd /home/christian/kernel_dev/dm-remap
sudo bash tests/test_v4.0.5_baseline.sh
```

**Expected result:** Clean device removal, no workqueue leak, destructor completes immediately.

## Commits ready to test

- `41ff167` - v4.1: Implement async metadata I/O API
- `3489c2d` - v4.1: Complete async I/O integration  
- `f2af120` - v4.1: Fix presuspend to use cancel_work_sync()
- `f88f116` - v4.1: Fix deadlock - release mutex before waiting ← **LATEST**

**Status:** Deadlock fixes applied, ready for testing after reboot ✓

## How the fix works

1. Work function submits async I/O (non-blocking)
2. Work releases mutex and waits for completion
3. Device removal triggers presuspend:
   - Sets device_active=0
   - Sets write_cancelled=1 (just the flag, no waiting!)
4. In-flight bios complete (see cancel flag, exit quickly)
5. Work function's wait returns
6. cancel_work_sync() succeeds
7. Workqueue destroyed cleanly!
