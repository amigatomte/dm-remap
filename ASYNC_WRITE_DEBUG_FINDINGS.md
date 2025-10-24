# Async Metadata Write Debugging - Critical Findings

**Date**: October 24, 2025  
**Branch**: v4.2-auto-rebuild  
**Status**: BREAKTHROUGH - Root cause identified

## Problem Summary

The `dm_remap_write_metadata_v4_async()` function was causing immediate kernel crashes (VM CPU disabled) when called from workqueue context during device initialization.

## Investigation Timeline

### Initial Symptoms
1. VM crashes immediately when async function is called
2. No logs appear from inside the function (first `printk` never executes)
3. Crash happens before any function code runs
4. Synchronous metadata write causes deadlock/hang from workqueue

### Failed Approaches
1. ❌ Using synchronous write from workqueue → **Deadlock**
2. ❌ Using synchronous write from constructor → **Deadlock** (documented in code)
3. ❌ Fire-and-forget without persistence → Works but **loses remaps on reboot**
4. ❌ Making array `static` → Still crashed
5. ❌ Removing debug logging → Still crashed
6. ❌ Using `GFP_NOIO` instead of `GFP_KERNEL` → Still crashed

### Breakthrough Discovery

**TEST**: Created minimal async function with NO implementation:
```c
int dm_remap_write_metadata_v4_async(struct block_device *bdev,
                                     struct dm_remap_metadata_v4 *metadata,
                                     struct dm_remap_async_metadata_context *context)
{
    if (!bdev || !metadata || !context)
        return -EINVAL;
    
    return -ENOSYS; /* Not implemented */
}
```

**RESULT**: ✅ **Function called successfully!**
```
[ 1182.281916] dm-remap: Async function returned: -38
[ 1182.281917] dm-remap v4.0 real: Async function entered successfully
```

## Critical Conclusion

**The crash is NOT in:**
- Function call mechanism
- Function entry/prologue  
- Stack frame setup
- Parameter passing

**The crash IS in:**
- The function body implementation
- Specific code within the async write logic
- Most likely: bio allocation, page allocation, or submit_bio calls

## Root Cause Analysis

The function CAN be called safely from workqueue context. The crash must be caused by:

1. **Bio allocation with wrong flags**: Using `GFP_KERNEL` from workqueue might trigger memory reclaim that deadlocks
2. **Page allocation timing**: `alloc_page()` might be unsafe in this context
3. **submit_bio() context issues**: Submitting bio from this workqueue might violate kernel constraints
4. **Mutex interactions**: `dm_remap_metadata_mutex` might deadlock with block layer locks

## Next Steps

### Immediate Actions Required
1. ✅ Confirmed function can be entered (done)
2. 🔄 Add implementation piece by piece to identify exact crash point:
   - Start with mutex lock/unlock
   - Add metadata header update
   - Add single page allocation
   - Add single bio allocation
   - Add submit_bio call

3. Test each addition to find the exact line causing crash

### Alternative Approaches if Bio Submission Fails
- Use `bio_alloc_bioset()` with custom bioset
- Pre-allocate pages and bios during device init
- Use different GFP flags (`GFP_NOIO | __GFP_NOWARN`)
- Submit bios from different context (kthread?)

## Metadata Persistence Status

**CRITICAL ISSUE**: Currently NO metadata is written to disk:
- ✅ Device creation works
- ✅ I/O operations work  
- ❌ Metadata not persisted (intentionally skipped)
- ❌ **Remaps will be LOST on reboot** (DATA LOSS RISK)

**Why This Matters**:
Without metadata persistence, the module cannot reassemble devices after reboot. All remaps exist only in volatile memory.

## Test Results

### What Works (v4.2.1)
- ✅ Module loads without crashes
- ✅ Device creation succeeds
- ✅ Read/write I/O: ~400-1700 MB/s
- ✅ Fire-and-forget remap activation
- ✅ Async function can be called and entered

### What Doesn't Work
- ❌ Actual metadata persistence to disk
- ❌ Device reassembly after reboot
- ❌ Async write implementation (crashes in body)
- ❌ Sync write from workqueue (deadlocks)

## Code State

### Current Implementation
- `dm_remap_write_metadata_v4_async()`: Minimal stub returning `-ENOSYS`
- Initial metadata write: Calls stub function (returns -ENOSYS)
- Write-ahead remap: Activates immediately, no wait
- Background sync: Uses synchronous write (would deadlock if triggered)

### Files Modified
- `src/dm-remap-v4-metadata.c`: Async function reduced to minimal stub
- `src/dm-remap-v4-real-main.c`: Fire-and-forget approach implemented

## Recommendation

**DO NOT DEPLOY** current code to production - metadata is not persisted. This is a debugging/diagnostic build only.

Priority: Implement working async metadata write by incrementally adding code to identify crash point.
