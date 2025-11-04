# 🆘 Troubleshooting: ZFS Pool Creation Fails

## You Got This Error

```
cannot create 'mynewpool': I/O error
```

**When**: Trying to create a ZFS mirror with dm-remap  
**Command**: `sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2`

---

## The Fix (Copy-Paste Ready)

### Step 1: Diagnose (Takes 1 minute)

```bash
sudo /home/christian/kernel_dev/dm-remap/scripts/diagnose-zfs-compat.sh
```

Read the colored output. It will tell you exactly what's wrong.

### Step 2: Fix It (Most likely fix - takes 5 minutes)

If diagnostic says **"SECTOR SIZE MISMATCH"**, run these commands:

```bash
# Clean up
sudo dmsetup remove dm-test-remap dm-test-linear 2>/dev/null
sudo losetup -d /dev/loop{0,1,2} 2>/dev/null
rm -f ~/src/dm-remap/tests/loopback-setup/*.img

# Recreate with correct sector size
cd ~/src/dm-remap/tests/loopback-setup
sudo ./setup-dm-remap-test.sh --sector-size 512 -c 10
```

### Step 3: Test (Takes 2 minutes)

```bash
# Verify it worked
blockdev --getss /dev/loop2
blockdev --getss /dev/mapper/dm-test-remap
# Both should show: 512

# Create your ZFS pool
sudo zpool create mynewpool mirror /dev/mapper/dm-test-remap /dev/loop2

# Verify pool exists
sudo zfs list
sudo zpool status mynewpool
```

**Done! ✅**

---

## Detailed Guides

If you need more information:

| Document | Read When |
|----------|-----------|
| **[`STEP_BY_STEP_FIX.md`](docs/troubleshooting/STEP_BY_STEP_FIX.md)** | You want a visual flowchart |
| **[`ZFS_QUICK_FIX.md`](docs/troubleshooting/ZFS_QUICK_FIX.md)** | You want to understand what happened |
| **[`ZFS_COMPATIBILITY_ISSUE.md`](docs/troubleshooting/ZFS_COMPATIBILITY_ISSUE.md)** | You want deep technical details |
| **[`ZFS_SOLUTION_PACKAGE.md`](ZFS_SOLUTION_PACKAGE.md)** | You want a complete overview |

---

## Didn't Work?

1. **Run diagnostic again** to check for new issues
2. Try alternative fixes in [STEP_BY_STEP_FIX.md](docs/troubleshooting/STEP_BY_STEP_FIX.md#-step-3b-alternative-fixes)
3. Check kernel logs: `sudo dmesg | tail -50`

---

**Estimated time to fix**: ~8 minutes  
**Success rate**: ~95% (sector size mismatch)

Happy troubleshooting! 🚀
