# v4.1 Development - Reboot Required #3

**Date:** October 22, 2025  
**Branch:** v4.1-async-io  
**Reason:** cancel_work_sync() deadlock, switched to drain_workqueue() approach

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
- **NEW DEADLOCK!**

## Final Fix - drain_workqueue() approach

Instead of trying to cancel work synchronously, let the destructor handle it:

### Presuspend (non-blocking)
```c
// Set device inactive
atomic_set(&device->device_active, 0);

// Signal async I/O cancellation (just the flag!)
atomic_set(&device->async_metadata_ctx.write_cancelled, 1);

// Cancel work (non-blocking - just marks as cancelled)
cancel_work(&device->metadata_sync_work);
```

### Destructor (blocking cleanup)
```c
// Drain all pending/running work
drain_workqueue(device->metadata_workqueue);

// Destroy the workqueue
destroy_workqueue(device->metadata_workqueue);
```

### Why this works
1. **Presuspend is non-blocking** - just signals cancellation
2. **drain_workqueue() is designed for this** - handles queued, running, and completed work
3. **No circular waits** - destructor waits, but nothing waits on destructor
4. **Work exits cleanly** - sees device_active=0 and cancelled flag

## Commits ready to test

- `41ff167` - v4.1: Implement async metadata I/O API
- `3489c2d` - v4.1: Complete async I/O integration  
- `f2af120` - v4.1: Fix presuspend to use cancel_work_sync()
- `f88f116` - v4.1: Fix deadlock - release mutex before waiting
- `eabb808` - v4.1: Back to cancel_work() + drain_workqueue() ← **LATEST**

**Status:** drain_workqueue approach implemented, ready for testing after reboot ✓
