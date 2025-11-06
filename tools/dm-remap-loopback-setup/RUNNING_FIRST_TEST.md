# Running Your First ZFS Mirror Degradation Test

## Overview

You now have a complete test suite to create a ZFS mirror pool with one clean dm-remap device and progressively inject bad sectors to test filesystem resilience.

## What You Have

### Script (test-zfs-mirror-degradation.sh)
- 711 lines of comprehensive testing logic
- Orchestrates full workflow: setup → baseline → degradation
- Configurable for different scenarios and resource constraints
- Automatic cleanup on exit

### Documentation
- **ZFS_MIRROR_TESTING.md**: Complete reference guide (287 lines)
- **QUICK_START_ZFS_TESTING.md**: Quick reference (157 lines)
- **THIS FILE**: Step-by-step execution guide

## Step 1: Verify Prerequisites

### Check Required Tools

```bash
# Check ZFS is installed
which zfs
which zpool

# If not installed:
sudo apt-get update
sudo apt-get install zfsutils-linux

# Verify device mapper tools exist (usually pre-installed)
which dmsetup
which losetup
```

### Check dm-remap Module

```bash
# Check if dm-remap module is available
lsmod | grep dm_remap

# If not loaded, try loading it
sudo modprobe dm_remap

# If that fails, you need to compile and install the dm-remap module:
cd /home/christian/kernel_dev/dm-remap
make
sudo make install
sudo modprobe dm_remap
```

### Verify Setup Script

```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
ls -la setup-dm-remap-test.sh

# Should show as executable (-rwxr-xr-x)
# If not, run:
chmod +x setup-dm-remap-test.sh
```

## Step 2: Choose Your Test Scenario

### Option A: Quick Test (5 minutes, minimal resources)

**Best for**: First-time testing, resource-constrained systems

```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 256 -n 2 -v
```

**What it does**:
- Creates 256 MB pool (small, fast)
- 2 degradation stages (quick progression)
- Verbose output (see everything happening)
- Total time: ~5 minutes

**Resources needed**:
- RAM: ~500 MB
- Disk: ~1 GB (temporary)
- Processors: 1 core

---

### Option B: Standard Test (15 minutes, recommended)

**Best for**: Most users, good coverage

```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

**What it does**:
- Creates 512 MB pool (moderate size)
- 5 degradation stages (good granularity)
- 50 bad sectors per stage (gradual degradation)
- Verbose output
- Total time: ~15 minutes

**Resources needed**:
- RAM: ~1 GB
- Disk: ~2 GB (temporary)
- Processors: 1 core

---

### Option C: Aggressive Test (30 minutes, comprehensive)

**Best for**: Deep analysis, larger systems

```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 1024 -n 10 -b 100 -v
```

**What it does**:
- Creates 1024 MB pool (large)
- 10 degradation stages (very fine granularity)
- 100 bad sectors per stage (faster degradation)
- Verbose output
- Total time: ~30 minutes

**Resources needed**:
- RAM: ~2 GB
- Disk: ~3 GB (temporary)
- Processors: 1 core

---

### Option D: Preview with Dry Run (1 minute)

**Best for**: Understanding workflow before executing

```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 256 -n 2 -d -v
```

**What it does**:
- Shows all steps without executing
- No devices created, no data written
- Perfect for understanding flow
- Total time: ~1 minute

## Step 3: Set Up Monitoring (Optional but Recommended)

Before running the test, open additional terminals for monitoring:

### Terminal 1: Run the Test
```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

### Terminal 2: Monitor ZFS Status

```bash
# Watch ZFS pool status in real-time
watch -n 1 'zpool status test-mirror'

# This shows:
# - Pool state (ONLINE / DEGRADED / FAULTED)
# - Device status
# - Any errors or repairs
```

### Terminal 3: Monitor Device Mapper

```bash
# Watch dm-remap status
watch -n 1 'dmsetup status dm-test-remap'

# This shows:
# - Device mapper information
# - Any errors
```

### Terminal 4: Monitor System Logs

```bash
# Watch for any kernel errors related to dm-remap or ZFS
tail -f /var/log/kern.log | grep -E "dm-remap|zfs|error|fail"

# Or use dmesg for real-time kernel messages
dmesg -w | grep -E "dm-remap|zfs|error"
```

