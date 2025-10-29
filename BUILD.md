# dm-remap Build Configuration

## Overview

dm-remap supports **two build modes**:

1. **INTEGRATED** (default) - Single `dm-remap-v4-real.ko` with all features
2. **MODULAR** (experimental) - Separate `.ko` files for independent modules

---

## Default Build (INTEGRATED)

The **INTEGRATED** build is the default and recommended for production.

### Quick Build

```bash
# Clone and build (uses INTEGRATED by default)
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap
make
sudo make install

# Load module
sudo modprobe dm_remap_v4_real
```

### What's Included

Single `dm-remap-v4-real.ko` module (~2.7 MB) contains:

- ✅ Core remapping engine
- ✅ Metadata management
- ✅ Spare pool management
- ✅ Device setup & reassembly
- ✅ Statistics collection

### Benefits of INTEGRATED

| Benefit | Details |
|---------|---------|
| **Simple** | Single `.ko` file to manage |
| **No dependencies** | All features in one module |
| **Standard** | Follows Linux kernel practice |
| **Production-ready** | Recommended for deployment |
| **Easy deployment** | Single `insmod dm_remap_v4_real.ko` |

---

## Alternative: Modular Build

The **MODULAR** build creates separate modules for each feature component.

### Experimental Status

⚠️ **Note:** MODULAR build is currently **experimental** and requires additional symbol export setup. Use INTEGRATED for production.

### Enable Modular Build

```bash
make clean
BUILD_MODE=MODULAR make

# Creates multiple modules:
# - dm-remap-v4-real.ko         (core remapping engine)
# - dm-remap-v4-spare-pool.ko   (spare pool management)
# - dm-remap-v4-setup-reassembly.ko (device setup)
# - dm-remap-v4-stats.ko        (statistics collection)
```

### Benefits of MODULAR

| Benefit | Use Case |
|---------|----------|
| **Independent loading** | Load/unload features separately |
| **Easier debugging** | Test individual components |
| **Selective features** | Only load what you need |
| **Update components** | Update one module without rebuilding all |

### Known Issues

- Requires additional EXPORT_SYMBOL setup in standalone files
- Symbol dependencies not yet fully configured
- Not recommended for production yet

---

## Build System Details

### Build Mode Parameter

```bash
# Explicit mode (INTEGRATED is default)
make                          # Uses INTEGRATED (default)
BUILD_MODE=INTEGRATED make    # Explicitly set INTEGRATED
BUILD_MODE=MODULAR make       # Switch to MODULAR mode
```

### Configuration

Build configuration controlled in `src/Makefile`:

```makefile
BUILD_MODE ?= INTEGRATED

ifeq ($(BUILD_MODE),INTEGRATED)
  # Single module with all features
  obj-m := dm-remap-v4-real.o
  dm-remap-v4-real-objs := \
      dm-remap-v4-real-main.o \
      dm-remap-v4-metadata.o \
      ... (all .o files combined)

else ifeq ($(BUILD_MODE),MODULAR)
  # Separate modules
  obj-m := dm-remap-v4-real.o
  obj-m += dm-remap-v4-spare-pool.o
  obj-m += dm-remap-v4-setup-reassembly.o
  obj-m += dm-remap-v4-stats.o
  ...
endif
```

---

## Compilation & Installation

### Standard Installation (INTEGRATED)

```bash
# Build
make clean
make

# Install (copies .ko to system module path)
sudo make install

# Load module
sudo modprobe dm_remap_v4_real

# Verify
sudo dmsetup targets | grep remap
```

### Advanced: Manual Installation

```bash
# Build only (no installation)
make

# Manual copy to kernel modules path
sudo cp src/dm-remap-v4-real.ko /lib/modules/$(uname -r)/kernel/drivers/md/

# Update module dependencies
sudo depmod -a

# Load
sudo modprobe dm_remap_v4_real
```

### Test Build

```bash
# Build without installing
make clean
make

# Load from build directory
sudo insmod src/dm-remap-v4-real.ko

# Unload
sudo rmmod dm_remap_v4_real
```

---

## Module Information

### INTEGRATED Module Details

```bash
# View module info
modinfo src/dm-remap-v4-real.ko

# Expected output
filename:       .../dm-remap-v4-real.ko
version:        4.0.0-real
license:        GPL
author:         dm-remap Development Team
description:    Device Mapper Remapping Target v4.0
```

### Module Dependencies

```bash
# Check dependencies
modinfo -d src/dm-remap-v4-real.ko

# Expected pre-dependencies:
# dm-bufio (buffer I/O manager)
# dm-remap-v4-metadata (metadata management)
```

---

## Troubleshooting

### Build Fails with "Module.symvers"

If you see errors about undefined symbols:

```bash
# Clean and rebuild
make clean
make
```

### Module Won't Load

```bash
# Check kernel log
dmesg | tail -20

# Verify module is in correct location
ls -l /lib/modules/$(uname -r)/kernel/drivers/md/

# Manually load with debug
sudo insmod src/dm-remap-v4-real.ko -v
```

### Symbol Export Issues

If you see "undefined reference to 'dm_remap_stats_inc_reads'":

- This occurs in MODULAR mode when symbols aren't exported
- Use INTEGRATED mode (default) as workaround
- MODULAR mode is still experimental

---

## Version Information

- **Current Build Mode Default:** INTEGRATED
- **Kernel Module Version:** 4.0.0-real
- **License:** GPL v2
- **Status:** Production ready (INTEGRATED), Experimental (MODULAR)

---

## Architecture Decision: Why INTEGRATED is Default

### Technical Rationale

1. **Symbol Management**: The statistics, spare pool, and setup modules provide helper functions to the core remapping engine. They don't have independent module init/exit.

2. **Logical Grouping**: All components work together as a single unified device mapper target.

3. **Deployment Simplicity**: Single module is easier to deploy, track, and version in production.

4. **Standard Practice**: Most kernel drivers use multi-object builds for related components.

### When to Use MODULAR (Future)

MODULAR mode would be useful for:
- Embedded systems with memory constraints
- Advanced troubleshooting in development
- Learning kernel module architecture
- Integration with specialized deployments

Currently requires fixing symbol export macros - planned for future release.

---

## See Also

- [Installation Guide](docs/user/INSTALLATION.md) - Step-by-step setup
- [User Guide](docs/user/USER_GUIDE.md) - Usage and configuration
- [Architecture Guide](docs/development/ARCHITECTURE.md) - Technical details
- [Quick Start](docs/user/QUICK_START.md) - 5-minute tutorial

---

**Build System Version:** 1.0  
**Last Updated:** October 29, 2025  
**Status:** PRODUCTION READY ✅
