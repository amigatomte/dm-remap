# dm-remap Packaging Guide

**Build and distribute dm-remap packages for Debian/Ubuntu and Red Hat/Fedora systems**

> This document explains how to create DEB and RPM packages for dm-remap distribution.

---

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Building DEB Packages](#building-deb-packages)
- [Building RPM Packages](#building-rpm-packages)
- [Package Contents](#package-contents)
- [Installation from Packages](#installation-from-packages)
- [Repository Publishing](#repository-publishing)
- [Advanced Topics](#advanced-topics)

---

## Overview

dm-remap provides packaging infrastructure for both Debian/Ubuntu (DEB) and Red Hat/Fedora (RPM) systems:

- **DEB packages** - For Debian, Ubuntu, and derived distributions
- **RPM packages** - For Red Hat, Fedora, CentOS, and derived distributions

Both package types include:
- ✅ Compiled kernel modules (INTEGRATED or MODULAR build)
- ✅ Complete documentation
- ✅ Development headers (RPM only)
- ✅ Post-install module dependency updates
- ✅ Pre-removal cleanup

---

## Prerequisites

### Common Requirements

```bash
# Update package manager
sudo apt-get update          # Debian/Ubuntu
sudo yum update              # Red Hat/CentOS
sudo dnf update              # Fedora

# Linux headers for kernel module compilation
sudo apt-get install -y linux-headers-$(uname -r)  # Debian/Ubuntu
sudo yum install -y kernel-devel                    # Red Hat/CentOS
sudo dnf install -y kernel-devel                    # Fedora

# Build tools
sudo apt-get install -y build-essential             # Debian/Ubuntu
sudo yum groupinstall -y "Development Tools"        # Red Hat/CentOS
sudo dnf groupinstall -y "Development Tools"        # Fedora
```

### DEB Package Building

```bash
# Debian/Ubuntu specific tools
sudo apt-get install -y debhelper devscripts

# Required for lintian checks (optional but recommended)
sudo apt-get install -y lintian
```

### RPM Package Building

```bash
# Red Hat/Fedora specific tools
sudo yum install -y rpmbuild rpm-build              # Red Hat/CentOS
sudo dnf install -y rpmbuild rpm-build              # Fedora

# Optional: Mock for isolated builds
sudo yum install -y mock                            # Red Hat/CentOS
sudo dnf install -y mock                            # Fedora
```

### Development Environment

```bash
# Clone the repository
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap

# Verify current build status
make show-build-mode
```

---

## Building DEB Packages

### Quick Build

```bash
# Navigate to project root
cd dm-remap

# Build with debuild (recommended)
debuild -us -uc

# Packages will be created in parent directory
cd ..
ls -lh *.deb
```

### Step-by-Step Build

```bash
# 1. Ensure all dependencies installed
sudo apt-get install -y debhelper devscripts build-essential linux-headers-$(uname -r)

# 2. Navigate to project
cd dm-remap

# 3. Build source package
debuild -S -us -uc

# 4. Build binary package (requires dsc file)
cd ..
sudo pbuilder build dm-remap_*.dsc

# 5. Find output packages
ls -lh dm-remap*.deb
```

### Manual Build Process

```bash
# Alternative: Manual dpkg-buildpackage
cd dm-remap

# Clean previous builds
dpkg-buildpackage -T clean

# Build packages (unsigned)
dpkg-buildpackage -us -uc -j4

# Output files
cd ..
ls -lh dm-remap*
```

### Build Options

```bash
# Build specific package only
debuild -us -uc -p dm-remap

# Enable debug symbols
DEB_BUILD_OPTIONS="debug" debuild -us -uc

# Build with parallelism
debuild -us -uc -j8

# Verbose output
DH_VERBOSE=1 debuild -us -uc
```

### Package Output

After successful build:

```
dm-remap_1.0.0-1_amd64.deb          # Main package (kernel module)
dm-remap-doc_1.0.0-1_all.deb        # Documentation package
dm-remap_1.0.0-1.dsc                # Source package descriptor
dm-remap_1.0.0-1.tar.gz             # Source package
dm-remap_1.0.0-1_amd64.changes      # Changes file (for upload)
```

### DEB Package Contents

```bash
# Inspect package contents
dpkg -L dm-remap

# Expected locations:
# /lib/modules/{kernel}/extra/dm-remap.ko
# /lib/modules/{kernel}/extra/dm-remap-test.ko
# /etc/depmod.d/dm-remap.conf

# Documentation
# /usr/share/doc/dm-remap/README.md
# /usr/share/doc/dm-remap/docs/
```

### Signing Packages

For public distribution, sign with GPG key:

```bash
# Build with signature
debuild -us -uc          # Build unsigned (for testing)
debuild -S               # Build and sign source package
debuild                  # Build and sign everything

# Requires GPG key setup:
gpg --gen-key            # Generate signing key if needed
gpg --list-keys          # List available keys
```

---

## Building RPM Packages

### Quick Build

```bash
# Navigate to project root
cd dm-remap

# Build RPM
rpmbuild -bb dm-remap.spec

# Packages will be in ~/rpmbuild/RPMS/x86_64/
ls -lh ~/rpmbuild/RPMS/x86_64/dm-remap*
```

### Step-by-Step Build

```bash
# 1. Install dependencies
sudo dnf install -y rpmbuild rpm-build kernel-devel gcc make

# 2. Setup rpmbuild directories
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
echo '%_topdir %(echo $HOME)/rpmbuild' > ~/.rpmmacros

# 3. Navigate to project
cd dm-remap

# 4. Copy spec file
cp dm-remap.spec ~/rpmbuild/SPECS/

# 5. Create source tarball
tar czf ~/rpmbuild/SOURCES/dm-remap-1.0.0.tar.gz --exclude=.git .

# 6. Build package
rpmbuild -bb ~/rpmbuild/SPECS/dm-remap.spec

# 7. Packages created
ls -lh ~/rpmbuild/RPMS/x86_64/
```

### RPM Build Targets

```bash
# Build binary RPM only
rpmbuild -bb dm-remap.spec

# Build source RPM only
rpmbuild -bs dm-remap.spec

# Build source and binary
rpmbuild -ba dm-remap.spec

# Build with verbose output
rpmbuild --verbose -bb dm-remap.spec
```

### Build Options

```bash
# Override version/release
rpmbuild -bb dm-remap.spec --define "version 1.0.1" --define "release 2"

# Specify kernel version (if different from running kernel)
rpmbuild -bb dm-remap.spec --define "kernel_version 5.15.0-1234"

# Enable debug build
rpmbuild -bb dm-remap.spec --define "debug 1"

# Build for specific architecture
rpmbuild -bb dm-remap.spec --target x86_64
```

### Package Output

After successful build:

```
~/rpmbuild/RPMS/x86_64/dm-remap-1.0.0-1.fc38.x86_64.rpm
~/rpmbuild/RPMS/x86_64/dm-remap-devel-1.0.0-1.fc38.x86_64.rpm
~/rpmbuild/RPMS/x86_64/dm-remap-doc-1.0.0-1.fc38.noarch.rpm
~/rpmbuild/SRPMS/dm-remap-1.0.0-1.fc38.src.rpm
```

### RPM Package Contents

```bash
# List package contents
rpm -ql dm-remap

# Expected locations:
# /lib/modules/{kernel}/extra/dm-remap.ko
# /lib/modules/{kernel}/extra/dm-remap-test.ko
# /etc/depmod.d/dm-remap.conf
# /usr/include/dm-remap/

# Documentation
# /usr/share/doc/dm-remap/
```

### Signing Packages

For public distribution, sign with GPG key:

```bash
# Generate GPG key (if needed)
gpg --gen-key
gpg --list-keys

# Configure RPM signing
~/.rpmmacros:
%_signature gpg
%_gpg_name "Your Name <email@example.com>"

# Build and sign
rpmbuild -bb --sign dm-remap.spec

# Verify signature
rpm --checksig dm-remap-1.0.0-1.fc38.x86_64.rpm
```

---

## Package Contents

### DEB Package Structure

```
dm-remap (main package)
├── /lib/modules/$(uname -r)/extra/
│   ├── dm-remap.ko
│   └── dm-remap-test.ko
├── /etc/depmod.d/
│   └── dm-remap.conf
└── Post-install hook: depmod -a

dm-remap-doc (documentation)
└── /usr/share/doc/dm-remap/
    ├── README.md
    ├── LICENSE
    └── docs/ (all documentation)
```

### RPM Package Structure

```
dm-remap (main package)
├── /lib/modules/$(uname -r)/extra/
│   ├── dm-remap.ko
│   └── dm-remap-test.ko
└── /etc/depmod.d/
    └── dm-remap.conf

dm-remap-devel (development files)
└── /usr/include/dm-remap/
    └── dm-remap-v4-*.h

dm-remap-doc (documentation)
└── /usr/share/doc/dm-remap/
    ├── README.md
    ├── LICENSE
    └── docs/ (all documentation)
```

### Post-Install Actions

**Both DEB and RPM perform:**
1. Update kernel module dependencies (`depmod -a`)
2. Display installation success message
3. Suggest next steps (modprobe command)

---

## Installation from Packages

### DEB Installation

```bash
# Install main package
sudo dpkg -i dm-remap_1.0.0-1_amd64.deb

# Or using apt (if in local repository)
sudo apt-get install ./dm-remap_1.0.0-1_amd64.deb

# Install documentation
sudo dpkg -i dm-remap-doc_1.0.0-1_all.deb

# Verify installation
dpkg -l | grep dm-remap
modinfo dm-remap

# Load module
sudo modprobe dm_remap
```

### RPM Installation

```bash
# Install main package
sudo rpm -i dm-remap-1.0.0-1.fc38.x86_64.rpm

# Or using dnf
sudo dnf install dm-remap-1.0.0-1.fc38.x86_64.rpm

# Install development files
sudo rpm -i dm-remap-devel-1.0.0-1.fc38.x86_64.rpm

# Install documentation
sudo rpm -i dm-remap-doc-1.0.0-1.fc38.noarch.rpm

# Verify installation
rpm -qa | grep dm-remap
modinfo dm-remap

# Load module
sudo modprobe dm_remap
```

### Upgrade from Previous Version

```bash
# DEB upgrade
sudo dpkg -i dm-remap_1.0.1-1_amd64.deb

# Or
sudo apt-get install --only-upgrade dm-remap

# RPM upgrade
sudo rpm -U dm-remap-1.0.1-1.fc38.x86_64.rpm

# Or
sudo dnf upgrade dm-remap
```

### Uninstall

```bash
# DEB removal
sudo apt-get remove dm-remap
sudo apt-get remove dm-remap-doc

# RPM removal
sudo rpm -e dm-remap
sudo dnf remove dm-remap
```

---

## Repository Publishing

### Creating Local DEB Repository

```bash
# Create repository directory
mkdir -p ~/dm-remap-repo/pool/main

# Copy DEB packages
cp dm-remap*.deb ~/dm-remap-repo/pool/main/

# Create Packages index
cd ~/dm-remap-repo
apt-ftparchive packages pool/main > Packages
gzip -c Packages > Packages.gz

# Create Release file
apt-ftparchive release . > Release

# Optional: Sign Release file
gpg -bao Release.gpg Release

# Serve via HTTP
cd ~/dm-remap-repo
python3 -m http.server 8000
```

### Creating Local RPM Repository

```bash
# Create repository directory
mkdir -p ~/dm-remap-repo

# Copy RPM packages
cp ~/rpmbuild/RPMS/*/*.rpm ~/dm-remap-repo/
cp ~/rpmbuild/SRPMS/*.rpm ~/dm-remap-repo/

# Create repository metadata
createrepo ~/dm-remap-repo/

# Optional: Sign repository
gpg --detach-sign --armor repodata/repomd.xml

# Serve via HTTP
cd ~/dm-remap-repo
python3 -m http.server 8000
```

### Using Local Repository (Client Side)

**For DEB:**
```bash
# Add to sources.list
echo "deb [trusted=yes] http://localhost:8000 ./" | sudo tee /etc/apt/sources.list.d/dm-remap.list

# Install
sudo apt-get update
sudo apt-get install dm-remap
```

**For RPM:**
```bash
# Add repository config
sudo tee /etc/yum.repos.d/dm-remap.repo << EOF
[dm-remap]
name=dm-remap Local Repository
baseurl=http://localhost:8000
enabled=1
gpgcheck=0
EOF

# Install
sudo dnf install dm-remap
```

### Publishing to Public Repository

See platform-specific guides:
- **DEB**: Ubuntu PPA, Debian Repository
- **RPM**: Fedora COPR, Koji, Package Hub
- **Both**: GitHub Releases

---

## Advanced Topics

### Cross-Compilation

```bash
# Build for different kernel version
TARGET_KERNEL=5.15.0-1234
KDIR=/usr/src/linux-headers-$TARGET_KERNEL
make clean && make KDIR=$KDIR

# Package with target kernel
rpmbuild -bb dm-remap.spec --define "kernel_version $TARGET_KERNEL"
```

### Modular Build Packaging

```bash
# Build MODULAR version for DEB
BUILD_MODE=MODULAR debuild -us -uc

# Build MODULAR version for RPM
BUILD_MODE=MODULAR rpmbuild -bb dm-remap.spec

# Packages will contain separate .ko files
dpkg -L dm-remap | grep "\.ko"
rpm -ql dm-remap | grep "\.ko"
```

### CI/CD Integration

```bash
# GitHub Actions (example)
name: Build Packages
on: [push]
jobs:
  build-deb:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - run: |
          sudo apt-get update
          sudo apt-get install -y debhelper devscripts build-essential linux-headers-generic
          debuild -us -uc
  build-rpm:
    runs-on: ubuntu-latest
    container: fedora:latest
    steps:
      - uses: actions/checkout@v2
      - run: |
          dnf install -y rpmbuild rpm-build kernel-devel gcc make
          rpmbuild -bb dm-remap.spec
```

### Automated Testing

```bash
# Test DEB installation
dpkg-buildpackage -us -uc
sudo dpkg -i ../dm-remap*.deb
sudo modprobe dm_remap
lsmod | grep dm_remap

# Test RPM installation
rpmbuild -bb dm-remap.spec
sudo rpm -i ~/rpmbuild/RPMS/x86_64/dm-remap*.rpm
sudo modprobe dm_remap
```

### Custom Patches

```bash
# For DEB (add to debian/patches/series)
mkdir -p debian/patches
echo "my-fix.patch" > debian/patches/series

# For RPM (add to dm-remap.spec)
Patch0: my-fix.patch
# In %prep section:
%patch0 -p1
```

---

## Troubleshooting

### DEB Build Issues

```bash
# Missing dependencies
E: Build depends not satisfied
Solution: sudo apt-get install debhelper devscripts

# Lintian warnings
debuild --no-lintian -us -uc  # Skip lintian checks
dpkg-buildpackage --no-sign   # Alternative

# Source format issues
cat debian/source/format      # Should be "3.0 (quilt)" or "1.0"
```

### RPM Build Issues

```bash
# Missing kernel headers
rpmbuild -bb dm-remap.spec --define "kernel_version $(uname -r)"

# Directory not found
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Bad URL in specfile
wget -c <source-url>  # Pre-download source
```

### Module Loading Issues

```bash
# Check kernel compatibility
uname -r
modinfo dm-remap.ko | grep vermagic

# Rebuild against running kernel
KDIR=/lib/modules/$(uname -r)/build make clean
KDIR=/lib/modules/$(uname -r)/build make
```

---

## See Also

- [README.md](../../README.md) - Project overview
- [BUILD_SYSTEM.md](./BUILD_SYSTEM.md) - Build modes and configuration
- [INSTALLATION.md](../user/INSTALLATION.md) - User installation guide
- [Debian New Maintainers' Guide](https://www.debian.org/doc/manuals/maint-guide/)
- [RPM Packaging Guide](https://rpm.org/)
