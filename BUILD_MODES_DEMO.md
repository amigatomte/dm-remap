# Build Mode Demonstration

**Date:** October 29, 2025  
**Module Naming:** Simplified (removed v4 version from names)

---

## Module Naming Updates

All modules have been renamed to remove the `v4` version designation:

| Old Name | New Name |
|----------|----------|
| `dm-remap-v4-real.ko` | `dm-remap.ko` |
| `dm-remap-minimal-test.ko` | `dm-remap-test.ko` |
| `dm-remap-v4-spare-pool.ko` | `dm-remap-spare-pool.ko` |
| `dm-remap-v4-setup-reassembly.ko` | `dm-remap-setup-reassembly.ko` |
| `dm-remap-v4-stats.ko` | `dm-remap-stats.ko` |

**Rationale:** Version numbers belong in module metadata, not file names. Clean names are more maintainable and easier to reference.

---

## Build Mode 1: INTEGRATED (Default)

### Command
```bash
make
# or explicitly:
make clean
make
```

### Configuration Output
```
==========================================
dm-remap Build Configuration
==========================================
BUILD_MODE: INTEGRATED
Output: dm-remap.ko (single module)
Features: All core features compiled in

Benefits:
  ✓ Simple installation (single file)
  ✓ No dependency management
  ✓ Standard Linux practice
  ✓ Recommended for production
==========================================
```

### Result: Single Monolithic Module
```
-rw-rw-r-- 2.7M dm-remap.ko          ← All features combined
-rw-rw-r-- 373K dm-remap-test.ko      ← Test module
```

### Module Information
```bash
$ modinfo src/dm-remap.ko
filename:       dm-remap.ko
version:        1.0.0
license:        GPL
author:         dm-remap Development Team
description:    Device Mapper Remapping Target - Block remapping with O(1) 
                performance
```

### Features Included
- ✅ Core remapping engine (2.7 MB)
- ✅ Hash table with dynamic resizing
- ✅ Metadata management
- ✅ Spare pool management
- ✅ Device setup & reassembly
- ✅ Statistics collection
- ✅ Health monitoring hooks

### Installation
```bash
# Single command
sudo insmod src/dm-remap.ko

# Or via modprobe
sudo modprobe -f dm-remap
```

### Use Case
- **Production deployments** (recommended)
- Enterprise storage systems
- Simplest installation path
- Lowest maintenance overhead

---

## Build Mode 2: MODULAR (Experimental)

### Command
```bash
make clean
BUILD_MODE=MODULAR make
```

### Configuration Output
```
==========================================
dm-remap Build Configuration
==========================================
BUILD_MODE: MODULAR
Output: Multiple kernel modules
Modules:
  • dm-remap.ko (core remapping engine)
  • dm-remap-spare-pool.ko (spare device management)
  • dm-remap-setup-reassembly.ko (device setup)
  • dm-remap-stats.ko (statistics collection)

Benefits:
  ✓ Load features independently
  ✓ Easier testing and troubleshooting
  ✓ Update components separately
==========================================
```

### Result: Multiple Separate Modules
```
-rw-rw-r-- 1.5M dm-remap.ko                    ← Core engine
-rw-rw-r--  919K dm-remap-setup-reassembly.ko ← Setup & reassembly
-rw-rw-r--  457K dm-remap-spare-pool.ko        ← Spare pool
-rw-rw-r--  330K dm-remap-stats.ko             ← Statistics
-rw-rw-r--  373K dm-remap-test.ko              ← Test module
```

**Total:** 5 modules vs. 1 module (INTEGRATED)

### Module Breakdown

**dm-remap.ko (1.5 MB) - Core Engine**
```
Includes:
  • Core remapping logic
  • Metadata management (read/write/repair)
  • Basic initialization
```

**dm-remap-spare-pool.ko (457 KB) - Spare Device Management**
```
Includes:
  • Spare pool tracking
  • Device allocation
  • Free space management
```

**dm-remap-setup-reassembly.ko (919 KB) - Device Setup**
```
Includes:
  • Device discovery
  • Setup and reassembly logic
  • Metadata storage/recovery
```

**dm-remap-stats.ko (330 KB) - Statistics**
```
Includes:
  • Performance metrics collection
  • Health scoring
  • sysfs export interfaces
```

**dm-remap-test.ko (373 KB) - Test Module**
```
Minimal test target for validation
```

### Installation (MODULAR)
```bash
# Load in dependency order
sudo insmod src/dm-remap.ko
sudo insmod src/dm-remap-spare-pool.ko
sudo insmod src/dm-remap-setup-reassembly.ko
sudo insmod src/dm-remap-stats.ko

# Or with modprobe (automatic dependency resolution)
sudo modprobe -f dm-remap
sudo modprobe -f dm-remap-spare-pool
sudo modprobe -f dm-remap-setup-reassembly
sudo modprobe -f dm-remap-stats
```

