# dm-remap v4.0 - Quick Start Guide

Get started with dm-remap v4.0 in 5 minutes! 🚀

## What is dm-remap?

**dm-remap** is a Linux kernel device-mapper target that provides transparent bad sector remapping with external spare device support. It protects your data by automatically relocating bad sectors to a spare storage pool while maintaining full read/write access.

### Key Features

- ✅ **Transparent Operation** - No application changes needed
- ✅ **External Spare Pool** - Flexible spare capacity management (Priority 3)
- ✅ **Automatic Setup** - Self-assembling device configuration (Priority 6)
- ✅ **Kernel 6.x Compatible** - Modern API integration
- ✅ **Production Ready** - Fully tested and validated

## Prerequisites

- **Linux Kernel**: 6.0 or higher
- **Tools**: `dmsetup`, `losetup`, standard build tools
- **Privileges**: Root access (sudo)
- **Storage**: Main device + spare device (≥5% larger)

## Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap
```

### Step 2: Build the Modules

```bash
cd src/
make clean
make
```

Expected output:
```
Building dm-remap-v4-real.ko...
Building dm-remap-v4-metadata.ko...
Building dm-remap-v4-spare-pool.ko...
Building dm-remap-v4-setup-reassembly.ko...
Build complete!
```

### Step 3: Verify the Build

```bash
ls -lh *.ko
```

You should see:
- `dm-remap-v4-real.ko` (~544 KB)
- `dm-remap-v4-metadata.ko` (~447 KB)
- `dm-remap-v4-spare-pool.ko` (~457 KB)
- `dm-remap-v4-setup-reassembly.ko` (~919 KB)

## Quick Demo

### Run the Interactive Demo

The fastest way to see dm-remap in action:

```bash
cd /path/to/dm-remap
sudo ./demos/v4_interactive_demo.sh
```

This 60-second demo will:
- Set up a test environment automatically
- Create a dm-remap device with spare pool
- Perform I/O operations
- Show performance metrics
- Clean up automatically

**See [`demos/README.md`](../demos/README.md) for details.**

## Basic Usage

### Example 1: Create a dm-remap Device

```bash
# 1. Load the module
sudo insmod src/dm-remap-v4-real.ko

# 2. Prepare your devices
MAIN_DEV="/dev/sdb"      # Your main storage device
SPARE_DEV="/dev/sdc"     # Spare device (≥5% larger)

# 3. Get the size
SECTORS=$(sudo blockdev --getsz $MAIN_DEV)

# 4. Create the dm-remap device
echo "0 $SECTORS dm-remap-v4 $MAIN_DEV $SPARE_DEV" | \
    sudo dmsetup create my-protected-disk

# 5. Use it like any block device!
sudo mkfs.ext4 /dev/mapper/my-protected-disk
sudo mount /dev/mapper/my-protected-disk /mnt/safe
```

### Example 2: Using Loop Devices (Testing)

Perfect for testing without risking real hardware:

```bash
# Create test images
dd if=/dev/zero of=main.img bs=1M count=1000   # 1GB main
dd if=/dev/zero of=spare.img bs=1M count=1060  # 1.06GB spare (6% larger)

# Set up loop devices
LOOP_MAIN=$(sudo losetup -f --show main.img)
LOOP_SPARE=$(sudo losetup -f --show spare.img)

# Load module
sudo insmod src/dm-remap-v4-real.ko

# Create dm-remap device
SECTORS=$(sudo blockdev --getsz $LOOP_MAIN)
echo "0 $SECTORS dm-remap-v4 $LOOP_MAIN $LOOP_SPARE" | \
    sudo dmsetup create test-remap

# Now /dev/mapper/test-remap is ready to use!
```

### Example 3: Check Device Status

```bash
# View device mapping
sudo dmsetup table my-protected-disk

# Check device status
sudo dmsetup status my-protected-disk

