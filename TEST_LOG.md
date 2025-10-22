# dm-remap Test Log - October 20, 2025

## Current Issue
VM crashes/freezes during device removal when remaps are present.

## Root Cause Identified
**Race condition in remap list iteration:**
- `dm_remap_find_remap_entry()` was iterating the remap list WITHOUT holding the spinlock
- `dm_remap_sync_persistent_metadata()` was also iterating without the lock
- When presuspend clears the list while map() tries to iterate, causes list corruption → infinite loop

## Fixes Applied
1. Added spinlock protection to `dm_remap_find_remap_entry()` 
2. Added spinlock protection to `dm_remap_sync_persistent_metadata()`
3. Both functions now hold `device->remap_lock` during list iteration

## Test Configuration
- Loop devices: /dev/loop18 (main), /dev/loop19 (spare)
- Test: test_one_remap_remove.sh
- Module: dm-remap-v4-real.ko with real_device_mode=1
- Test creates ONE remap then attempts removal

## Expected Behavior
- Device removal should succeed without crash
- Presuspend hook clears remaps with spinlock held
- Map function returns errors after shutdown flag set
- No infinite loops in list iteration

## Test Status

### Previous Test Result (18:37)
**FAILED** - VM froze immediately during device removal despite spinlock fixes

### Current Test (18:45) - NEW APPROACH
**Strategy:** Disable remap clearing in presuspend, let destructor handle it
**Theory:** Presuspend clearing itself may be causing the race condition

**Changes:**
- Presuspend: Sets shutdown flag, does NOT clear remaps
- Map: Returns DM_MAPIO_KILL when shutdown (not DM_MAPIO_SUBMITTED)  
- Destructor: Frees remaps with spinlock protection
- All list iterations protected by spinlock

**Module rebuilt:** `ls -lh src/dm-remap-v4-real.ko` shows latest timestamp

---
## Test Execution About to Start
**If VM freezes:** The issue is not presuspend clearing, but something deeper (bio handling, device-mapper state machine, etc.)