## Step 4: Run the Test

### Execute Your Chosen Scenario

```bash
# Standard test (recommended first run)
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

### What You'll See

#### Phase 1: Setup (10-15 seconds)

```
[INFO] =========================================
[INFO] ZFS Mirror Pool Degradation Testing
[INFO] =========================================
[INFO] Configuration:
[INFO]   Pool name: test-mirror
[INFO]   Pool size: 512MB
[INFO]   ...

[INFO] Checking requirements...
[✓] All requirements met

[INFO] ========== SETUP PHASE ==========
[INFO] Setting up loopback device...
[INFO]   File: /tmp/zfs-mirror-loopback.img
[INFO]   Size: 512M
[✓] Loopback device created: /dev/loop0
```

#### Phase 2: Baseline Testing (5-10 seconds)

```
[INFO] ========== BASELINE TESTING ==========
[INFO] Running baseline tests on clean pool...
[INFO] Writing test data (100MB)...
[INFO] Calculating checksums...
[INFO]   Baseline checksum: a3f5c8d2e1b9...
[✓] Baseline tests passed
```

#### Phase 3: Degradation Stages (remaining time)

```
[INFO] ========== DEGRADATION TESTING ==========

[INFO] --- Stage 1/5 ---
[INFO] Stage 1: Injecting bad sectors (total: ~50 bad sectors)...
[✓] Bad sectors injected
[INFO] Pool status:
    pool: test-mirror
    state: ONLINE
    ...
[✓] Checksum verified (data integrity maintained)
[✓] Stage 1 tests complete

[INFO] --- Stage 2/5 ---
...
```

#### Completion

```
[INFO] ========== FINAL STATUS ==========
[INFO] ZFS Pool Status:
  pool: test-mirror
  state: ONLINE (or DEGRADED)
  ...

[✓] Testing complete!
```

## Step 5: Interpret Results

### Successful Test (All Stages Complete)

**Indicators**:
- All stages completed without hanging
- Pool status shown for each stage
- Checksums verified at most stages
- Clean exit and cleanup

**Interpretation**:
- ZFS mirror working correctly
- dm-remap handling injections properly
- Data integrity maintained (checksums match)
- Graceful degradation as expected

### Stages Pass, But Some Warnings

**Example**:
```
[WARN] Could not write stage test data (device may have I/O errors)
```

**Interpretation**:
- This is normal at later stages with many bad sectors
- Indicates dm-remap device is becoming unreliable
- ZFS may continue via mirror redundancy
- Shows where device failure threshold is

### Pool Becomes Degraded

**Example**:
```
pool: test-mirror
state: DEGRADED
```

**Interpretation**:
- dm-remap device has faulted
- One device in mirror is offline
- ZFS correctly identified failure
- Mirror still operational via other device
- This is expected with heavy degradation

### Test Stops/Hangs

**Possible Causes**:
1. dm-remap device completely unresponsive
2. ZFS attempting extensive repair
3. System I/O overloaded

**Resolution**:
```bash
# Stop with Ctrl+C
# Cleanup should run automatically

# If cleanup hangs, in another terminal:
sudo zpool destroy -f test-mirror
sudo ./setup-dm-remap-test.sh --cleanup
```

## Step 6: Analyze Results

### Key Metrics to Check

1. **Data Integrity**
   ```
   Baseline checksum: a3f5c8d2e1b9...
   Stage 1 checksum: a3f5c8d2e1b9... ✓ (matches)
   Stage 2 checksum: a3f5c8d2e1b9... ✓ (matches)
   ```
   - Should match throughout the test
   - Mismatches indicate data corruption

2. **Pool Status Progression**
   ```
   Stage 1: ONLINE
   Stage 2: ONLINE
   Stage 3: ONLINE
   Stage 4: DEGRADED ← Device faulted
   Stage 5: FAULTED  ← Critical failure
   ```
   - Tracks when mirror becomes degraded
   - Shows failure threshold

3. **Operation Success Rate**
   ```
   Stage 1: All operations succeed
   Stage 2: All operations succeed
   Stage 3: Some operations may fail (warnings)
   Stage 4-5: More failures expected
   ```

### Questions to Answer

1. **How many bad sectors until pool degradation?**
   - Count the stages where state changed to DEGRADED
   - Multiply by bad sectors per stage
   - Indicates failure threshold

2. **Did data remain consistent?**
   - Check if checksums matched through all stages
   - Any mismatches indicate corruption

3. **How responsive was ZFS?**
   - Did operations complete quickly?
   - Any extended timeouts?
   - How many errors reported?

4. **What's the mirror's actual resilience?**
   - One device failed, did other protect data?
   - Could you still read files?
   - Was data correctly replicated?

## Step 7: Run Additional Tests

After successful first run, try variations:

### Different Pool Sizes

```bash
# Test with larger pool
sudo ./test-zfs-mirror-degradation.sh -s 1024 -n 5 -v

