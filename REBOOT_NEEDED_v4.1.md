# v4.1 Development - Reboot Required

**Date:** October 22, 2025  
**Branch:** v4.1-async-io  
**Reason:** Kernel hung on old workqueue leak, need clean reboot

## What happened

First test of v4.1 async I/O hung during device removal at `destroy_workqueue()`.

**Root cause:** Used `cancel_work()` instead of `cancel_work_sync()` in presuspend.
- `cancel_work()` just marks work as cancelled (doesn't wait)
- `destroy_workqueue()` DOES wait for work to finish
- Result: destructor hung waiting for work that was never properly cancelled

## What was fixed

Changed presuspend to use `cancel_work_sync()` which is NOW SAFE because:

1. **Set device_active=0 first** - work function exits early at checkpoints
2. **Cancel async I/O before waiting** - no blocking I/O to devices being removed  
3. **Work has multiple exit points** - checks device_active before each operation

**The fix:**
```c
// Before (WRONG - causes hang):
cancel_work(&device->metadata_sync_work);  // Doesn't wait!

// After (CORRECT):
cancel_work_sync(&device->metadata_sync_work);  // Waits for completion
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

**Status:** Ready for testing after reboot ✓
