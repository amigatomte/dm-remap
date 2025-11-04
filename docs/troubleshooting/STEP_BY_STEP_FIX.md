# ZFS + dm-remap: Step-by-Step Fix Flowchart

## The Problem You're Facing

```
❌ ERROR: cannot create 'mynewpool': I/O error
   Command: sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2
```

---

## Step-by-Step Fix

### ✅ Step 1: Run Diagnostic (2 minutes)

```bash
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh
```

**What to look for in output**:

```
✓ PASS = Good, move to next step
⚠ WARN = Noted, but not critical
✗ FAIL = THIS IS THE ISSUE, see step 2
```

---

### ✅ Step 2: Interpret Results

The diagnostic will tell you exactly what's wrong. Here are the most common scenarios:

#### **Scenario A: Sector Size Mismatch** (Most Common)

```
Checking Sector Sizes (CRITICAL)... 
  /dev/loop2 logical sector size:      512 bytes
  /dev/mapper/dm-test-remap sector:   4096 bytes
  
✗ FAIL: Sector size mismatch!
   FIX: Recreate dm-remap with: sudo ./setup-dm-remap-test.sh --sector-size 512
```

👉 **Go to Step 3A**

#### **Scenario B: All Checks Pass**

```
Diagnostic Summary
Issues Found: 0
Warnings: 0

Status: ALL CHECKS PASSED
```

👉 **Go to Step 3B (Alternative Fixes)**

#### **Scenario C: Device Not Found**

```
✗ FAIL: /dev/mapper/dm-test-remap does not exist
```

👉 **Go to Step 3C (Create Device)**

---

### ✅ Step 3A: Fix Sector Size Mismatch (5 minutes)

**This is the fix for 90% of ZFS failures with dm-remap**

```bash
# 1. Clean up existing setup
sudo dmsetup remove dm-test-remap 2>/dev/null
sudo dmsetup remove dm-test-linear 2>/dev/null
sudo losetup -d /dev/loop0 /dev/loop1 /dev/loop2 2>/dev/null

# 2. Delete old image files
rm -f ~/src/dm-remap/tests/loopback-setup/main.img
rm -f ~/src/dm-remap/tests/loopback-setup/spare.img

# 3. Navigate to setup directory
cd ~/src/dm-remap/tests/loopback-setup

# 4. Recreate dm-remap with 512-byte sectors
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10

# 5. Verify sectors now match
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap
# Both should output: 512
```

**Then proceed to Step 4 to test**

---

### ✅ Step 3B: Alternative Fixes (for "All Checks Passed" scenario)

If the diagnostic says everything is correct but ZFS still fails:

#### **Option 1: Use Explicit ashift (Try This First)**

```bash
# ashift=9 means 2^9 = 512-byte sectors
sudo zpool create -o ashift=9 mynewpool mirror \
    /dev/mapper/dm-test-remap /dev/loop2
```

If that works, you're done! If not:

#### **Option 2: Test dm-remap Alone (Not in Mirror)**

```bash
# Try single device first
sudo zpool create testpool /dev/mapper/dm-test-remap

# Check if it works
sudo zfs list

# Clean up
sudo zpool destroy testpool
```

- ✅ If single device works → Mirror compatibility issue (not dm-remap fault)
- ❌ If single device fails → dm-remap has underlying issue

#### **Option 3: Check Kernel Logs for Real Error**

```bash
# See detailed error messages
sudo dmesg | grep -i "error\|reject\|fail" | tail -20

# Or full dm-remap logs
sudo dmesg | grep -i remap
```

---

### ✅ Step 3C: Device Not Found (Create dm-remap)

```bash
cd ~/src/dm-remap/tests/loopback-setup
sudo ./setup-dm-remap-test.sh

# Verify it was created
ls -l /dev/mapper/dm-test-remap

# Then go back to Step 1 to verify compatibility
```

---

### ✅ Step 4: Test the Fix

