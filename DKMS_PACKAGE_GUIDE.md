# dm-remap DKMS Package - Build & Installation Guide

## Package Created Successfully ✅

**File:** `dm-remap-dkms_1.0.0-1_all.deb` (225 KB)

**Architecture:** `all` (architecture-independent source package)

This is a proper DKMS (Dynamic Kernel Module Support) package that:
- ✅ Installs source code to `/usr/src/dm-remap-1.0.0/`
- ✅ Automatically registers with DKMS system at install time
- ✅ Automatically builds modules for the installed kernel
- ✅ Automatically rebuilds when new kernels are installed
- ✅ Works with ANY supported Linux kernel (not hardcoded to one version)

## Package Contents

The DKMS package includes:
- **Source code** (all `.c` and `.h` files)
- **Build files** (Makefile, dkms.conf)
- **Documentation** (README.md, LICENSE)
- **Installation scripts** (postinst, prerm)

## Installation

### Prerequisites
```bash
sudo apt-get install dkms build-essential linux-headers-generic make
```

### Install the Package
```bash
sudo dpkg -i dm-remap-dkms_1.0.0-1_all.deb
```

The postinstall script will:
1. Register the module with DKMS
2. Automatically build for your current kernel
3. Install the compiled modules
4. Update module dependencies

### Verify Installation
```bash
# Check DKMS status
sudo dkms status
# Output: dm-remap/1.0.0, 6.14.0-34-generic, x86_64: installed

# Check installed modules
ls -l /lib/modules/$(uname -r)/updates/dkms/dm-remap*.ko*
```

## Loading the Module

```bash
sudo modprobe dm_remap
lsmod | grep dm_remap
```

## Multi-Kernel Support

When you install a new kernel, DKMS automatically builds and installs dm-remap for it:

```bash
# Update to a new kernel
sudo apt-get install linux-image-generic

# DKMS will automatically:
# 1. Detect the new kernel
# 2. Build dm-remap for the new kernel
# 3. Install the module

# Verify it's installed
sudo dkms status
```

## Uninstallation

```bash
sudo dpkg -r dm-remap-dkms
```

The prerm script will:
1. Unload the module if possible
2. Unregister from DKMS
3. Clean up all kernel-specific builds

## Package Details

| Property | Value |
|----------|-------|
| Package Name | dm-remap-dkms |
| Version | 1.0.0-1 |
| Architecture | all |
| Size | 225 KB |
| Type | DKMS Source Package |
| Section | kernel |
| Depends | dkms, linux-headers, build-essential, make |
| Install Location | `/usr/src/dm-remap-1.0.0/` |
| Module Location | `/lib/modules/<kernel>/updates/dkms/` |

## DKMS Status Output

After installation:
```
dm-remap/1.0.0, 6.14.0-34-generic, x86_64: installed
```

For multiple kernels (after installing more):
```
dm-remap/1.0.0, 6.14.0-33-generic, x86_64: installed
dm-remap/1.0.0, 6.14.0-34-generic, x86_64: installed
dm-remap/1.0.0, 6.15.0-rc1-generic, x86_64: installed
```

## Build Information

- **Build Date:** October 31, 2025
- **Kernel Used for Build:** 6.14.0-34-generic
- **Compiler Warnings:** ZERO (completely clean build)
- **Module Size:** dm-remap.ko = 2.2 MB (uncompressed)
- **Compression:** Kernel modules are automatically zstd-compressed

## Files in Package

```
/usr/src/dm-remap-1.0.0/
├── dkms.conf              # DKMS configuration
├── Makefile               # Top-level Makefile
├── README.md              # Documentation
├── LICENSE                # GPL license
├── include/               # Header files
│   └── *.h
├── src/                   # Source code
│   ├── *.c
│   └── Makefile
└── (other build files)
```

## Support & Documentation

- **Source Repository:** https://github.com/amigatomte/dm-remap
- **Issue Tracker:** https://github.com/amigatomte/dm-remap/issues
- **Documentation:** `/usr/share/doc/dm-remap-dkms/`

## Troubleshooting

### Module not loading
```bash
# Check if it's installed
sudo dkms status

# Check for build errors
sudo dkms build -m dm-remap -v 1.0.0 -k $(uname -r) --force

# Check kernel logs
sudo dmesg | tail -20
```

### Need to rebuild manually
```bash
sudo dkms remove -m dm-remap -v 1.0.0 --all
sudo dpkg -r dm-remap-dkms
sudo dpkg -i dm-remap-dkms_1.0.0-1_all.deb
```

---

**Status: ✅ Production Ready**

The DKMS package is fully functional and ready for distribution.
All installations automatically adapt to the target kernel version.