### Use Cases
- **Development & debugging** (test individual components)
- **Embedded systems** (load only needed features)
- **Advanced troubleshooting** (isolate functionality)
- **Learning** (understand module architecture)
- **Performance testing** (measure component overhead)

---

## Side-by-Side Comparison

| Feature | INTEGRATED | MODULAR |
|---------|-----------|---------|
| **Total Size** | 2.7 MB | 3.6 MB |
| **Core Module** | dm-remap.ko | dm-remap.ko |
| **Installation** | 1 command | 4+ commands |
| **Dependencies** | None | Cross-module symbols |
| **Flexibility** | Low (monolithic) | High (modular) |
| **Maintenance** | Simple | Complex |
| **Deployment** | Easy | Requires planning |
| **Debugging** | Harder | Easier (isolate) |
| **Recommended** | ✅ YES | 🔧 Experimental |

---

## Technical Details: Module Metadata

### INTEGRATED Mode
```c
MODULE_DESCRIPTION("Device Mapper Remapping Target - Block remapping with O(1) performance");
MODULE_VERSION("1.0.0");
MODULE_SOFTDEP("pre: dm-bufio");
```

### MODULAR Mode (per module)
```c
// dm-remap.ko (core)
MODULE_DESCRIPTION("dm-remap core remapping engine");
MODULE_VERSION("1.0.0");

// dm-remap-spare-pool.ko (features)
MODULE_DESCRIPTION("dm-remap spare device pool management");
EXPORT_SYMBOL(spare_pool_init);
EXPORT_SYMBOL(spare_pool_allocate);
// ... other symbols
```

---

## Source Files

### Main Source Files (Renamed)
```
src/dm-remap-core.c         ← dm-remap.ko (was dm-remap-v4-real-main.c)
src/dm-remap-test.c         ← dm-remap-test.ko (was dm-remap-minimal-test.c)
```

### Component Files (Keep version in name - internal)
```
src/dm-remap-v4-metadata.c
src/dm-remap-v4-metadata-creation.c
src/dm-remap-v4-metadata-utils.c
src/dm-remap-v4-repair.c
src/dm-remap-v4-spare-pool.c
src/dm-remap-v4-setup-reassembly-core.c
src/dm-remap-v4-setup-reassembly-storage.c
src/dm-remap-v4-setup-reassembly-discovery.c
src/dm-remap-v4-stats.c
```

**Note:** Component files keep "v4" in names because they're internal implementation files, not exported modules. Only user-facing module names were simplified.

---

## Build System Files

```
Makefile                    ← Root makefile (simplified)
src/Makefile               ← Build configuration (flexible)
```

### Makefile Logic (src/Makefile)
```makefile
BUILD_MODE ?= INTEGRATED

ifeq ($(BUILD_MODE),INTEGRATED)
    # Single monolithic build
    obj-m := dm-remap.o dm-remap-test.o
    dm-remap-objs := dm-remap-core.o dm-remap-v4-metadata.o ... (all components)
else ifeq ($(BUILD_MODE),MODULAR)
    # Separate modular build
    obj-m := dm-remap.o dm-remap-test.o
    obj-m += dm-remap-spare-pool.o
    obj-m += dm-remap-setup-reassembly.o
    obj-m += dm-remap-stats.o
    # Each module defined separately
endif
```

---

## Quick Reference

### Switch Between Modes
```bash
# Current mode (INTEGRATED)
make              # Builds INTEGRATED by default
ls -lh src/*.ko   # Shows: 1 × 2.7 MB dm-remap.ko

# Switch to MODULAR
make clean
BUILD_MODE=MODULAR make
ls -lh src/*.ko   # Shows: 5 modules (1.5M + 457K + 919K + 330K + 373K)

# Back to INTEGRATED
make clean
make
```

### Module Information
```bash
# INTEGRATED
modinfo src/dm-remap.ko | grep "^description"
# Output: Device Mapper Remapping Target - Block remapping with O(1) performance

# MODULAR (core)
modinfo src/dm-remap.ko | grep "^description"
# Output: dm-remap core remapping engine
```

---

## Documentation Updates

All documentation has been updated to reflect:
- ✅ New module names (without v4)
- ✅ Build mode options (INTEGRATED/MODULAR)
- ✅ Version 1.0.0 (initial release)
- ✅ Pre-release status clarification

**See:** BUILD.md for detailed build documentation

---

## Key Points

1. **Naming Simplified:** `v4` removed from module names - version in metadata only
2. **INTEGRATED Default:** Recommended for production (simple, clean, fast)
3. **MODULAR Option:** Available for advanced use (flexible, experimental)
4. **No Code Changes:** Only build system and naming updated
5. **Backward Compatible:** Same functionality, cleaner interface

---

**Status:** ✅ READY FOR DEMONSTRATION

Both build modes working and tested. Module names simplified for production readiness.
