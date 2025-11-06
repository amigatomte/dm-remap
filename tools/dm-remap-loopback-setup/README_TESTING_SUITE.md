# ZFS Mirror Pool Testing Suite - Documentation Index

## Quick Navigation

### 👤 First Time Here?
**Start here:** [RUNNING_FIRST_TEST.md](RUNNING_FIRST_TEST.md)
- Step-by-step guide to running your first test
- Prerequisites checklist
- 4 testing scenarios with timing
- Expected output examples

### 🚀 Quick Start
**TL;DR:** [QUICK_START_ZFS_TESTING.md](QUICK_START_ZFS_TESTING.md)
- One-command quick start
- Common scenarios (5 min / 15 min / 30 min)
- Troubleshooting quick reference
- Performance baselines

### 📖 Complete Reference
**Full details:** [ZFS_MIRROR_TESTING.md](ZFS_MIRROR_TESTING.md)
- Architecture overview with diagrams
- Detailed workflow phases
- Advanced usage patterns
- Real-world applications

### 💾 Bad Sector Injection Details
**Technical info:** [ON_THE_FLY_BAD_SECTORS.md](ON_THE_FLY_BAD_SECTORS.md)
- Bad sector injection mechanism
- Progressive degradation workflows
- Integration with ZFS testing

---

## Documentation at a Glance

| Document | Purpose | Length | Audience |
|----------|---------|--------|----------|
| RUNNING_FIRST_TEST.md | Step-by-step execution | 535 lines | Everyone |
| QUICK_START_ZFS_TESTING.md | Quick reference | 157 lines | Experienced users |
| ZFS_MIRROR_TESTING.md | Complete reference | 287 lines | Advanced users |
| ON_THE_FLY_BAD_SECTORS.md | Technical details | Updated | Developers |

---

## The Testing Suite

### Main Script
**`test-zfs-mirror-degradation.sh`** (711 lines)

What it does:
- Creates ZFS mirror pool with loopback + dm-remap devices
- Runs baseline tests on clean pool
- Progressively injects bad sectors
- Monitors ZFS behavior at each stage
- Verifies data integrity
- Automatic cleanup

Quick start:
```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

---

## Supported Testing Scenarios

### 1️⃣ Quick Test (5 minutes)
```bash
sudo ./test-zfs-mirror-degradation.sh -s 256 -n 2 -v
```
- 256 MB pool
- 2 degradation stages
- Resources: 500 MB RAM, 1 GB disk
- Use when: Limited time/resources

### 2️⃣ Standard Test (15 minutes) ⭐ RECOMMENDED
```bash
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```
- 512 MB pool
- 5 degradation stages
- Resources: 1 GB RAM, 2 GB disk
- Use when: Most comprehensive analysis

### 3️⃣ Aggressive Test (30 minutes)
```bash
sudo ./test-zfs-mirror-degradation.sh -s 1024 -n 10 -b 100 -v
```
- 1024 MB pool
- 10 degradation stages
- Resources: 2 GB RAM, 3 GB disk
- Use when: Fine-grained analysis needed

### 4️⃣ Dry Run (1 minute)
```bash
sudo ./test-zfs-mirror-degradation.sh -d -v
```
- Preview without executing
- No devices created
- Use when: Understanding workflow

---

## What Gets Tested

### ✓ ZFS Resilience
- Does mirror redundancy protect data?
- How does pool respond to failures?
- At what threshold does it degrade?

### ✓ Data Integrity
- Checksums maintained through degradation?
- Any corruption detected?
- RAID protection working?

### ✓ I/O Performance
- Throughput impact of bad sectors?
- Latency increase over time?
- Error recovery overhead?

### ✓ Device Mapper Integration
- On-the-fly injection working?
- Error statistics accurate?
- Bad sector remapping correct?

---

## Key Features

✅ **Automated Workflow**
- Setup, baseline, degradation phases
- No manual intervention required
- Automatic cleanup on exit

✅ **Configurable**
- Pool size: 128 MB - 4 GB+
- Degradation stages: 1 - 20+
- Bad sectors per stage: adjustable
- Custom pool names

✅ **Data Verification**
- SHA256 checksums
- Automatic integrity checks
- Detects corruption

✅ **Monitoring**
- Color-coded output
- Real-time status
- Comprehensive logging

✅ **Robust Error Handling**
- Validation of configuration
- Graceful failure recovery
- Automatic cleanup

---

## Architecture

```
ZFS Mirror Pool (test-mirror)
│
├─ Device 1: Loopback (/dev/loop*)
│  └─ Baseline device (no dm-remap)
│  └─ For comparison
│
└─ Device 2: dm-remap (/dev/mapper/dm-test-remap)
   ├─ Started clean (--no-bad-sectors)
   ├─ Bad sectors injected on-the-fly (--add-bad-sectors)
   └─ Simulates progressive media degradation

Device Mapper Stack:
  /dev/mapper/dm-test-linear ──> dm-linear device
              ↓
  /dev/mapper/dm-test-remap ──> dm-remap-v4 (handles remapping)
```

---

## Command Reference

### Basic Usage
```bash
# Default: 1024 MB pool, 5 stages, 50 bad sectors/stage
sudo ./test-zfs-mirror-degradation.sh

# With verbose output
sudo ./test-zfs-mirror-degradation.sh -v

# Dry run (preview)
sudo ./test-zfs-mirror-degradation.sh -d -v
```

### Custom Parameters
```bash
# Different pool size
sudo ./test-zfs-mirror-degradation.sh -s 512

# Different number of stages
sudo ./test-zfs-mirror-degradation.sh -n 10

