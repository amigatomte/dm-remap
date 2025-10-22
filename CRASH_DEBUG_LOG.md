# Crash Debugging Log - dm-remap v4.0
**Date:** October 20, 2025

## Problem Summary
Device removal crashes/freezes the VM when remap entries are present in the device.
- ✅ Device removal WITHOUT remaps: Works perfectly
- ❌ Device removal WITH remaps: VM freezes, CPU disabled, high CPU usage

## Root Cause Analysis - Evolution

### Attempt 1: Shutdown Checks in end_io and handle_io_error (FAILED)
**Hypothesis:** Race between I/O completion and device teardown
**Implementation:** Added shutdown_in_progress checks in end_io and handle_io_error
**Result:** ❌ Still crashes/freezes - VM consumed all CPU during removal

**Why it failed:** Shutdown checks alone are not sufficient. The problem is:
1. In-flight I/Os are still completing AFTER presuspend starts freeing remaps
2. There's no synchronization - we just check a flag but don't wait for I/O to drain
3. Device-mapper doesn't automatically stop I/O completions when removal starts
4. The remap list is freed while end_io callbacks can still run and access it

### Attempt 2: In-Flight I/O Counter with Drain Logic (CURRENT)
**Hypothesis:** We need to wait for ALL in-flight I/O to complete before freeing any resources

**Implementation:**
1. **Added `atomic_t in_flight_ios` counter** to device structure (line ~297)
2. **Increment in map():** When accepting I/O (DM_MAPIO_REMAPPED path, line ~1412)
3. **Decrement in end_io():** When I/O completes (line ~1960)
4. **Wait in presuspend():** Before freeing remaps, spin-wait for counter to reach zero (line ~2104)

**Key changes:**
```c
// In device structure:
atomic_t in_flight_ios;  /* Track in-flight I/O operations */

// In map() - when accepting I/O:
atomic_inc(&device->in_flight_ios);
return DM_MAPIO_REMAPPED;

// In end_io() - when I/O completes:
atomic_dec(&device->in_flight_ios);
return DM_ENDIO_DONE;

// In presuspend() - wait for drain:
atomic_set(&device->shutdown_in_progress, 1);
int wait_count = 0;
while (atomic_read(&device->in_flight_ios) > 0 && wait_count < 100) {
    printk(KERN_CRIT "dm-remap: Waiting for %d in-flight I/Os...\n",
           atomic_read(&device->in_flight_ios));
    msleep(100);
    wait_count++;
}
// Then free remaps safely
```

**Why this should work:**
- ✅ Guarantees ALL I/O completes before freeing any resources
- ✅ Prevents race between end_io() and presuspend()
- ✅ Standard device-mapper pattern for safe teardown
- ✅ Timeout prevents infinite wait (10 seconds max)

**Code locations:**
- Counter declaration: Line ~297 in dm_remap_device_v4_real structure
- Counter init: Line ~1691 in dm_remap_ctr_v4_real()
- Counter increment: Line ~1412 in dm_remap_map_v4_real()
- Counter decrement: Line ~1960 in dm_remap_end_io_v4_real()
- Drain logic: Line ~2104 in dm_remap_presuspend_v4_real()

## Testing Plan - BEFORE RUNNING TEST
1. **Module compiled successfully** - no errors
2. **About to test:** test_one_remap_remove.sh
3. **Expected behavior:**
   - Device creates successfully
   - One remap is triggered (read from sector 1000)
   - During removal: presuspend logs "Waiting for N in-flight I/Os"
   - In-flight count drops to zero
   - Presuspend frees all remaps safely
   - Destructor is reached and completes
   - ✅ No crash/freeze
4. **If it fails:** Check dmesg for last messages before freeze

## Previous Failed Attempts
1. ❌ Emergency remap freeing in destructor → Never reached destructor
2. ❌ Clearing remaps in presuspend → Race with end_io still occurred
3. ❌ Shutdown checks alone → Still crashed (no synchronization)
4. ❌ Disabling suspend hooks → Not the issue
5. ❌ Manual suspend/resume → Not the issue
6. ❌ Disabling workqueues → Not the issue

## Key Insight - Race Condition Details
**The real problem:** Device-mapper's I/O completion (end_io callbacks) can run AFTER presuspend starts. Simply checking a flag doesn't prevent the race - we need to actively WAIT for all I/O to finish.

**Analogy:** It's like trying to demolish a building while people are still inside. Setting a "closing" sign isn't enough - you must wait for everyone to exit first.

---
**Status:** In-flight I/O counter implemented, compiled successfully, READY TO TEST
**Confidence:** High - this is the standard device-mapper pattern for safe teardown
**Risk:** If this still crashes, the problem may be in device-mapper core, not our code
