# Module Unloading Fix - Test Plan

## Issue Identified
**Problem**: Module cannot be unloaded due to persistent kernel worker threads
- `kworker/R-dmr_auto_remap` - from auto_remap_wq global workqueue  
- `kworker/R-dm-remap-autosave` - from per-device autosave workqueue
- `kworker/R-dm-remap-health` - from per-device health scanner workqueue

**Root Cause**: Missing `dmr_io_exit()` call in module exit function + potential per-device workqueue cleanup issues

## Fix Applied (Commit 68810db)
- Added missing `dmr_io_exit()` call to `dm_remap_exit()` function
- This should cleanup the global `auto_remap_wq` workqueue properly
- Improved cleanup sequence and documentation

## Test Sequence After Reboot

### Test 1: Basic Module Loading/Unloading
```bash
cd src
sudo insmod dm_remap.ko
lsmod | grep dm_remap                    # Should show: dm_remap 126976 0

# Check for worker threads (should be none without devices)
ps aux | grep -E "(dm_remap|remap)" | grep -v grep

# Try unloading immediately
sudo rmmod dm_remap                      # Should succeed now!
lsmod | grep dm_remap                    # Should show nothing
```

### Test 2: Module Loading + Device Creation + Cleanup
```bash
# Load module
sudo insmod dm_remap.ko

# Create test devices
dd if=/dev/zero of=/tmp/test1.img bs=1M count=10
dd if=/dev/zero of=/tmp/test2.img bs=1M count=10
LOOP1=$(sudo losetup -f --show /tmp/test1.img)
LOOP2=$(sudo losetup -f --show /tmp/test2.img)

# Create dm-remap device
echo "0 20480 remap $LOOP1 $LOOP2 0 20480" | sudo dmsetup create test_device

# Check worker threads (may exist due to device)
ps aux | grep -E "(dm_remap|remap)" | grep -v grep

# Test I/O to ensure functionality
sudo dd if=/dev/dm-0 of=/dev/null bs=4096 count=1

# Clean up device
sudo dmsetup remove test_device
sudo losetup -d $LOOP1 $LOOP2
rm /tmp/test*.img

# Check worker threads after device removal (should be minimal/none)
ps aux | grep -E "(dm_remap|remap)" | grep -v grep

# Try module unloading
sudo rmmod dm_remap                      # Should succeed!
```

### Test 3: Multiple Device Creation/Removal Cycle
```bash
sudo insmod dm_remap.ko

# Create and remove multiple devices to test workqueue cleanup
for i in {1..3}; do
    dd if=/dev/zero of=/tmp/test${i}_main.img bs=1M count=5
    dd if=/dev/zero of=/tmp/test${i}_spare.img bs=1M count=5
    LOOP_MAIN=$(sudo losetup -f --show /tmp/test${i}_main.img)
    LOOP_SPARE=$(sudo losetup -f --show /tmp/test${i}_spare.img)
    echo "0 10240 remap $LOOP_MAIN $LOOP_SPARE 0 10240" | sudo dmsetup create test_device_$i
    
    # Brief I/O test
    sudo dd if=/dev/dm-$((i-1)) of=/dev/null bs=4096 count=1
    
    # Cleanup
    sudo dmsetup remove test_device_$i
    sudo losetup -d $LOOP_MAIN $LOOP_SPARE
    rm /tmp/test${i}_*.img
done

# Check for lingering worker threads
ps aux | grep -E "(dm_remap|remap)" | grep -v grep

# Final unload test
sudo rmmod dm_remap                      # Should succeed!
```

## Expected Results
- ✅ Module loads and unloads cleanly without devices
- ✅ Device creation and I/O operations work perfectly  
- ✅ Device removal cleans up per-device workqueues
- ✅ Module unloading succeeds after all devices removed
- ✅ No persistent kernel worker threads after module removal

## If Tests Fail
If per-device workqueue cleanup still fails:
1. Check device destructor (`remap_dtr`) for missing cleanup calls
2. Add explicit workqueue destruction to autosave and health cleanup functions
3. Consider making autosave and health workqueues global (like auto_remap_wq)

## Success Criteria
- **Primary**: Module can be loaded and unloaded without "Resource temporarily unavailable" error
- **Secondary**: No persistent kernel worker threads after module removal
- **Tertiary**: All hanging issues remain resolved (module loading, device creation, I/O operations)

---
**Status**: Ready for reboot and testing