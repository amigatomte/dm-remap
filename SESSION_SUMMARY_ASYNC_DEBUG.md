# Session Summary: Async Metadata Write Debugging

**Date**: October 24, 2025  
**Commit**: 530a095  
**Progress**: Critical breakthrough achieved

## What We Accomplished

### 1. Identified Root Cause Location ✅
- **Proved**: Async function CAN be called from workqueue
- **Proved**: Crash is NOT in function entry/call mechanism  
- **Proved**: Crash is IN the function body implementation
- **Method**: Reduced function to minimal stub → worked perfectly

### 2. Ruled Out Multiple Hypotheses
- ❌ Function pointer corruption
- ❌ Stack overflow during call
- ❌ Invalid parameters
- ❌ Calling convention issues
- ❌ Static array allocation
- ❌ Module symbol resolution

### 3. Established Working Baseline
- Module loads and runs stable
- Device creation works
- I/O operations work (400-1700 MB/s)
- Fire-and-forget remap activation works
- **Async function stub callable**

## Critical Findings

### The Good News
```c
// This WORKS - proves function entry is safe:
int dm_remap_write_metadata_v4_async(...) {
    if (!bdev || !metadata || !context)
        return -EINVAL;
    return -ENOSYS;
}
```

**Result**: Function called successfully, returned -38 (-ENOSYS)

### The Bad News
- Original implementation crashes VM
- Crash happens in function body, not entry
- Synchronous write deadlocks from workqueue
- **Currently NO metadata is persisted to disk**

## Current State

### What Works
- ✅ Device creation and basic operation
- ✅ Read/write I/O performance
- ✅ Module stability (no crashes with stub)
- ✅ Async function callable

### What Doesn't Work  
- ❌ Metadata persistence (data loss on reboot)
- ❌ Async write implementation (crashes)
- ❌ Sync write from workqueue (deadlocks)
- ❌ Device reassembly after reboot

### Data Safety Status
**CRITICAL**: This build does NOT persist metadata!
- Remaps exist only in volatile memory
- **Reboot = complete data loss**
- **NOT SUITABLE FOR PRODUCTION**

## Next Steps (Priority Order)

### Immediate (Must Do)
1. **Incremental Implementation**: Add async write code line-by-line
   - Start with mutex lock
   - Add metadata update
   - Add single page allocation
   - Add single bio creation
   - Add submit_bio call
   - **Test after each addition**

2. **Identify Exact Crash Line**: Find which operation causes crash
   - Bio allocation with specific flags?
   - Page allocation in this context?
   - submit_bio from workqueue?
   - Mutex interaction?

### Alternative Approaches (If Direct Fix Fails)
1. Use pre-allocated bio pool (bioset)
2. Use kthread instead of workqueue
3. Different GFP flags (GFP_NOIO | __GFP_NOWARN)
4. Defer to userspace helper
5. Use dedicated I/O submission thread

### Long-term
1. Implement robust metadata persistence
2. Add crash recovery testing
3. Validate device reassembly
4. Performance optimization

## Technical Decisions Made

### Fire-and-Forget Architecture
- Remaps activated immediately (no wait)
- Background worker syncs to disk (when working)
- Relies on 5-copy redundancy for safety
- **Tradeoff**: Better than deadlock but requires working async write

### Deferred Metadata Operations
- Read metadata in delayed workqueue (100ms after construction)
- Avoids constructor deadlock
- Allows device creation to complete quickly

## Questions Answered

1. **Can async function be called?** YES ✅
2. **Is crash in function entry?** NO ❌  
3. **Is crash in function body?** YES ✅
4. **Does sync write work?** Only outside workqueue ⚠️
5. **Is metadata persisted?** NO (currently disabled) ❌

## Lessons Learned

1. **Workqueue I/O is tricky**: Can't do synchronous waits
2. **Async I/O from workqueue**: Should work but implementation matters
3. **Incremental debugging**: Minimal repro is key to finding root cause
4. **Function stubs**: Useful for isolating crash location
5. **Metadata persistence**: Critical for data safety, can't be skipped

## Files Modified

- `src/dm-remap-v4-metadata.c`: Async function reduced to stub
- `src/dm-remap-v4-real-main.c`: Fire-and-forget implementation
- `ASYNC_WRITE_DEBUG_FINDINGS.md`: Detailed investigation log

## Testing Evidence

```
# Successful stub function call:
[ 1182.281916] dm-remap: Async function returned: -38
[ 1182.281917] dm-remap v4.0 real: Async function entered successfully

# Device creation working:
Device created successfully
5 MB write @ 376 MB/s
5 MB read @ 1.7 GB/s
0 errors
Clean shutdown
```

## Recommendation

**Continue with incremental implementation approach**:
- We know the function can be called
- We know crash is in the body
- Add code carefully, test each addition
- This WILL lead us to the exact problematic line

**Do NOT use this build for anything except debugging** - metadata is not persisted.

---
*End of Session Summary*
