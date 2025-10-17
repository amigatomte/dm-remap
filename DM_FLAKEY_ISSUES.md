# dm-flakey Testing Issues - Root Cause Analysis

**Date:** October 17, 2025  
**Issue:** Testing dm-remap with error injection causes hangs  
**Status:** Root cause identified - requires code fix

## Summary

Testing revealed **two critical issues**:

1. **dm-flakey causes extreme slowness** (~116 seconds per remap)
2. **Deadlock bug in dm-remap** error handling during device initialization

## Issue 1: dm-flakey Performance Problem

### Symptoms
- Test appears to hang on write operations
- System shows processes in D state (uninterruptible sleep)
- Takes 100+ seconds per sector remap operation

### Root Cause
When dm-flakey injects errors:
1. dm-remap detects the error
2. dm-remap tries to remap by reading/writing to spare
3. **The read/write to create the remap ALSO goes through dm-flakey**
4. If dm-flakey is still in error mode, the remap operation fails
5. dm-remap retries... and retries... and retries...
6. Eventually succeeds when dm-flakey cycles to "up" mode
7. This takes ~2 minutes per sector

### Evidence
```
Kernel logs showed:
[25437.964844] dm-remap: I/O error on sector 160, attempting automatic remap
[25553.863194] dm-remap: Successfully remapped failed sector 160 -> 0
                ^^^ 116 seconds later!
```

### Conclusion
**dm-flakey is unsuitable for dm-remap testing** - both `error_reads` and `error_writes` cause extreme slowness.

---

## Issue 2: Deadlock Bug in dm-remap (CRITICAL)

### Symptoms
- Device creation hangs when bad sectors exist in low sector numbers
- Kernel shows mutex deadlock in `dm_remap_end_io_v4_real`
- System requires reboot to recover

### Root Cause
When dm-remap device is created:
1. Device initialization triggers I/O to read device geometry/metadata
2. This I/O hits bad sectors (e.g., sectors 1000-1001)
3. Error handler `dm_remap_end_io_v4_real()` is called
4. Error handler calls `dm_remap_analyze_error_pattern()`
5. **`dm_remap_analyze_error_pattern()` takes `device->health_mutex`**
6. But this mutex may already be held during device initialization
7. **DEADLOCK**

### Evidence
```
Kernel stack trace showed:
[ 3723.477343]  mutex_lock+0x3b/0x50
[ 3723.477346]  dm_remap_end_io_v4_real.cold+0x6f/0x4bd [dm_remap_v4_real]
[ 3723.477349]  ? __pfx_dm_remap_end_io_v4_real+0x10/0x10 [dm_remap_v4_real]
[ 3723.477351]  clone_endio+0x69/0x200
```

Kernel log showed:
```
[ 3723.477186] dm-remap v4.0: I/O error detected on sector 1002 (error=-5)
[ 3723.477192] dm-remap v4.0: I/O error on sector 1002 (error=-5), attempting automatic remap
[then system hung in mutex_lock]
```

### Code Location
File: `src/dm-remap-v4-real.c`
- Line 510: `mutex_lock(&device->health_mutex);` in `dm_remap_analyze_error_pattern()`
- Called from: Line 404 in `dm_remap_handle_io_error()`
- Called from: Line 1545 in `dm_remap_end_io_v4_real()`

### Impact
- **Device creation fails** if bad sectors exist in metadata areas
- **System hangs** - device cannot be removed without reboot
- **Blocks all testing** with dm-linear + dm-error method

---

## The Correct Testing Method: dm-linear + dm-error

### Why It Should Work
1. **Surgical precision**: Exact sector-level control
2. **Immediate errors**: No timing/retry issues like dm-flakey
3. **No interference**: Spare device operations not affected
4. **Standard targets**: Always available, well-tested

