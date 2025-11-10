# dm-remap Demonstrations

Welcome to dm-remap demonstrations! 🎉

## Available Demonstrations

### 1. Interactive Script Demo (`v4_interactive_demo.sh`)

A hands-on, guided tour of dm-remap's key features:

- **Priority 3: External Spare Device Support** - Automatic spare pool management
- **Priority 6: Automatic Setup Reassembly** - Symbol exports and module integration
- **Transparent I/O** - Read/write operations through dm-remap
- **Performance Testing** - Real-world performance characteristics
- **Module Management** - Loading, verification, and monitoring

### 2. Video Demonstrations (vhs tape files)

Creating terminal recording videos for demonstration and documentation:

#### `dm-remap-zfs-demo.tape`
A step-by-step walkthrough showing:
- dm-remap module loading
- Device creation and configuration
- ZFS pool creation with dm-remap
- Data compression and performance
- Full system integration

**Usage**:
```bash
vhs < demos/dm-remap-zfs-demo.tape > dm-remap-demo.mp4
```

#### `dm-remap-test-execution.tape`
Complete integration test execution showing:
- Full 10-step test sequence
- Test device creation
- Pool setup and configuration
- Bad sector injection
- Filesystem operations
- Data integrity verification
- Compression ratio results (1.46x)

**Usage**:
```bash
vhs < demos/dm-remap-test-execution.tape > dm-remap-test.mp4
```

**Requirements for video generation**:
- Install vhs: https://github.com/charmbracelet/vhs
- Install ffmpeg for video output
- Linux environment with bash

---

## Interactive Script Demo Details

This section covers the `v4_interactive_demo.sh` script:

### Prerequisites

- Linux kernel 6.x or higher
- Root privileges (sudo)
- dm-remap v4.0 modules built (run `make` in `src/` directory)

### Running the Demo

```bash
cd /home/christian/kernel_dev/dm-remap
sudo ./demos/v4_interactive_demo.sh
```

**Duration**: ~60 seconds  
**Interactive**: Yes (press ENTER to continue at prompts)

## What Happens During the Demo

### Step 1: Environment Setup
- Creates temporary test directory (`/tmp/dm-remap-demo`)
- Creates 200MB main device image
- Creates 212MB spare device image (6% larger for metadata overhead)
- Sets up loop devices for both images

### Step 2: Module Loading
- Loads `dm-remap-v4-real.ko` kernel module
- Verifies dm-remap-v4 target availability
- Confirms version information

### Step 3: Device Creation
- Creates dm-remap device with spare pool
- Configures main and spare device mapping
- Verifies device node creation (`/dev/mapper/dm-remap-demo`)

### Step 4: I/O Operations
- **Write Test**: 50MB of random data
- **Read Test**: 50MB read back for verification
- **Random I/O**: Mixed read/write pattern simulation

### Step 5: Status Monitoring
- Device status query via `dmsetup status`
- Kernel message review
- Real-time monitoring information

### Step 6: Module Information
- Lists loaded dm-remap modules
- Shows exported kernel symbols
- Confirms module stability

### Step 7: Performance Testing
- Sequential write benchmark (10MB)
- Sequential read benchmark (10MB)
- Performance summary and analysis

### Finale
- Summary of what was demonstrated
- Device information and statistics
- Option to explore further or clean up

## Sample Output

```
╔═══════════════════════════════════════════════════════════════╗
║                                                               ║
║          dm-remap v4.0 Phase 1 - Interactive Demo            ║
║                                                               ║
║              Bad Sector Remapping Made Easy                   ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝

ℹ This demo will showcase dm-remap v4.0 features:
  ✓ Priority 3: External Spare Device Support
  ✓ Priority 6: Automatic Setup Reassembly
  ✓ Transparent I/O operations
  ✓ Real-time monitoring

Demo environment:
  • Main device:  /tmp/dm-remap-demo/main_device.img (200MB)
  • Spare device: /tmp/dm-remap-demo/spare_device.img (212MB)
  • Duration:     ~60 seconds

Press ENTER to start the demo...
```

## Post-Demo Exploration

After the demo completes, the dm-remap device remains active. You can:

### Manual Testing

```bash
# Write your own test data
sudo dd if=/dev/urandom of=/dev/mapper/dm-remap-demo bs=1M count=10

# Read it back
sudo dd if=/dev/mapper/dm-remap-demo of=/tmp/test.dat bs=1M count=10

# Check device status
sudo dmsetup status dm-remap-demo

# View device table
sudo dmsetup table dm-remap-demo

# Check kernel messages
sudo dmesg | grep "dm-remap v4" | tail -20
```