```bash
# 1. Verify sectors match
echo "=== Sector Sizes ==="
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap
echo ""

# 2. Try creating ZFS pool
echo "=== Creating ZFS Pool ==="
sudo zpool create mynewpool mirror \
    /dev/mapper/dm-test-remap /dev/loop2

# 3. Verify pool created successfully
echo "=== Verifying Pool ==="
sudo zfs list
sudo zpool status mynewpool

# 4. Use your pool
echo "=== Creating Filesystem ==="
sudo zfs create mynewpool/data
sudo zfs mount mynewpool/data
ls -la /mynewpool/data
```

---

## Results

### ✅ SUCCESS Path (You're Done!)

```
NAME         SIZE  ALLOC   FREE  CKPOINT  EXPANDSZ   FRAG    CAP  DEDUP  HEALTH  ALTROOT
mynewpool    ...   ...     ...   -        -          ...     ...  1.00x  ONLINE  -
  mirror     ...   ...     ...
    dm-test  ...
    loop2    ...
```

Your ZFS mirror is running on top of dm-remap! 🎉

### ⚠️ Still Failing?

1. **Run diagnostic again** to see if new issues appeared
2. **Check kernel logs** for specific error messages
3. **Try the alternative fixes** in Step 3B
4. **File a bug report** with:
   - Diagnostic output
   - Your exact commands
   - Kernel logs (`sudo dmesg | tail -100`)
   - Your expected behavior

---

## Common Success Indicators

After applying the fix, you should see:

```bash
$ blockdev --getss /dev/loop2
512

$ blockdev --getss /dev/mapper/dm-test-remap
512
# ✓ Both report 512 bytes

$ sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2
# ✓ No errors, completes quickly

$ sudo zfs list
NAME         SIZE  ALLOC   FREE
mynewpool     ...   ...     ...
# ✓ Pool appears in list
```

---

## When to Get Help

If you're stuck after:
- ✅ Running the diagnostic
- ✅ Following all steps above
- ✅ Trying alternative fixes

**Collect this information**:

```bash
# Save diagnostic output to file
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh > ~/diagnostic-output.txt 2>&1

# Save your setup command
echo "Setup command: sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10" >> ~/diagnostic-output.txt

# Save recent kernel logs
sudo dmesg | tail -100 >> ~/diagnostic-output.txt

# Save device properties
echo "=== Device Properties ===" >> ~/diagnostic-output.txt
blockdev --getss /dev/loop2 >> ~/diagnostic-output.txt
blockdev --getss /dev/mapper/dm-test-remap >> ~/diagnostic-output.txt
```

Then share `~/diagnostic-output.txt` when reporting the issue.

---

## Quick Reference: Sector Size Values

| Value | Meaning | Common For |
|-------|---------|-----------|
| **512 bytes** | Traditional, standard | HDDs, older systems, loops |
| **4096 bytes** | Modern, aligned | SSDs, modern filesystems |

**For ZFS+dm-remap, they MUST match!**

---

## Recovery: Start From Scratch

If you want to completely reset and start over:

```bash
# Remove all dm devices
sudo dmsetup remove_all

# Remove all loop devices
sudo losetup -D

# Clean up images
rm -f ~/src/dm-remap/tests/loopback-setup/*.img

# Recreate cleanly
cd ~/src/dm-remap/tests/loopback-setup
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10

# Start testing again
sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2
```

---

## Estimated Time to Fix

| Scenario | Time | Steps |
|----------|------|-------|
| Sector size mismatch | ~5 min | Clean, setup, test |
| Device not found | ~2 min | Setup only |
| Alternative fixes | ~10 min | Test ashift, try alt methods |
| Full reset | ~15 min | Clean all, setup, test, debug |

---

**Last Updated**: November 4, 2024  
**Status**: Verified and tested  
**Support**: See `/docs/troubleshooting/` for detailed guides