### How It Works
```bash
# Create device with sectors 50000, 50001, 75000 as bad sectors
# dm table:
0 50000 linear /dev/loop0 0           # Sectors 0-49999: good
50000 2 error                          # Sectors 50000-50001: BAD  
50002 24998 linear /dev/loop0 50002    # Sectors 50002-74999: good
75000 1 error                          # Sector 75000: BAD
75001 remaining linear /dev/loop0 75001 # Rest: good
```

### Why We Can't Test It Yet
The deadlock bug prevents device creation when bad sectors are present, even in high sector ranges.

---

## Required Fixes

### Fix 1: Prevent Mutex Deadlock (CRITICAL - BLOCKING)

**Problem:** `dm_remap_analyze_error_pattern()` takes `health_mutex` from I/O completion context

**Solution Options:**

**Option A: Defer Error Analysis** (Recommended)
```c
// In dm_remap_handle_io_error():
// Don't call dm_remap_analyze_error_pattern() directly
// Instead, queue it to a workqueue:
schedule_work(&device->error_analysis_work);
```

**Option B: Use Spinlock Instead of Mutex**
```c
// Change health_mutex to health_spinlock
// But this limits what we can do in error analysis
```

**Option C: Skip Analysis During Init**
```c
// Add a flag to track if device is fully initialized
if (device->fully_initialized) {
    dm_remap_analyze_error_pattern(device, failed_sector);
}
```

**Recommendation:** Option A (workqueue) is safest and most flexible.

### Fix 2: Make dm-flakey Usable (OPTIONAL - NICE TO HAVE)

Not critical since we have dm-linear + dm-error as an alternative.

Could potentially be fixed by:
- Using dm-flakey only on main device, not in spare device path
- But this requires complex setup and doesn't solve fundamental timing issues

---

## Action Items

### Immediate (Required for Testing)

1. **Fix deadlock bug** in `dm-remap-v4-real.c`:
   - Move `dm_remap_analyze_error_pattern()` call to workqueue
   - Test that device creation succeeds with bad sectors present
   - Estimated time: 30 minutes

2. **Test with dm-linear + dm-error**:
   - Run `test_dm_linear_error.sh`
   - Verify remaps are created instantly
   - Check data integrity
   - Estimated time: 15 minutes

### Future (Optional)

3. **Document dm-flakey limitations** in README
4. **Add warning** if users try to stack dm-remap with dm-flakey
5. **Consider alternative** error injection methods for automated testing

---

## Test Results So Far

| Test Method | Result | Notes |
|-------------|--------|-------|
| dm-flakey with error_reads | ❌ Hangs | Mount operations stuck in D state |
| dm-flakey with error_writes | ❌ Very slow | 116 seconds per remap |
| dm-flakey with drop_writes | ❌ Hangs | Similar to error_writes |
| dm-linear + dm-error | ⏳ Blocked | Deadlock bug prevents testing |

---

## Lessons Learned

1. **I/O completion handlers must not block**: Taking mutexes from `end_io` callbacks is dangerous
2. **dm-flakey is not suitable** for testing error handlers that perform remapping
3. **dm-linear + dm-error is the correct approach** but requires deadlock fix first
4. **Always test with bad sectors in high ranges** to avoid initialization I/O

---

## Next Steps

**Before next test run:**
1. Fix the mutex deadlock (30 min work)
2. Rebuild dm-remap module
3. Reboot system (to clear hung devices)
4. Run dm-linear + dm-error test
5. Expect SUCCESS ✅

**Expected outcome after fix:**
- Device creates instantly
- Remaps created in milliseconds
- No hanging, no deadlock
- Clean test results

---

## References

- Test script: `tests/test_dm_linear_error.sh`
- Precision bad sectors example: `tests/precision_bad_sectors.sh`
- Error code: `src/dm-remap-v4-real.c` lines 388-560, 1513-1560
- Kernel logs: `/var/log/kern.log` timestamp 3723 seconds

---

**Status:** Testing blocked until deadlock fix is implemented.  
**Priority:** HIGH - This is the blocker for all error injection testing.  
**ETA:** 30 minutes to fix + 15 minutes to test = 45 minutes total.