# Different bad sectors per stage
sudo ./test-zfs-mirror-degradation.sh -b 100

# Custom pool name
sudo ./test-zfs-mirror-degradation.sh -p my-pool

# Save output to file
sudo ./test-zfs-mirror-degradation.sh -v > results.log 2>&1
```

---

## Expected Output Timeline

### Phase 1: Setup (10-15 seconds)
```
Creating loopback device...
Creating dm-remap device (clean)...
Creating ZFS mirror pool...
```

### Phase 2: Baseline (5-10 seconds)
```
Writing test data (100 MB)...
Calculating checksums...
Creating test files...
Verifying data integrity...
```

### Phase 3: Degradation (varies by configuration)
For each stage:
```
Stage N: Injecting bad sectors...
  Pool status: ONLINE/DEGRADED
  Checksums: VERIFIED/FAILED
  Operations: SUCCESS/PARTIAL/FAILED
```

### Phase 4: Cleanup (5 seconds)
```
Destroying ZFS pool...
Cleaning up dm-remap...
Removing loopback files...
```

---

## Interpreting Results

### Successful Test
- ✓ All stages complete
- ✓ Checksums verified throughout
- ✓ Clean automatic exit

**Interpretation:** ZFS mirror protecting data correctly

### Pool Becomes Degraded
- ⚠ Pool status: DEGRADED at stage N
- ✓ Checksums still verified
- ✓ Operations may have warnings

**Interpretation:** Expected behavior with heavy degradation

### Data Corruption
- ✗ Checksum mismatches
- ✗ Operations fail with errors
- ✗ Pool state: FAULTED

**Interpretation:** Critical failure point reached

---

## Common Commands for Monitoring

### Monitor in Separate Terminals

**Terminal 1: Run test**
```bash
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

**Terminal 2: Watch ZFS status**
```bash
watch -n 1 'zpool status test-mirror'
```

**Terminal 3: Watch device mapper**
```bash
watch -n 1 'dmsetup status dm-test-remap'
```

**Terminal 4: Check system logs**
```bash
tail -f /var/log/kern.log | grep -i error
```

---

## Troubleshooting

### Problem: "zfs command not found"
**Solution:** `sudo apt-get install zfsutils-linux`

### Problem: "dm_remap module not found"
**Solution:** Compile and install dm-remap kernel module

### Problem: Pool creation fails
**Solution:** Try smaller pool size: `-s 256`

### Problem: Script hangs
**Solution:** Press Ctrl+C, cleanup runs automatically

For more: See [RUNNING_FIRST_TEST.md](RUNNING_FIRST_TEST.md) troubleshooting section

---

## Prerequisites Checklist

```
✓ ZFS utilities: sudo apt-get install zfsutils-linux
✓ Device mapper tools: which dmsetup
✓ Loopback support: which losetup
✓ dm-remap module: lsmod | grep dm_remap
✓ Resources: 1+ GB RAM, 2+ GB disk
✓ Script executable: chmod +x test-zfs-mirror-degradation.sh
```

---

## File Locations

```
/home/christian/kernel_dev/dm-remap/
└─ tools/dm-remap-loopback-setup/
   ├─ test-zfs-mirror-degradation.sh ← Main script
   ├─ setup-dm-remap-test.sh ← Device setup tool
   ├─ RUNNING_FIRST_TEST.md ← This is where to start
   ├─ QUICK_START_ZFS_TESTING.md ← Quick reference
   ├─ ZFS_MIRROR_TESTING.md ← Full documentation
   └─ ON_THE_FLY_BAD_SECTORS.md ← Technical details
```

---

## Next Steps

### Immediate (5 minutes)
1. Read [RUNNING_FIRST_TEST.md](RUNNING_FIRST_TEST.md)
2. Check prerequisites
3. Run dry-run: `sudo ./test-zfs-mirror-degradation.sh -d -v`

### Short-term (15-30 minutes)
1. Run first test: `sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v`
2. Observe output and results
3. Note failure threshold

### Medium-term (hours/days)
1. Run variations with different parameters
2. Compare results
3. Create performance reports

### Long-term
1. Integrate analysis with other tools
2. Generate degradation curves
3. Document findings

---

## Real-World Applications

- **Storage Validation:** Test new hardware under degradation
- **ZFS Testing:** Verify RAID mirror protection
- **App Testing:** Evaluate application robustness
- **Driver Development:** Validate error handling

---

## Key Commits

| Commit | Feature | Lines |
|--------|---------|-------|
| f582044 | Clean device support (--no-bad-sectors) | 164 |
| f17dbd1 | ZFS mirror test suite | 1,155 |
| 8cc848b | Execution guide | 535 |

**Total:** 1,854 lines of code + documentation

---

## Support

For issues or questions:

1. **"How do I run this?"**
   → See [RUNNING_FIRST_TEST.md](RUNNING_FIRST_TEST.md)

2. **"How does it work?"**
   → See [ZFS_MIRROR_TESTING.md](ZFS_MIRROR_TESTING.md)

3. **"Quick tips?"**
   → See [QUICK_START_ZFS_TESTING.md](QUICK_START_ZFS_TESTING.md)

4. **"Bad sector details?"**
   → See [ON_THE_FLY_BAD_SECTORS.md](ON_THE_FLY_BAD_SECTORS.md)

---

## TL;DR

**One command to test ZFS mirror resilience:**

```bash
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

**Wait ~15 minutes for results, then analyze.**

For details: See [RUNNING_FIRST_TEST.md](RUNNING_FIRST_TEST.md)

---

Ready to test? Start with [RUNNING_FIRST_TEST.md](RUNNING_FIRST_TEST.md) 🚀
