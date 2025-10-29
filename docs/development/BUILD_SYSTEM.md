# dm-remap Build System

**Flexible module compilation with two deployment modes**

> This document explains the two build modes available in dm-remap and how to use them.

---

## Table of Contents

- [Overview](#overview)
- [Build Modes](#build-modes)
- [Quick Reference](#quick-reference)
- [INTEGRATED Mode (Default)](#integrated-mode-default)
- [MODULAR Mode](#modular-mode)
- [Comparison](#comparison)
- [Usage Examples](#usage-examples)
- [Advanced Configuration](#advanced-configuration)

---

## Overview

The dm-remap build system provides **two flexible deployment options** compiled from the same source code:

1. **INTEGRATED** (default) - Single kernel module with all features
2. **MODULAR** - Separate kernel modules for independent loading

Both modes produce identical core functionality with the same O(1) performance characteristics. The choice between them depends on your deployment needs and preferences.

### Design Philosophy

The flexible build system is designed to support:
- **Production deployments** - INTEGRATED for simplicity and stability
- **Development workflows** - MODULAR for independent module testing
- **Enterprise environments** - Either mode based on infrastructure requirements
- **Embedded systems** - INTEGRATED for minimal footprint

---

## Build Modes

### INTEGRATED Mode (Default)

**Single dm-remap.ko with all features compiled together**

```
make clean && make
```

**Produces:**
- `dm-remap.ko` (2.7 MB) - Complete remapping engine with all features
- `dm-remap-test.ko` (373 KB) - Test module

**Module components (compiled in):**
- Core remapping engine (hash table, O(1) lookups)
- Metadata management (creation, utils, repair)
- Spare pool management
- Device setup and reassembly
- Statistics collection

**Recommended for:**
- ✅ Production deployments
- ✅ Standard Linux installations
- ✅ Minimal complexity deployments
- ✅ Enterprise environments
- ✅ Most users

---

### MODULAR Mode

**Separate .ko files for independent feature modules**

```
BUILD_MODE=MODULAR make clean && BUILD_MODE=MODULAR make
```

**Produces:**
- `dm-remap.ko` (1.5 MB) - Core remapping engine (required)
- `dm-remap-spare-pool.ko` (457 KB) - Spare pool management (optional)
- `dm-remap-setup-reassembly.ko` (919 KB) - Device setup (optional)
- `dm-remap-stats.ko` (330 KB) - Statistics collection (optional)
- `dm-remap-test.ko` (373 KB) - Test module (optional)

**Total size:** 3.6 MB (slightly larger, but more granular)

**Recommended for:**
- ✅ Development and testing
- ✅ Troubleshooting individual components
- ✅ Advanced users with specific needs
- ✅ Environments requiring hot-unload of features
- ✅ Custom deployments with selective features

---

## Quick Reference

### Build Defaults

```bash
# Build integrated mode (default)
make clean
make

# Build modular mode
BUILD_MODE=MODULAR make clean
BUILD_MODE=MODULAR make

# Check current build mode
make show-build-mode

# Clean build artifacts
make clean
```

### Loading Modules

**INTEGRATED Mode:**
```bash
# Load single module
sudo modprobe dm_remap

# Verify
sudo dmsetup targets | grep remap
# Output: remap v1.0.0
```

**MODULAR Mode:**
```bash
# Load core (automatically loads dependencies)
sudo modprobe dm_remap

# Optionally load additional features
sudo modprobe dm_remap_spare_pool
sudo modprobe dm_remap_setup_reassembly
sudo modprobe dm_remap_stats

# Verify all loaded
lsmod | grep dm_remap
```

---

## INTEGRATED Mode (Default)

### Overview

Single compiled kernel module containing all dm-remap functionality. This is the **recommended approach for most users and production deployments**.

### Components Included

```
dm-remap.ko
├── Core Remapping Engine
│   ├── Hash table (O(1) lookups)
│   ├── Dynamic resize logic
│   ├── Block remapping
│   └── IO handling
│
├── Metadata Management
│   ├── Metadata creation
│   ├── Metadata utilities
│   ├── Repair procedures
│   └── Persistence logic
│
├── Spare Pool Management
│   ├── Spare device handling
│   ├── Pool tracking
│   └── Automatic failover
│
├── Setup & Reassembly
│   ├── Device initialization
│   ├── Discovery procedures
│   └── Storage management
│
└── Statistics Collection
    ├── Performance metrics
    ├── Event tracking
    └── Health monitoring
```

### Advantages

| Advantage | Benefit |
|-----------|---------|
| **Single file** | Easy installation and deployment |
| **No dependencies** | No need to manage module loading order |
| **Standard practice** | Follows Linux kernel conventions |
| **Simpler debugging** | All code in single module context |
| **Atomic updates** | Update everything at once |
| **Production proven** | Tested approach used in most Linux kernels |

### Installation

```bash
# Build
cd /home/christian/kernel_dev/dm-remap
make clean
make

# Install
sudo make install

# Load
sudo modprobe dm_remap

# Verify
sudo dmsetup targets | grep remap
```

### Module Information

```bash
# Show module details
modinfo src/dm-remap.ko

# Expected output:
# filename:       /lib/modules/.../dm-remap.ko
# version:        1.0.0
# description:    Device Mapper Remapping Target
# license:        GPL
# srcversion:     [hash]
# depends:        dm-bufio
```

### Unloading

```bash
# Remove device
sudo dmsetup remove my-remap

# Unload module
sudo modprobe -r dm_remap

# Verify
lsmod | grep dm_remap
# Should show empty
```

---

## MODULAR Mode

### Overview

Multiple separate kernel modules, each handling a specific aspect of dm-remap functionality. Useful for development, testing, and advanced deployments.

### Module Breakdown

#### Core Module (Required)

**dm-remap.ko** (1.5 MB)
```
Core Remapping Engine
├── Hash table implementation
├── O(1) lookup logic
├── Block remapping
├── Dynamic resize
├── Metadata persistence
├── Basic repair procedures
└── Device target registration
```

Required by all other modules. Always load first.

#### Optional Feature Modules

**dm-remap-spare-pool.ko** (457 KB)
```
Spare Pool Management
├── Spare device tracking
├── Pool allocation
├── Automatic failover logic
└── Recovery procedures
```

Load if using spare device functionality.

**dm-remap-setup-reassembly.ko** (919 KB)
```
Setup & Reassembly
├── Device initialization
├── Discovery procedures
├── Storage management
├── Reassembly logic
└── Configuration helpers
```

Load if using device setup features.

**dm-remap-stats.ko** (330 KB)
```
Statistics Collection
├── Performance metrics
├── Event tracking
├── Health monitoring
├── Diagnostic information
└── Real-time statistics
```

Load for monitoring and diagnostics.

**dm-remap-test.ko** (373 KB)
```
Test Module
├── Unit tests
├── Functional tests
├── Integration tests
└── Validation routines
```

Load for testing purposes only.

### Advantages

| Advantage | Benefit |
|-----------|---------|
| **Modularity** | Load only what you need |
| **Development** | Test components independently |
| **Debugging** | Isolate issues to specific modules |
| **Hot-unload** | Remove features without rebooting |
| **Custom builds** | Exclude unnecessary features |
| **Version control** | Update modules independently |

### Installation

```bash
# Build with modular mode
cd /home/christian/kernel_dev/dm-remap
BUILD_MODE=MODULAR make clean
BUILD_MODE=MODULAR make

# Install
sudo make install

# Load core module
sudo modprobe dm_remap

# Load optional features
sudo modprobe dm_remap_spare_pool
sudo modprobe dm_remap_setup_reassembly
sudo modprobe dm_remap_stats

# Verify
lsmod | grep dm_remap
```

### Loading Order

When using modular mode, generally follow this order:

```bash
# 1. Core module (required)
sudo modprobe dm_remap

# 2. Metadata and repair (dependencies for other modules)
# (Automatically loaded as dependencies)

# 3. Feature modules (optional, any order)
sudo modprobe dm_remap_spare_pool
sudo modprobe dm_remap_setup_reassembly
sudo modprobe dm_remap_stats

# 4. Test module (only for testing)
sudo modprobe dm_remap_test
```

### Checking Loaded Modules

```bash
# Show all dm_remap modules
lsmod | grep dm_remap

# Expected in full modular mode:
# dm_remap_stats          330
# dm_remap_setup_reassembly  919
# dm_remap_spare_pool      457
# dm_remap                1500
```

### Unloading

```bash
# Remove device first
sudo dmsetup remove my-remap

# Unload in reverse order (dependencies)
sudo modprobe -r dm_remap_test
sudo modprobe -r dm_remap_stats
sudo modprobe -r dm_remap_setup_reassembly
sudo modprobe -r dm_remap_spare_pool
sudo modprobe -r dm_remap

# Verify all unloaded
lsmod | grep dm_remap
# Should show empty
```

---

## Comparison

### Feature Matrix

| Feature | INTEGRATED | MODULAR |
|---------|-----------|---------|
| Core remapping | ✓ | ✓ |
| Metadata management | ✓ | ✓ |
| Spare pool | ✓ | ✓ (optional) |
| Device setup | ✓ | ✓ (optional) |
| Statistics | ✓ | ✓ (optional) |
| Testing | ✓ | ✓ (optional) |
| **Performance** | ✓ O(1) | ✓ O(1) |
| **Capacity** | 4.2B remaps | 4.2B remaps |

### Size Comparison

```
INTEGRATED Mode:
  dm-remap.ko          2.7 MB  (all features)
  dm-remap-test.ko     373 KB
  Total:               3.1 MB

MODULAR Mode:
  dm-remap.ko          1.5 MB  (core only)
  dm-remap-spare-pool.ko       457 KB
  dm-remap-setup-reassembly.ko 919 KB
  dm-remap-stats.ko    330 KB
  dm-remap-test.ko     373 KB
  Total:               3.6 MB
```

### Installation Complexity

```
INTEGRATED:
  make && sudo make install
  sudo modprobe dm_remap
  Done!

MODULAR:
  BUILD_MODE=MODULAR make && sudo make install
  sudo modprobe dm_remap
  sudo modprobe dm_remap_spare_pool  (optional)
  sudo modprobe dm_remap_setup_reassembly  (optional)
  sudo modprobe dm_remap_stats  (optional)
```

### Use Case Recommendations

| Scenario | Recommended | Reason |
|----------|-------------|--------|
| **Production server** | INTEGRATED | Simplicity, stability, proven approach |
| **Enterprise data center** | INTEGRATED | Standard practice, easier management |
| **Development/testing** | MODULAR | Component isolation, flexible testing |
| **Embedded/minimal** | INTEGRATED | Single file, complete functionality |
| **Troubleshooting** | MODULAR | Load/unload specific features |
| **Custom deployment** | Either | Depends on specific requirements |

---

## Usage Examples

### Example 1: Production INTEGRATED Build

```bash
# Setup
cd /home/christian/kernel_dev/dm-remap
make clean
make

# Install
sudo make install

# Load
sudo modprobe dm_remap

# Create device
MAIN=/dev/sda
SPARE=/dev/sdb
TABLE="0 $(blockdev --getsz $MAIN) dm-remap $MAIN $SPARE"
echo "$TABLE" | sudo dmsetup create my-storage

# Use device
sudo mkfs.ext4 /dev/mapper/my-storage
sudo mount /dev/mapper/my-storage /mnt/storage

# Verify running
sudo dmsetup status my-storage
sudo dmsetup message my-storage 0 "stats"
```

### Example 2: Development MODULAR Build

```bash
# Setup with modular mode
cd /home/christian/kernel_dev/dm-remap
BUILD_MODE=MODULAR make clean
BUILD_MODE=MODULAR make

# Install
sudo make install

# Load only core for basic testing
sudo modprobe dm_remap

# Create test device
TABLE="0 1000000 dm-remap /dev/loop0 /dev/loop1"
echo "$TABLE" | sudo dmsetup create test-dev

# Add some remaps
sudo dmsetup message test-dev 0 "add_remap 0 5000 100"
sudo dmsetup message test-dev 0 "add_remap 1000 6000 50"

# Now load statistics module to monitor
sudo modprobe dm_remap_stats

# Get detailed stats
sudo dmsetup message test-dev 0 "stats"

# Unload to test independently
sudo modprobe -r dm_remap_stats
```

### Example 3: Switching Between Modes

```bash
# Currently have integrated mode loaded
sudo modprobe -r dm_remap

# Build and switch to modular
cd /home/christian/kernel_dev/dm-remap
BUILD_MODE=MODULAR make clean
BUILD_MODE=MODULAR make
sudo make install

# Load modular version
sudo modprobe dm_remap

# To switch back to integrated
sudo modprobe -r dm_remap
make clean
make
sudo make install
sudo modprobe dm_remap
```

---

## Advanced Configuration

### Custom Kernel Compatibility

The build system automatically handles kernel version compatibility via `compat.h`:

```bash
# Verify compatibility with current kernel
cat src/compat.h | grep -E "#if|#else|#endif|KERNEL_VERSION"

# Build will select appropriate code paths
make show-build-mode
make
```

### Build-Time Options

```bash
# Enable debug logging
ccflags-y += -DDM_REMAP_DEBUG

# Specific to your architecture
ccflags-y += -march=native

# Custom kernel source
KDIR=/path/to/custom/kernel make

# Specific kernel version
KDIR=/lib/modules/5.10.0-generic/build make
```

### Makefile Inspection

View the current build configuration:

```bash
cat src/Makefile | grep -A 20 "BUILD_MODE"
```

Expected output shows which modules are selected for current BUILD_MODE:

```makefile
BUILD_MODE ?= INTEGRATED

ifeq ($(BUILD_MODE),INTEGRATED)
  obj-m := dm-remap.o dm-remap-test.o
  dm-remap-objs := dm-remap-core.o dm-remap-v4-metadata.o ... (10 files)
else ifeq ($(BUILD_MODE),MODULAR)
  obj-m := dm-remap.o dm-remap-test.o
  obj-m += dm-remap-spare-pool.o
  obj-m += dm-remap-setup-reassembly.o
  obj-m += dm-remap-stats.o
endif
```

### Environment Variables

```bash
# Set build mode (overrides default)
export BUILD_MODE=MODULAR
make

# Set kernel directory
export KDIR=/lib/modules/$(uname -r)/build
make

# Set build verbosity
export V=1
make

# Combined example
export BUILD_MODE=MODULAR V=1
make clean && make
```

### Troubleshooting Build Issues

```bash
# Check kernel compatibility
uname -r
ls -la /lib/modules/$(uname -r)/build

# View compiler flags
cat src/Makefile | grep ccflags

# Try explicit kernel path
KDIR=/lib/modules/$(uname -r)/build make

# Check for errors
make 2>&1 | head -50

# Clean and retry
make clean
make -j1  # Single job to see all output
```

---

## Decision Matrix

Use this table to choose the right build mode:

```
Question                           Answer      Recommendation
─────────────────────────────────────────────────────────────
Is this for production?             Yes         → INTEGRATED
Need to test components separately? Yes         → MODULAR
Want simplest installation?         Yes         → INTEGRATED
Minimal disk space important?       Yes         → INTEGRATED
Development/research project?       Yes         → MODULAR
Need to load features independently? Yes         → MODULAR
Following Linux kernel standards?   Yes         → INTEGRATED
First time using dm-remap?          Yes         → INTEGRATED
```

---

## Common Tasks

### Verify Build Mode

```bash
# Before building
make show-build-mode

# After building, check loaded modules
lsmod | grep dm_remap
```

### Rebuild with Different Mode

```bash
# Clean first
make clean

# Set mode and rebuild
BUILD_MODE=MODULAR make

# Verify
make show-build-mode
```

### Check Module Dependencies

```bash
# For integrated mode
modinfo src/dm-remap.ko | grep depends

# For modular mode
for mod in src/dm-remap*.ko; do
  echo "=== $(basename $mod) ==="
  modinfo "$mod" | grep depends
done
```

### Performance Testing Both Modes

```bash
# Build and test INTEGRATED
make clean && make
sudo make install
# Run performance tests
bash tests/validate_hash_resize.sh

# Build and test MODULAR
BUILD_MODE=MODULAR make clean
BUILD_MODE=MODULAR make
sudo make install
# Run same tests
bash tests/validate_hash_resize.sh

# Results should be identical (O(1) performance)
```

---

## FAQ

**Q: Which mode should I use?**
A: Use INTEGRATED for production (recommended). Use MODULAR for development or if you have specific needs.

**Q: Can I switch between modes?**
A: Yes, unload modules, rebuild with different BUILD_MODE, and reinstall.

**Q: What's the performance difference?**
A: None - both modes use identical core code and have O(1) performance.

**Q: Can I load both modes simultaneously?**
A: No, choose one mode per system. Switching requires unload, rebuild, and reinstall.

**Q: Which is more reliable?**
A: Both are equally reliable - INTEGRATED is proven standard practice, MODULAR is tested separately.

**Q: Do I need all optional modules in MODULAR mode?**
A: No, load only what you need. Core (dm-remap.ko) is always required.

**Q: Can I customize which features are included?**
A: Yes, edit src/Makefile and comment out unwanted feature objects.

---

## See Also

- [README.md](../../README.md) - Project overview
- [QUICK_START.md](../user/QUICK_START.md) - Get started quickly
- [Makefile](../../src/Makefile) - Build configuration source
- [ARCHITECTURE.md](./ARCHITECTURE.md) - System architecture details