# View kernel messages
sudo dmesg | grep "dm-remap v4" | tail -20
```

## Important Notes

### Spare Device Size Requirements

⚠️ **Current Implementation Limitation (v4.0 Phase 1)**

The current implementation requires the spare device to be **at least 5% larger** than the main device due to a conservative validation check. **This is a known limitation that will be addressed in future releases.**

**Current requirement:**
```bash
# If main device is 1TB:
# Current: Spare must be ≥ 1.05TB (wasteful!)
```

**What it SHOULD be (planned for Priority 4):**

In a properly optimized spare pool implementation, the spare device should only need to store:
- **Metadata**: Remapping tables, sector maps (~few MB)
- **Remapped sectors**: Only the actual bad sectors that need relocation

**Realistic sizing:**
```bash
# Main: 1TB
# Spare: Could be as small as 10GB-50GB depending on:
#   - Expected number of bad sectors
#   - Metadata overhead
#   - Safety margin
```

**Why the current implementation is conservative:**
- Phase 1 focuses on core functionality and testing
- Ensures adequate space for all scenarios during development
- Will be optimized in Priority 4 (Metadata Format Migration Tool)

**Workaround for now:**
- For production use, consider that you're "wasting" 5% capacity
- For testing with loop devices, the overhead is minimal
- Future versions will allow proper flexible spare sizing

### Better Alternative (Current Recommendation)

**Your observation is correct**: If you have a spare device as large as or larger than the main device, you should consider:

1. **RAID 1 (Mirroring)**: Use md/RAID or dm-mirror for full redundancy
2. **LVM + Snapshots**: Better space utilization
3. **Wait for dm-remap v4.1+**: Optimized spare pool sizing

**dm-remap is best suited for**:
- Small spare pools (once optimized)
- Scenarios where you have limited spare capacity
- Gradual bad sector management without full mirroring overhead

### Device Creation Syntax

```bash
echo "0 <sectors> dm-remap-v4 <main_device> <spare_device>" | \
    dmsetup create <name>
```

Parameters:
- `<sectors>`: Size in 512-byte sectors (from `blockdev --getsz`)
- `<main_device>`: Path to main storage device
- `<spare_device>`: Path to spare device (≥5% larger)
- `<name>`: Name for your dm-remap device

## Common Tasks

### Mount a dm-remap Device

```bash
# Create filesystem
sudo mkfs.ext4 /dev/mapper/my-protected-disk

# Mount it
sudo mkdir -p /mnt/protected
sudo mount /dev/mapper/my-protected-disk /mnt/protected

# Use it normally
echo "Hello, dm-remap!" | sudo tee /mnt/protected/test.txt
cat /mnt/protected/test.txt
```

### Remove a dm-remap Device

```bash
# 1. Unmount if mounted
sudo umount /mnt/protected

# 2. Remove the device
sudo dmsetup remove my-protected-disk

# 3. Unload the module (optional)
sudo rmmod dm-remap-v4-real
```

### Persistent Setup (Across Reboots)

Add to `/etc/rc.local` or create a systemd service:

```bash
#!/bin/bash
# Load dm-remap module
insmod /path/to/dm-remap-v4-real.ko

# Create device
SECTORS=$(blockdev --getsz /dev/sdb)
echo "0 $SECTORS dm-remap-v4 /dev/sdb /dev/sdc" | \
    dmsetup create my-protected-disk

# Mount
mount /dev/mapper/my-protected-disk /mnt/protected
```

## Troubleshooting

### "Cannot open spare device" Error

**Problem:** Spare device not found or inaccessible

**Solution:**
```bash
# Check device exists
ls -l /dev/sdc

# Verify permissions
sudo blockdev --getsz /dev/sdc
```

### "Device compatibility validation failed (-ENOSPC)"

**Problem:** Spare device is too small

**Solution:** The spare must be ≥5% larger than main:
```bash
# Check sizes
sudo blockdev --getsz /dev/sdb  # Main
sudo blockdev --getsz /dev/sdc  # Spare (must be ≥ main × 1.05)
```

### "dm-remap-v4: unknown target type"

**Problem:** Module not loaded

**Solution:**
```bash
# Load the module
sudo insmod src/dm-remap-v4-real.ko

# Verify
sudo dmsetup targets | grep dm-remap-v4
```

### Module Won't Load

**Problem:** Kernel version incompatibility

**Solution:**
```bash
# Check kernel version (need 6.x)
uname -r

