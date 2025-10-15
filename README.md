# dm-remap - Transparent Bad Sector Remapping for Linux

**dm-remap** is a Linux kernel device-mapper target that transparently remaps bad sectors from a failing drive to a spare device. When your drive develops bad sectors beyond its internal spare capacity, dm-remap keeps your data accessible.

> **Status**: Active development - not yet released to production

---

## What It Does

dm-remap sits between your applications and storage devices, automatically detecting and remapping bad sectors:

```
Application I/O
       ↓
   dm-remap  ────→  Spare Device
       ↓             (bad sectors)
  Main Device
 (healthy sectors)
```

**Key Capabilities:**
- Transparent bad sector remapping to external spare device
- Intelligent spare sizing (only ~2-5% of main device needed)
- Persistent remap tables that survive reboots
- Statistics export for monitoring tools (Prometheus, Nagios, Grafana)
- Automatic device reassembly after system restart

---

## When You Need This

Modern drives have built-in bad sector remapping, but when that fails:

- **Aging storage** (2-4 years old) where hardware spare pools are exhausted
- **Cost-sensitive environments** where replacing drives is expensive
- **Legacy systems** running critical workloads on failing hardware
- **Testing/development** for storage resilience validation

**Not a replacement for**: Regular backups, SMART monitoring, or proper drive maintenance.

### Firmware Remapping vs dm-remap

**How they work together:**

```
┌─────────────────────────────────────────────────────────┐
│  Application Layer                                      │
│  (filesystems, databases, etc.)                         │
└────────────────────┬────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│  dm-remap (Software Layer)                              │
│  • Handles sectors firmware can't remap                 │
│  • Provides additional spare capacity                   │
│  • Transparent to applications                          │
└────────────────────┬────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│  Drive Firmware (Hardware Layer)                        │
│  • Internal spare pool (factory allocated)              │
│  • Automatic remapping (invisible to OS)                │
│  • Works until spare pool exhausted                     │
└─────────────────────────────────────────────────────────┘
```

**Timeline of a failing drive:**

1. **Months 0-24**: Firmware handles occasional bad sectors internally
   - dm-remap: Passive monitoring, no intervention needed
   - User: No action required

2. **Months 24-36**: Firmware spare pool depleting
   - dm-remap: Detects increasing I/O errors, prepares for activation
   - User: Monitor SMART attributes, plan spare device

3. **Months 36+**: Firmware spare pool exhausted
   - dm-remap: **Activates**, takes over bad sector management
   - User: Benefit from extended drive life, plan replacement

**Key insight**: dm-remap is your **second line of defense** when hardware-level protection fails. The drive firmware tries first; dm-remap catches what firmware can't handle.

---

## Quick Start

### Prerequisites
- Linux kernel 5.x or later
- Root access
- Two block devices (main device + spare device)

### Build and Load

```bash
# Clone and build
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap/src
make

# Load the modules
sudo insmod dm-remap-v4-real.ko
sudo insmod dm-remap-v4-metadata.ko
sudo insmod dm-remap-v4-spare-pool.ko
sudo insmod dm-remap-v4-setup-reassembly.ko
```

### Create a Remap Device

```bash
# Example: 100GB main device with 5GB spare
MAIN=/dev/sdb     # Your main device
SPARE=/dev/sdc    # Your spare device

# Create dm-remap device
echo "0 $(blockdev --getsz $MAIN) remap $MAIN $SPARE 0 $(blockdev --getsz $SPARE)" | \
  sudo dmsetup create my_remap

# Use /dev/mapper/my_remap for all I/O operations
# Bad sectors are automatically remapped to spare
```

### Check Status

```bash
# View current status
sudo dmsetup status my_remap

# View statistics (if stats module loaded)
cat /sys/kernel/dm_remap/total_remaps
cat /sys/kernel/dm_remap/health_score
cat /sys/kernel/dm_remap/all_stats    # Prometheus format
```

### Testing with Loop Devices

For development and testing, use sparse files and loop devices:

```bash
# Create sparse files (fast, no disk space used until written)
truncate -s 1G main_device.img
truncate -s 50M spare_device.img

# Attach as loop devices
LOOP_MAIN=$(sudo losetup -f --show main_device.img)
LOOP_SPARE=$(sudo losetup -f --show spare_device.img)

# Create dm-remap device
echo "0 $(sudo blockdev --getsz $LOOP_MAIN) remap $LOOP_MAIN $LOOP_SPARE 0 $(sudo blockdev --getsz $LOOP_SPARE)" | \
  sudo dmsetup create test_remap

# Use the device
sudo mkfs.ext4 /dev/mapper/test_remap
sudo mount /dev/mapper/test_remap /mnt

# Cleanup when done
sudo umount /mnt
sudo dmsetup remove test_remap
sudo losetup -d $LOOP_MAIN $LOOP_SPARE
rm main_device.img spare_device.img
```

**Why sparse files?** They don't consume disk space until data is written, making them perfect for testing large device configurations without using actual storage.

### Adding Multiple Spare Devices

dm-remap supports pooling multiple spare devices for additional capacity:

```bash
# Create dm-remap device with initial spare
echo "0 $(blockdev --getsz $MAIN) remap $MAIN $SPARE1 0 $(blockdev --getsz $SPARE1)" | \
  sudo dmsetup create my_remap

# Add additional spare devices via dmsetup message
sudo dmsetup message my_remap 0 add_spare /dev/sdd
sudo dmsetup message my_remap 0 add_spare /dev/sde

# Check spare pool status
sudo dmsetup status my_remap

# Remove a spare device (if no active allocations)
sudo dmsetup message my_remap 0 remove_spare /dev/sdd
```