# Test with smaller pool
sudo ./test-zfs-mirror-degradation.sh -s 128 -n 5 -v
```

### Different Degradation Rates

```bash
# Slower degradation (smaller increments)
sudo ./test-zfs-mirror-degradation.sh -s 512 -b 10 -n 10 -v

# Faster degradation (larger increments)
sudo ./test-zfs-mirror-degradation.sh -s 512 -b 200 -n 3 -v
```

### More Stages

```bash
# Fine-grained analysis (20 stages)
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 20 -b 25 -v
```

### Compare Different Configurations

```bash
# Test 1: Light degradation
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 10 -b 25 -v

# Test 2: Medium degradation
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 10 -b 50 -v

# Test 3: Heavy degradation
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 10 -b 100 -v

# Compare results: Which shows clearest degradation?
```

## Troubleshooting

### Error: "zfs command not found"

```bash
# Install ZFS utilities
sudo apt-get update
sudo apt-get install zfsutils-linux
```

### Error: "dm_remap module not found"

```bash
# Navigate to dm-remap directory and compile
cd /home/christian/kernel_dev/dm-remap
make
sudo make install
sudo modprobe dm_remap

# Verify it loaded
lsmod | grep dm_remap
```

### Error: "Permission denied"

```bash
# Must run with sudo
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v

# Or use sudo bash
sudo bash -c './test-zfs-mirror-degradation.sh -s 512 -n 5 -v'
```

### Script Hangs at a Stage

```bash
# Check what's happening in another terminal
watch -n 1 'zpool status test-mirror'
watch -n 1 'dmsetup status dm-test-remap'

# If truly stuck, Ctrl+C and cleanup
# Cleanup runs automatically via trap
```

### Pool Creation Fails

```bash
# Try smaller pool
sudo ./test-zfs-mirror-degradation.sh -s 256 -n 3 -v

# Check available loopback devices
losetup -f

# Check system logs
dmesg | tail -20
```

## Next Steps

After completing tests:

1. **Compare Results**
   - Run tests with different configurations
   - Create comparison table of failure thresholds
   - Document performance impacts

2. **Generate Reports**
   - Copy output to file: `sudo ./script.sh > test_results.log 2>&1`
   - Capture pool status at each stage
   - Document any warnings or errors

3. **Explore Device Mapper Stats**
   ```bash
   # During test, in another terminal:
   dmsetup status dm-test-remap
   # Shows detailed remapping statistics
   ```

4. **Benchmark Performance**
   - Add timing measurements
   - Compare degraded vs clean performance
   - Identify bottlenecks

## Key Files Location

```
/home/christian/kernel_dev/dm-remap/
├── tools/dm-remap-loopback-setup/
│   ├── test-zfs-mirror-degradation.sh ← Main test script
│   ├── setup-dm-remap-test.sh ← Device setup tool
│   ├── ZFS_MIRROR_TESTING.md ← Full reference
│   ├── QUICK_START_ZFS_TESTING.md ← Quick reference
│   └── ON_THE_FLY_BAD_SECTORS.md ← Bad sector details
```

## Success! What's Next?

You now have a complete testing framework for:

- ✓ Creating clean ZFS mirror pools
- ✓ Progressively degrading devices in real-time
- ✓ Measuring ZFS resilience under failure
- ✓ Verifying data integrity
- ✓ Understanding failure thresholds
- ✓ Analyzing recovery mechanisms

Use this to test various storage configurations, compare different setups, or validate your dm-remap implementation.

Good luck with your testing!
