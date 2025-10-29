# dm-remap DKMS Guide

**Dynamic Kernel Module Support - Automatic compilation for any kernel version**

> This document explains how to use DKMS to automatically build and maintain dm-remap across kernel updates.

---

## Table of Contents

- [Overview](#overview)
- [Why DKMS?](#why-dkms)
- [Quick Start](#quick-start)
- [Installation](#installation)
- [Management](#management)
- [Kernel Updates](#kernel-updates)
- [Troubleshooting](#troubleshooting)
- [Manual DKMS Operations](#manual-dkms-operations)
- [Building DKMS Packages](#building-dkms-packages)
- [DKMS Configuration](#dkms-configuration)

---

## Overview

**DKMS (Dynamic Kernel Module Support)** automatically builds and installs the dm-remap kernel module for **any kernel version** on your system.

### Key Benefits

✅ **One package for all kernels** - No need for separate packages per kernel version  
✅ **Automatic kernel updates** - Module rebuilds when kernel is updated  
✅ **Zero configuration** - Works out of the box after installation  
✅ **System integration** - Installed in standard kernel module location  
✅ **Easy management** - Simple commands to check status and remove  

### Traditional vs DKMS Approach

```
Traditional Packaging:
├── dm-remap-1.0.0-kernel5.10.ko      ← Need separate package for each kernel
├── dm-remap-1.0.0-kernel5.15.ko      ← Massive repository complexity
├── dm-remap-1.0.0-kernel5.19.ko      ← Hard to maintain
├── dm-remap-1.0.0-kernel6.0.ko
└── dm-remap-1.0.0-kernel6.1.ko

DKMS Approach:
└── dm-remap-dkms-1.0.0                ← One package
    └── Automatically compiles for:
        ├── Kernel 5.10 (on first install)
        ├── Kernel 5.15 (on kernel update)
        ├── Kernel 5.19 (on kernel update)
        ├── Kernel 6.0 (on kernel update)
        └── Kernel 6.1 (on kernel update)
```

---

## Why DKMS?

### Problem: Multiple Kernel Versions

When systems have multiple kernels or users have different kernel versions:

**Without DKMS:**
- Maintainer must build package for every kernel version
- Repository has hundreds of nearly-identical packages
- Users on unsupported kernel versions get no module
- Complex dependency management

**With DKMS:**
- Single package that works for any kernel
- Automatic rebuild when kernel is updated
- Users always have module for their kernel
- Simple dependency: just DKMS and kernel-headers

### Real-World Scenarios

**Scenario 1: Kernel Update**
```
Without DKMS:
1. User installs dm-remap-kernel5.10.rpm
2. System kernel updates to 5.15
3. Old module no longer works
4. Must wait for dm-remap-kernel5.15.rpm release
5. User manually upgrades package

With DKMS:
1. User installs dm-remap-dkms.rpm
2. System kernel updates to 5.15
3. DKMS automatically rebuilds for 5.15
4. Module works immediately
5. Zero user action needed
```

**Scenario 2: Multiple Kernel Versions**
```
Without DKMS:
Need packages: dm-remap-5.10.rpm, dm-remap-5.15.rpm, dm-remap-5.19.rpm, ...
(20+ packages for common kernels)

With DKMS:
Install once: dm-remap-dkms.rpm
Automatically works for all kernels
```

---

## Quick Start

### Installation

```bash
# DEB (Debian/Ubuntu)
sudo apt-get update
sudo apt-get install dm-remap-dkms

# RPM (Red Hat/Fedora/CentOS)
sudo dnf install dm-remap-dkms
# or
sudo yum install dm-remap-dkms
```

### Verify Installation

```bash
# Check DKMS status
dkms status

# Expected output:
# dm-remap/1.0.0, 5.15.0-91-generic, x86_64: installed

# Verify module is loaded
lsmod | grep dm_remap

# Load module if not loaded
sudo modprobe dm_remap
```

### Check Module Info

```bash
modinfo dm_remap

# Expected output:
# filename:       /lib/modules/.../extra/dm-remap.ko
# version:        1.0.0
# description:    Device Mapper Remapping Target
```

---

## Installation

### DEB Installation (Debian/Ubuntu)

```bash
# Update package manager
sudo apt-get update

# Install DKMS (if not already installed)
sudo apt-get install -y dkms build-essential linux-headers-generic

# Install dm-remap-dkms
sudo apt-get install dm-remap-dkms

# Automatic actions during install:
# 1. Adds dm-remap to DKMS
# 2. Builds module for current kernel
# 3. Installs module in /lib/modules/
```

### RPM Installation (Red Hat/Fedora/CentOS)

```bash
# Install DKMS and dependencies
sudo dnf groupinstall "Development Tools"
sudo dnf install -y dkms kernel-devel

# Install dm-remap-dkms
sudo dnf install dm-remap-dkms
# or
sudo yum install dm-remap-dkms

# Automatic actions during install:
# 1. Adds dm-remap to DKMS
# 2. Builds module for current kernel
# 3. Installs module in /lib/modules/
```

### Post-Installation

```bash
# Verify installation
dkms status

# Load the module
sudo modprobe dm_remap

# Verify loading
lsmod | grep dm_remap

# Expected output:
# dm_remap               2703360  0
```

---

## Management

### Check Status

```bash
# Display complete DKMS status
dkms status

# Output format:
# dm-remap/1.0.0, 5.15.0-91-generic, x86_64: installed
# dm-remap/1.0.0, 5.19.0-35-generic, x86_64: installed
# (one line per kernel version)

# Check for specific module
dkms status -m dm-remap

# List all versions
dkms status -m dm-remap -v 1.0.0
```

### Load/Unload Module

```bash
# Load module
sudo modprobe dm_remap

# Verify loaded
lsmod | grep dm_remap

# Unload module (if not in use)
sudo modprobe -r dm_remap

# Verify unloaded
lsmod | grep dm_remap  # Should be empty
```

### Information Commands

```bash
# Show build status
dkms status -m dm-remap -v 1.0.0

# Show module file location
find /lib/modules -name "dm-remap.ko" -type f

# Show build logs (if available)
dkms build -m dm-remap -v 1.0.0 --verbose

# Show installed versions
dkms list
```

### Remove DKMS Module

```bash
# Remove from all kernels
sudo dkms remove -m dm-remap -v 1.0.0 --all

# Or via package manager:
# DEB
sudo apt-get remove dm-remap-dkms

# RPM
sudo dnf remove dm-remap-dkms
sudo yum remove dm-remap-dkms
```

---

## Kernel Updates

### Automatic Rebuild Process

When you update your kernel, DKMS automatically:

1. **Detects new kernel** - When new kernel is installed
2. **Builds module** - Compiles dm-remap for new kernel
3. **Installs module** - Places in correct `/lib/modules/` location
4. **Updates depmod** - Updates kernel module dependencies

### Manual Rebuild (if needed)

```bash
# Rebuild for current kernel
sudo dkms build -m dm-remap -v 1.0.0

# Rebuild and install
sudo dkms install -m dm-remap -v 1.0.0

# Rebuild for specific kernel
sudo dkms build -m dm-remap -v 1.0.0 -k 5.15.0-91-generic

# Rebuild for all installed kernels
sudo dkms autoinstall -m dm-remap -v 1.0.0
```

### Monitoring Kernel Updates

```bash
# Watch DKMS status
watch dkms status -m dm-remap

# Check kernel module location after update
ls -la /lib/modules/$(uname -r)/extra/dm-remap.ko

# View build logs
cat /var/lib/dkms/dm-remap/1.0.0/build/make.log
```

### Troubleshooting Updates

```bash
# If module not found after kernel update:

# 1. Check DKMS status
dkms status -m dm-remap

# 2. Trigger rebuild manually
sudo dkms autoinstall

# 3. Check for compilation errors
sudo dkms build -m dm-remap -v 1.0.0 --verbose 2>&1 | tail -50

# 4. Verify installed
dkms status -m dm-remap
```

---

## Troubleshooting

### Module Not Loading After Install

```bash
# Check DKMS status
dkms status -m dm-remap

# If not installed:
sudo dkms install -m dm-remap -v 1.0.0

# Check for compilation errors
dkms build -m dm-remap -v 1.0.0 -k $(uname -r) --verbose

# Verify kernel headers installed
dpkg -l | grep linux-headers      # Debian/Ubuntu
rpm -qa | grep kernel-devel       # Red Hat/Fedora
```

### Build Failures

```bash
# View detailed build log
cat /var/lib/dkms/dm-remap/1.0.0/build/make.log

# Ensure dependencies installed
sudo apt-get install build-essential linux-headers-$(uname -r)  # Debian/Ubuntu
sudo dnf install kernel-devel gcc make                           # Red Hat/Fedora

# Retry build with verbose output
sudo dkms build -m dm-remap -v 1.0.0 --verbose

# Check kernel headers location
ls -la /lib/modules/$(uname -r)/build
```

### Module File Not Found

```bash
# Check where module was installed
sudo find /lib/modules -name "dm-remap.ko"

# If not found, reinstall
sudo dkms remove -m dm-remap -v 1.0.0 --all
sudo dkms install -m dm-remap -v 1.0.0

# Update depmod
sudo depmod -a
```

### Dependency Issues

```bash
# Verify kernel module dependencies
modinfo /lib/modules/$(uname -r)/extra/dm-remap.ko | grep depends

# Expected: depends: dm-bufio

# If dm-bufio not available:
# Load dm-bufio first (usually auto-loaded with dm_remap)
sudo modprobe dm-bufio
```

### Permission Errors

```bash
# All DKMS commands require sudo
sudo dkms status
sudo dkms build -m dm-remap -v 1.0.0
sudo dkms install -m dm-remap -v 1.0.0

# If still issues, check DKMS permissions
ls -la /var/lib/dkms/
ls -la /usr/src/
```

---

## Manual DKMS Operations

### Complete Rebuild Cycle

```bash
# 1. Remove existing DKMS module
sudo dkms remove -m dm-remap -v 1.0.0 --all

# 2. Add DKMS source
sudo dkms add -m dm-remap -v 1.0.0

# 3. Build module
sudo dkms build -m dm-remap -v 1.0.0

# 4. Install module
sudo dkms install -m dm-remap -v 1.0.0

# 5. Verify installation
dkms status -m dm-remap

# 6. Load module
sudo modprobe dm_remap
```

### Advanced DKMS Commands

```bash
# List all DKMS modules
dkms list

# Show DKMS configuration
dkms config -m dm-remap -v 1.0.0

# Mark module for autoinstall
dkms autoinstall -m dm-remap -v 1.0.0

# Build with custom kernel
sudo dkms build -m dm-remap -v 1.0.0 -k /path/to/kernel/source

# Export DKMS status
dkms status --json
```

### Viewing Build Artifacts

```bash
# DKMS working directory
ls -la /var/lib/dkms/dm-remap/1.0.0/

# Build log
cat /var/lib/dkms/dm-remap/1.0.0/build/make.log

# Source location
ls -la /usr/src/dm-remap-1.0.0/

# Built module
ls -la /lib/modules/$(uname -r)/extra/dm-remap.ko
```

---

## Building DKMS Packages

### Overview

DKMS packages are built from source and can be distributed to other systems. There are two main package formats:

- **DEB** - For Debian/Ubuntu systems
- **RPM** - For Red Hat/Fedora/CentOS systems

### Prerequisites

```bash
# For DEB package building
sudo apt-get install -y debhelper devscripts fakeroot

# For RPM package building
sudo dnf install -y rpm-build rpmbuild
```

### Building DEB Package (Debian/Ubuntu)

```bash
# Build the DKMS DEB package
make dkms-deb

# Output location: parent directory (../*.deb)
# Example: dm-remap-dkms_1.0.0_all.deb

# Verify build
ls -lh ../*dkms*.deb

# Clean build artifacts
make dkms-deb-clean
```

### Building RPM Package (Red Hat/Fedora/CentOS)

```bash
# Build the DKMS RPM package
make dkms-rpm

# Output location: ~/rpmbuild/RPMS/noarch/
# Example: dm-remap-dkms-1.0.0-1.el9.noarch.rpm

# Verify build
ls -lh ~/rpmbuild/RPMS/noarch/*dkms*.rpm

# Clean build artifacts
rm -rf ~/rpmbuild/BUILD/dm-remap-dkms-*
```

### Building Both Packages

```bash
# Build DEB and RPM packages
make dkms-packages

# Outputs will be in:
# - DEB: ../*.deb
# - RPM: ~/rpmbuild/RPMS/noarch/*.rpm
```

### Distribution

After building, distribute the packages to users:

```bash
# DEB installation
sudo apt-get install ./dm-remap-dkms_1.0.0_all.deb

# RPM installation
sudo dnf install ./dm-remap-dkms-1.0.0-1.el9.noarch.rpm
sudo yum install ./dm-remap-dkms-1.0.0-1.el9.noarch.rpm
```

### Package Contents

Both DEB and RPM packages contain:

```
dkms.conf                    # DKMS configuration
src/                        # Source code
debian-dkms/                # Debian packaging files (DEB only)
dm-remap-dkms.spec         # RPM spec file (RPM only)
debian-dkms/postinst       # Post-install script (automatic DKMS setup)
debian-dkms/prerm          # Pre-remove script (cleanup)
```

### Package Installation Flow

When a user installs the package:

1. **Extract** - Package extracts to `/usr/src/dm-remap-1.0.0/`
2. **Register** - `dkms add -m dm-remap -v 1.0.0` registers the source
3. **Build** - `dkms build -m dm-remap -v 1.0.0` compiles for current kernel
4. **Install** - `dkms install -m dm-remap -v 1.0.0` places module in `/lib/modules/`
5. **Verify** - User can check with `dkms status`

---

## DKMS Configuration

### dkms.conf Format

The `dkms.conf` file controls how DKMS builds dm-remap:

```makefile
PACKAGE_NAME="dm-remap"          # Module name
PACKAGE_VERSION="1.0.0"          # Version
CLEAN="make -C src clean"        # Clean command
MAKE="make -C src ..."           # Build command
INSTALL[0]="..."                 # Installation procedures
AUTOINSTALL="yes"                # Auto-rebuild on kernel update
KERNELRELEASE="${kernelver}"     # Kernel version variable
BUILT_MODULE_NAME[0]="dm-remap"  # Output module name
BUILT_MODULE_LOCATION[0]="src/"  # Where module is built
DEST_MODULE_LOCATION[0]="/extra"  # Where to install in /lib/modules/
```

### Customizing Build

```bash
# View current DKMS config
cat dkms.conf

# Edit if needed (usually not required)
sudo nano dkms.conf

# Rebuild after changes
sudo dkms remove -m dm-remap -v 1.0.0 --all
sudo dkms add -m dm-remap -v 1.0.0
sudo dkms install -m dm-remap -v 1.0.0
```

---

## Comparison: DKMS vs Other Approaches

### DKMS vs Traditional Packages

| Aspect | DKMS | Traditional |
|--------|------|-----------|
| **Package count** | 1 | 20+ (one per kernel) |
| **Kernel update** | Automatic rebuild | Manual package upgrade |
| **Repository size** | Minimal | Huge |
| **Maintenance** | Low (one source) | High (many binaries) |
| **User experience** | Automatic | Manual upgrades |

### DKMS vs LKM Repositories

| Aspect | DKMS | LKM Repo |
|--------|------|---------|
| **Building** | On system | Remote server |
| **Dependencies** | Local headers | Upload to repo |
| **Speed** | Slower (build time) | Faster (download) |
| **Customization** | Easy local build | Limited |
| **Flexibility** | Maximum | Preset builds |

### DKMS vs In-Kernel

| Aspect | DKMS | In-Kernel |
|--------|------|-----------|
| **Update cycle** | Independent | Kernel release cycle |
| **Development** | Faster | Slower (kernel merge) |
| **Deployment** | Immediate | Months/years wait |
| **User adoption** | Optional | Mandatory |

---

## See Also

- [README.md](../../README.md) - Project overview
- [PACKAGING.md](./PACKAGING.md) - Traditional package building
- [BUILD_SYSTEM.md](./BUILD_SYSTEM.md) - Build modes and configuration
- [INSTALLATION.md](../user/INSTALLATION.md) - User installation guide
- [DKMS Official Documentation](https://linux.oracle.com/projects/dkms/)
- [Ubuntu DKMS Guide](https://ubuntu.com/manpages/focal/man8/dkms.8)
- [Fedora DKMS Guide](https://docs.fedoraproject.org/en-US/fedora/latest/system-administrators-guide/servers/Kernel_Module_Driver_Configuration/)
