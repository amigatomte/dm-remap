# Quick Start: ZFS Mirror with dm-remap Testing

## TL;DR - One Command

```bash
sudo ./test-zfs-mirror-degradation.sh
```

This creates a ZFS mirror pool with clean dm-remap device and progressively injects bad sectors while testing filesystem operations.

## Before You Start

```bash
# Make sure you have ZFS installed
sudo apt-get install zfsutils-linux

# Navigate to the script directory
cd /home/christian/kernel_dev/dm-remap/tools/dm-remap-loopback-setup

# Make script executable (if needed)
chmod +x test-zfs-mirror-degradation.sh
```

## Common Scenarios

### Quick Test (5 min, minimal resources)

```bash
sudo ./test-zfs-mirror-degradation.sh -s 256 -n 2 -v
```

- 256 MB pool
- 2 degradation stages
- 50 bad sectors per stage

### Standard Test (15 min, recommended for most cases)

```bash
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v
```

- 512 MB pool (typical)
- 5 degradation stages (good coverage)
- 50 bad sectors per stage (gradual degradation)

### Aggressive Test (30 min, comprehensive analysis)

```bash
sudo ./test-zfs-mirror-degradation.sh -s 1024 -n 10 -b 100 -v
```

- 1024 MB pool
- 10 degradation stages (fine granularity)
- 100 bad sectors per stage (faster degradation)

### Dry Run (verify without changes)

```bash
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 3 -d -v
```

- Shows all steps without executing
- Useful to understand what will happen
- Good for first-time testing

## Monitor During Testing

In a separate terminal, watch the test progress:

```bash
# Watch ZFS pool status
watch -n 1 'zpool status test-mirror'

# Or in another terminal, watch device mapper
watch -n 1 'dmsetup status dm-test-remap'

# Or check system logs for errors
tail -f /var/log/kern.log | grep -i "dm-remap\|zfs\|error"
```

## Expected Results

### Baseline Phase

```
[✓] Baseline tests passed
    - 100 MB file written successfully
    - Checksum verified
    - 10 test files created
```

### Each Degradation Stage

```
[INFO] Stage 2: Injecting bad sectors (total: ~100 bad sectors)...
[INFO] Pool status: ONLINE (or DEGRADED)
[✓] Checksum verified (data integrity maintained)
[✓] Stage 2 tests complete
```

### Successful Completion

```
[✓] Testing complete!
```

Followed by automatic cleanup.

## What Gets Tested

1. **Baseline Performance**
   - Write 100 MB random data
   - Calculate SHA256 checksums
   - Verify data integrity

2. **Progressive Degradation** (for each stage)
   - Inject bad sectors
   - Check ZFS pool status
   - Read baseline data (checksum verification)
   - Write new test data
   - Read directory contents

3. **Resilience Observations**
   - Does ZFS stay ONLINE or degrade?
   - Are I/O errors transparent?
   - Does data remain consistent?
   - How many bad sectors until failure?

## Output Files

During testing, created at `/$POOL_NAME` (e.g., `/test-mirror`):

```
/test-mirror/
├── test_baseline.dat          # Baseline test file (100 MB)
├── test_files/                # Multiple test files (10 × 10 MB)
│   ├── file_1.dat
│   ├── file_2.dat
│   └── ...
├── stage_1/                   # Files written at stage 1
│   └── stage_1.dat
├── stage_2/                   # Files written at stage 2
│   └── stage_2.dat
└── ...
```

All automatically cleaned up at script exit.

## Troubleshooting

### Error: "zfs command not found"

```bash
sudo apt-get install zfsutils-linux
```

### Error: "device mapper error"

```bash
# Check if setup script exists
ls -la setup-dm-remap-test.sh

# Verify it's executable
chmod +x setup-dm-remap-test.sh
```

### Pool Creation Failed

```bash
# Try a smaller pool size
sudo ./test-zfs-mirror-degradation.sh -s 256 -v

# Or check system logs
dmesg | tail -20
```

### Stuck/Hanging

```bash
# Try Ctrl+C to interrupt
# The cleanup should run automatically
```

## Performance Baseline

| Stage | Description | Expected Time |
|-------|-------------|---|
| Setup | Create loopback + dm-remap | 5-10 sec |
| Baseline | Write and verify 100 MB | 5-10 sec |
| Stage 1-5 | Each: inject + test | 5-15 sec per stage |
| **Total (5 stages)** | **Complete workflow** | **2-3 minutes** |

## Key Files

- Script: `test-zfs-mirror-degradation.sh`
- Setup tool: `setup-dm-remap-test.sh`
- Full guide: `ZFS_MIRROR_TESTING.md`
- Bad sector info: `ON_THE_FLY_BAD_SECTORS.md`

## Next Steps

After first successful run:

1. **Try different pool sizes**: `-s 1024` for larger tests
2. **Adjust degradation rate**: `-b 100` for faster injection
3. **Monitor in more detail**: `-v` for verbose output
4. **Run multiple tests**: Compare results

## Example: Full Testing Session

```bash
# Terminal 1: Run the test
sudo ./test-zfs-mirror-degradation.sh -s 512 -n 5 -v

# Terminal 2: Monitor in parallel
watch -n 1 'zpool status test-mirror'

# Terminal 3: Check for errors
tail -f /var/log/kern.log | grep -i error
```

## Success Criteria

- ✓ Pool creation succeeds
- ✓ Baseline tests pass (checksums verified)
- ✓ Each stage completes without hanging
- ✓ Data integrity maintained through stages
- ✓ Cleanup executes successfully

You're all set! Run `sudo ./test-zfs-mirror-degradation.sh` to start testing.