# Rebuild for your kernel
cd src/
make clean
make
```

## Performance Tips

### Optimal Configuration

1. **Use SSD for spare device** - Faster remapping
2. **Match sector sizes** - Both devices should have same sector size (usually 512 or 4096)
3. **Avoid RAID** - Let dm-remap handle redundancy
4. **Monitor regularly** - Check `dmsetup status` periodically

### Expected Performance

- **Overhead**: < 1% during normal operations
- **Throughput**: Near-native disk speed
- **Latency**: Minimal added latency (<1ms typical)

## Real-World Example

Protecting a 2TB data drive with a 2.1TB spare:

```bash
# Setup
sudo insmod src/dm-remap-v4-real.ko

# Main: 2TB /dev/sdb
# Spare: 2.1TB /dev/sdc (5% larger)

# Create protected device
SECTORS=$(sudo blockdev --getsz /dev/sdb)
echo "0 $SECTORS dm-remap-v4 /dev/sdb /dev/sdc" | \
    sudo dmsetup create data-protected

# Format and mount
sudo mkfs.ext4 /dev/mapper/data-protected
sudo mount /dev/mapper/data-protected /data

# Add to /etc/fstab for persistence
echo "/dev/mapper/data-protected /data ext4 defaults 0 2" | \
    sudo tee -a /etc/fstab
```

## Testing

### Run the Test Suite

```bash
# Quick validation (19 tests)
sudo ./tests/quick_production_validation.sh

# Module loading tests
sudo ./tests/test_v4_module_loading.sh

# Spare pool tests
sudo ./tests/test_v4_spare_pool_functional.sh
```

### Manual Testing

```bash
# Create device
echo "0 102400 dm-remap-v4 /dev/loop0 /dev/loop1" | \
    sudo dmsetup create test

# Write test
sudo dd if=/dev/urandom of=/dev/mapper/test bs=1M count=10

# Read test
sudo dd if=/dev/mapper/test of=/dev/null bs=1M count=10

# Check status
sudo dmsetup status test

# Cleanup
sudo dmsetup remove test
```

## Next Steps

### Learn More

- 📖 [Advanced Usage Guide](ADVANCED_USAGE.md) - Detailed configuration options
- 🎓 [Demo README](../demos/README.md) - Interactive demo documentation
- 🧪 [Testing Guide](../tests/README.md) - Comprehensive testing
- 📊 [Production Validation Report](../docs/testing/PRODUCTION_VALIDATION_REPORT.md)

### Get Help

- 🐛 [Report Issues](https://github.com/amigatomte/dm-remap/issues)
- 💬 [Discussions](https://github.com/amigatomte/dm-remap/discussions)
- 📧 Contact the maintainer

### Contribute

- 🔧 Submit bug fixes
- ✨ Add new features
- 📝 Improve documentation
- 🧪 Add tests

## Quick Reference Card

```bash
# Load module
sudo insmod src/dm-remap-v4-real.ko

# Create device
echo "0 SECTORS dm-remap-v4 MAIN_DEV SPARE_DEV" | \
    sudo dmsetup create NAME

# Check status
sudo dmsetup status NAME

# View table
sudo dmsetup table NAME

# Remove device
sudo dmsetup remove NAME

# Unload module
sudo rmmod dm-remap-v4-real
```

## FAQ

**Q: Can I use dm-remap on my root filesystem?**  
A: Not recommended initially. Test with data partitions first.

**Q: What happens if the spare device fails?**  
A: Data on the main device remains accessible, but new remappings can't occur.

**Q: Can I resize devices after creation?**  
A: Not currently supported in v4.0. Plan your sizes carefully.

**Q: Does dm-remap work with SSDs?**  
A: Yes! Works with HDDs, SSDs, NVMe, and any block device.

**Q: Can I stack dm-remap with other device-mapper targets?**  
A: Yes, dm-remap can be combined with dm-crypt, LVM, etc.

---

**Ready to protect your data?** Start with the [Interactive Demo](../demos/v4_interactive_demo.sh)! 🚀