### Manual Cleanup

If you chose to explore after the demo, clean up manually:

```bash
# Remove dm device
sudo dmsetup remove dm-remap-demo

# Unload module
sudo rmmod dm-remap-v4-real

# Remove loop devices
sudo losetup -D

# Remove demo files
sudo rm -rf /tmp/dm-remap-demo
```

## Troubleshooting

### "Module not found" Error

Make sure you've built the modules first:

```bash
cd src/
make clean
make
```

### "Cannot create device" Error

Check kernel messages for details:

```bash
sudo dmesg | tail -20
```

Common issues:
- Spare device too small (must be ≥5% larger than main)
- Loop devices already in use
- dm-remap target not loaded

### "Permission denied" Error

The demo requires root privileges:

```bash
sudo ./demos/v4_interactive_demo.sh
```

## Educational Value

This demo is perfect for:

- **Learning** how dm-remap works
- **Teaching** device-mapper concepts
- **Demonstrating** v4.0 features to others
- **Testing** your dm-remap installation
- **Validating** build and configuration

## Technical Details

### Device Sizes

- **Main Device**: 200MB
- **Spare Device**: 212MB (6% overhead)
- **Overhead Calculation**: 200MB × 1.06 = 212MB

**Note**: The current implementation (v4.0 Phase 1) conservatively requires the spare device to be ≥5% larger than the main device. This is a known limitation. In a production-optimized implementation, the spare device could be **much smaller** (e.g., 10-50GB for a 1TB main device), as it only needs to store:
- Metadata (remapping tables, sector maps)
- Actual remapped bad sectors

This sizing requirement will be optimized in future releases (Priority 4). For now, the demo uses similar-sized devices to meet the current validation requirements.

### Performance Expectations

Typical results on modern hardware:
- **Sequential Write**: 100-500 MB/s (depends on disk)
- **Sequential Read**: 100-500 MB/s (depends on disk)
- **Overhead**: Minimal (<1% for normal operations)

### Files Created

All demo files are in `/tmp/dm-remap-demo/`:
- `main_device.img` - Main device image (200MB)
- `spare_device.img` - Spare device image (212MB)

These are automatically cleaned up when the demo exits.

## Customization

You can modify the demo by editing these variables at the top of the script:

```bash
DEMO_SIZE_MB=200        # Main device size
SPARE_SIZE_MB=212       # Spare device size (≥ main × 1.05)
DELAY=0.8              # Animation delay between steps
```

## Use Cases Demonstrated

1. **Data Protection**: Transparent remapping protects against bad sectors
2. **Spare Pool**: External spare device provides remapping capacity
3. **Zero Configuration**: Automatic setup and management
4. **Performance**: Minimal overhead during normal operation
5. **Monitoring**: Real-time status and statistics

## Next Steps

After running the demo:

1. **Read the Documentation**: See `docs/user/QUICK_START.md` (coming soon)
2. **Try Real Hardware**: Test with actual storage devices
3. **Explore Features**: Experiment with different configurations
4. **Run Tests**: Execute the test suite in `tests/`
5. **Contribute**: Help improve dm-remap!

## Demo Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Demo Script                              │
│  (v4_interactive_demo.sh)                                   │
└────────────┬────────────────────────────────────────────────┘
             │
             ├── Creates Test Environment
             │   └── /tmp/dm-remap-demo/
             │       ├── main_device.img (200MB)
             │       └── spare_device.img (212MB)
             │
             ├── Sets Up Loop Devices
             │   ├── /dev/loop0 → main_device.img
             │   └── /dev/loop1 → spare_device.img
             │
             ├── Loads dm-remap Module
             │   └── dm-remap-v4-real.ko
             │
             ├── Creates dm Device
             │   └── /dev/mapper/dm-remap-demo
             │       ├── Main: /dev/loop0
             │       └── Spare: /dev/loop1
             │
             ├── Performs I/O Tests
             │   ├── Write: 50MB random data
             │   ├── Read: 50MB verification
             │   └── Random: Mixed pattern
             │
             ├── Shows Status
             │   ├── dmsetup status
             │   ├── Kernel messages
             │   └── Module info
             │
             ├── Benchmarks Performance
             │   ├── Sequential write
             │   └── Sequential read
             │
             └── Cleanup
                 ├── Remove dm device
                 ├── Unload module
                 ├── Remove loop devices
                 └── Delete temp files
```

## Feedback

Found an issue or have suggestions? Please contribute:
- Open an issue on GitHub
- Submit a pull request
- Contact the maintainer

---

**Enjoy exploring dm-remap v4.0!** 🚀
