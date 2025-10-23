# v4.1 Development - Reboot Required #4

**Date:** October 23, 2025  
**Branch:** v4.1-async-io  
**Reason:** drain_workqueue() hangs because async I/O wait never completes

## What happened - THREE iterations of deadlock fixes

### Iteration 1: Workqueue not cancelled properly
- Used `cancel_work()` instead of `cancel_work_sync()`
- Destructor hung at `destroy_workqueue()`

### Iteration 2: Deadlock in cancellation
- Work function held mutex while waiting for async I/O
- Presuspend called `dm_remap_cancel_metadata_write()` which ALSO waits
- Result: circular wait deadlock

### Iteration 3: cancel_work_sync() hangs on queued work  
- Presuspend used `cancel_work_sync()` which waits for work
- But work was queued and NOT running yet
- `cancel_work_sync()` blocks waiting for work that will never run (device_active=0)
- **DEADLOCK #2!**

### Iteration 4: drain_workqueue() hangs - async I/O wait never completes
- Presuspend only set cancel flag: `atomic_set(&ctx.write_cancelled, 1)`
- Work function waiting at: `wait_for_completion_timeout(&ctx.all_copies_done)`
- Completion NEVER triggered, so work blocks forever
- `drain_workqueue()` hangs waiting for work that will never exit
- **DEADLOCK #3!**

## Final Fix - complete_all() approach

The work function waits for async I/O, so presuspend must complete that wait:

### Presuspend (cancellation)
```c
// Set device inactive
atomic_set(&device->device_active, 0);

// Cancel async I/O AND complete the wait
if (atomic_read(&device->metadata_write_in_progress)) {
    atomic_set(&device->async_metadata_ctx.write_cancelled, 1);
    complete_all(&device->async_metadata_ctx.all_copies_done);  // ← KEY FIX!
}

// Cancel work (non-blocking)
cancel_work(&device->metadata_sync_work);
```

### Work function (checks after wait)
```c
// Wait for async I/O (now unblocked by presuspend)
ret = dm_remap_wait_metadata_write(&ctx, 5000);

// Check device_active after wait - exit if cancelled
if (!atomic_read(&device->device_active)) {
    return;  // Exit cleanly
}
```

### Destructor (blocking cleanup)
```c
// Drain all pending/running work (now completes!)
drain_workqueue(device->metadata_workqueue);

// Destroy the workqueue
destroy_workqueue(device->metadata_workqueue);
```

### Why this works
1. **Presuspend completes the wait** - work function unblocks immediately
2. **Work checks device_active** - sees it's 0 and exits cleanly
3. **No deadlocks** - work completes quickly, drain_workqueue() succeeds
4. **Safe cancellation** - completion can be called multiple times (complete_all)

## Commits ready to test

- `41ff167` - v4.1: Implement async metadata I/O API
- `3489c2d` - v4.1: Complete async I/O integration  
- `f2af120` - v4.1: Fix presuspend to use cancel_work_sync()
- `f88f116` - v4.1: Fix deadlock - release mutex before waiting
- `eabb808` - v4.1: Back to cancel_work() + drain_workqueue()
- `fcdcf54` - v4.1: Fix drain_workqueue hang - complete async I/O on cancel ← **LATEST**

**Status:** complete_all() approach implemented, ready for testing after reboot #4 ✓