**Spare device selection**: dm-remap uses first-fit allocation, automatically choosing the best spare device with available capacity for each remap operation.

---

## Testing

Run the comprehensive test suite to validate your setup:

```bash
cd dm-remap
sudo tests/quick_production_validation.sh
```

**Test Coverage:**
- Module loading and unloading
- Device creation and teardown
- Symbol exports and dependencies
- Basic I/O operations
- Error handling and recovery
- Metadata persistence
- Spare pool allocation
- Setup reassembly after reboot

Expected output: All tests should pass with green checkmarks ✓

---

## Current Features

### Core Functionality
- ✅ **Transparent remapping**: Applications see normal block device
- ✅ **Automatic bad sector detection**: I/O errors trigger remapping
- ✅ **Persistent metadata**: Remap tables survive reboots
- ✅ **Intelligent spare sizing**: ~2% overhead instead of 100%+ mirroring
- ✅ **Multiple spare devices**: Pool multiple spare devices for capacity

### Monitoring & Operations
- ✅ **sysfs statistics**: `/sys/kernel/dm_remap/`
- ✅ **Monitoring integration**: Prometheus, Grafana, Nagios examples
- ✅ **dmsetup commands**: Standard device-mapper interface
- ✅ **Health scoring**: 0-100 health metric

### Reliability
- ✅ **Automatic reassembly**: Devices reconnect after reboot
- ✅ **Metadata redundancy**: Multiple copies for safety
- ✅ **Error recovery**: Graceful handling of corruption
- ✅ **Resource management**: Proper cleanup and error paths

---

## Documentation

Detailed guides are available in the `docs/` directory:

- **[User Guide](docs/user/)** - Setup, configuration, monitoring
- **[Statistics Monitoring](docs/user/STATISTICS_MONITORING.md)** - Integration with monitoring tools
- **[Developer Notes](docs/development/)** - Architecture and internals

---

## Current Limitations

**This is development software.** Known limitations:

- No active wear leveling on spare devices
- No built-in SMART monitoring (use `smartctl` separately)
- Statistics are counters only (no predictive analysis)
- Tested primarily on loop devices and virtual environments
- Add/remove spare devices requires dmsetup message commands

**Production Use**: Not recommended yet. Use at your own risk.

---

## Monitoring Integration

Export statistics to your monitoring stack:

```bash
# Load statistics module
sudo insmod src/dm-remap-v4-stats.ko

# Prometheus scraping
curl http://localhost:9100/sys/kernel/dm_remap/all_stats

# Nagios check
HEALTH=$(cat /sys/kernel/dm_remap/health_score)
if [ $HEALTH -lt 70 ]; then exit 2; fi  # CRITICAL
if [ $HEALTH -lt 90 ]; then exit 1; fi  # WARNING
exit 0  # OK

# Grafana dashboard
# Import metrics from Prometheus, visualize health_score, total_remaps, etc.
```

See [STATISTICS_MONITORING.md](docs/user/STATISTICS_MONITORING.md) for complete examples.

---

## Architecture

dm-remap is built as a modular kernel subsystem:

- **dm-remap-v4-real** - Core device-mapper target
- **dm-remap-v4-metadata** - Persistent remap table management  
- **dm-remap-v4-spare-pool** - Spare sector allocation
- **dm-remap-v4-setup-reassembly** - Automatic device pairing
- **dm-remap-v4-stats** - Statistics export (optional)

All modules must be loaded in dependency order (see Quick Start).

---

## Contributing

Contributions welcome! This is an active development project.

**Focus areas:**
- Real hardware testing (physical drives, not just loop devices)
- Performance optimization for production workloads
- Integration with existing storage management tools
- Documentation improvements

**Please test thoroughly** before submitting pull requests. All tests must pass.

---

## Comparison with Alternatives

**vs. Hardware RAID**: Software solution, no special hardware needed  
**vs. BTRFS/ZFS**: Works with any filesystem, simpler but less featured  
**vs. mdadm**: Focused on bad sector remapping, not full redundancy  
**vs. lvmcache**: Different purpose (caching vs. bad sector management)

**Use existing tools for**: SMART monitoring (`smartctl`), bad sector detection (`badblocks`), full drive diagnostics.

dm-remap is designed to **extend** your storage management toolkit, not replace it.

---

## License

GNU General Public License v2.0 (GPL-2.0)

See [LICENSE](LICENSE) for full text.

---

## Author

Christian ([@amigatomte](https://github.com/amigatomte))

**Acknowledgments**: Built on Linux device-mapper infrastructure. Thanks to the kernel storage subsystem maintainers.

---

## Getting Help

- **Issues**: [GitHub Issues](https://github.com/amigatomte/dm-remap/issues)
- **Questions**: Open a discussion on GitHub
- **Testing**: Run test suite and report results

**This is development software.** Expect bugs, API changes, and incomplete features.

---

## Project Status

**Latest Updates:**
- Intelligent spare sizing (Oct 15, 2025)
- Statistics monitoring integration (Oct 15, 2025)
- Automatic setup reassembly (Oct 14, 2025)

**In Progress:**
- Real hardware validation
- Performance optimization
- Production hardening

**Planned:**
- Public release (pending production validation)
- Documentation website
- RPM/DEB packages

---

*dm-remap is under active development. This README reflects current capabilities as of October 2025.*
