# dm-remap Installation Guide

**Version:** 4.2.2  
**Last Updated:** October 28, 2025

---

## Quick Reference

```bash
# Ubuntu/Debian
sudo apt-get install build-essential linux-headers-$(uname -r)
git clone https://github.com/amigatomte/dm-remap.git && cd dm-remap
make && sudo make install
sudo modprobe dm_remap_v4_real

# RHEL/CentOS
sudo yum groupinstall "Development Tools"
sudo yum install kernel-devel-$(uname -r | sed 's/\..*//')
git clone https://github.com/amigatomte/dm-remap.git && cd dm-remap
make && sudo make install
sudo modprobe dm_remap_v4_real
```

---

## Step-by-Step Installation

### 1. Check Prerequisites

```bash
# Check kernel version (5.10+)
uname -r
# Expected: 5.10 or later (e.g., 6.14.0-34-generic)

# Check device mapper support
ls /sys/module/dm_*
# Expected: Several dm_* modules

# Check device mapper tools
which dmsetup
# Expected: /sbin/dmsetup or /usr/sbin/dmsetup
```

### 2. Install Build Tools

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  linux-headers-$(uname -r) \
  git
```

**RHEL/CentOS/Fedora:**
```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
  kernel-devel-$(uname -r | sed 's/\..*//) \
  git
```

**Arch Linux:**
```bash
sudo pacman -S base-devel linux-headers git
```

### 3. Obtain the Source

```bash
# Clone from GitHub
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap

# Or download release package
wget https://github.com/amigatomte/dm-remap/releases/download/v4.2.2/dm-remap-4.2.2.tar.gz
tar xzf dm-remap-4.2.2.tar.gz
cd dm-remap-4.2.2
```

### 4. Build the Module

```bash
# Clean previous builds
make clean

# Build
make

# This will compile:
# - dm-remap-v4-real.ko (main module, ~1.5 MB)
# - Supporting files

# Verify compilation
ls -lh src/dm-remap-v4-real.ko
# Expected: -rw-r--r-- 1 christian christian 1.5M Oct 28 20:00 src/dm-remap-v4-real.ko
```

**Build troubleshooting:**

```bash
# If build fails with "permission denied"
sudo make clean
sudo make

# If build fails with "kernel headers not found"
apt-get install linux-headers-$(uname -r)
make clean && make

# If make: command not found
sudo apt-get install build-essential
# or
sudo yum groupinstall "Development Tools"
```

### 5. Install the Module

```bash
# Install to system kernel modules directory
sudo make install

# This copies:
# - dm-remap-v4-real.ko to /lib/modules/$(uname -r)/extra/

# Verify installation
ls -la /lib/modules/$(uname -r)/extra/dm-remap-v4-real.ko

# Update module dependencies
sudo depmod -a
```

### 6. Load the Module

```bash
# Manual load
sudo modprobe dm_remap_v4_real

# Verify module loaded
lsmod | grep dm_remap
# Expected output:
# dm_remap_v4_real  81920  0
# dm_remap_v4_stats 16384  1 dm_remap_v4_real
# dm_bufio           57344  1 dm_remap_v4_real

# Verify target registered
sudo dmsetup targets | grep remap
# Expected: dm-remap-v4 v4.0.0
```

### 7. (Optional) Load at Boot

**Option A: Using /etc/modules**
```bash
echo "dm_remap_v4_real" | sudo tee -a /etc/modules
```

**Option B: Using systemd**
```bash
sudo bash -c 'cat > /etc/modules-load.d/dm-remap.conf <<EOF
dm_remap_v4_real
EOF'
```

**Option C: Using /etc/modprobe.d/**
```bash
sudo bash -c 'cat > /etc/modprobe.d/dm-remap.conf <<EOF
install dm_remap_v4_real /sbin/modprobe dm_remap_v4_stats && /sbin/insmod /lib/modules/$(uname -r)/extra/dm-remap-v4-real.ko
EOF'
```

---

## Docker Installation

```dockerfile
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    linux-headers-generic \
    git

# Clone and build
RUN git clone https://github.com/amigatomte/dm-remap.git /src/dm-remap
WORKDIR /src/dm-remap
RUN make

# Install
RUN make install

# Verify
RUN lsmod | grep dm_remap || \
    modprobe dm_remap_v4_real && \
    lsmod | grep dm_remap

CMD ["/bin/bash"]
```

---

## Troubleshooting Installation

### Issue: Linux headers not found

```bash
# Problem
make: *** /lib/modules/5.10.0-generic/build: No such file or directory

# Solution
sudo apt-get install linux-headers-$(uname -r)
# or
sudo yum install kernel-devel-$(uname -r | sed 's/\..*//')

# Verify
ls /lib/modules/$(uname -r)/build/
# Should show: Makefile, include/, etc.
```

### Issue: Permission denied when building

```bash
# Problem
make: Permission denied

# Solution (if not sudoing)
sudo make clean
sudo make
sudo make install

# Or use a build directory
mkdir -p build && cd build
make -C .. O=$(pwd)
```

### Issue: Module fails to load

```bash
# Problem
modprobe: FATAL: Module dm_remap_v4_real not found in directory /lib/modules/5.10.0-generic

# Solutions
# 1. Verify installation
ls -la /lib/modules/$(uname -r)/extra/dm-remap-v4-real.ko

# 2. Update module cache
sudo depmod -a

# 3. Try absolute path
sudo insmod /lib/modules/$(uname -r)/extra/dm-remap-v4-real.ko

# 4. Check for errors
dmesg | tail -20
```

### Issue: dmsetup targets doesn't show dm-remap-v4

```bash
# Problem
$ sudo dmsetup targets | grep remap
(no output)

# Solution
# 1. Load module
sudo modprobe dm_remap_v4_real

# 2. Verify again
sudo dmsetup targets | grep remap

# 3. If still not showing, check kernel logs
sudo dmesg | grep "dm-remap\|FATAL\|ERROR"

# 4. Rebuild and reinstall
make clean
make
sudo make install
sudo modprobe -r dm_remap_v4_real
sudo modprobe dm_remap_v4_real
```

### Issue: Compilation errors

```bash
# Problem
error: implicit declaration of function 'dm_remap_something'

# Solution
# 1. Check kernel version
uname -r
# dm-remap requires Linux 5.10+

# 2. Update headers
sudo apt-get update
sudo apt-get install linux-headers-$(uname -r)

# 3. Clean and rebuild
make clean
make -j$(nproc)

# 4. If errors persist, check dmesg
dmesg | tail -50
```

---

## Verification

After installation, verify everything works:

```bash
#!/bin/bash
# verify_installation.sh

echo "=== Verifying dm-remap Installation ==="

# 1. Check module loaded
echo "1. Checking module..."
if lsmod | grep -q dm_remap_v4_real; then
    echo "   ✓ Module loaded"
else
    echo "   ✗ Module not loaded"
    sudo modprobe dm_remap_v4_real
fi

# 2. Check target registered
echo "2. Checking target registration..."
if sudo dmsetup targets | grep -q dm-remap-v4; then
    echo "   ✓ Target registered"
else
    echo "   ✗ Target not registered"
fi

# 3. Check kernel module file
echo "3. Checking module file..."
if [ -f "/lib/modules/$(uname -r)/extra/dm-remap-v4-real.ko" ]; then
    SIZE=$(stat -c%s /lib/modules/$(uname -r)/extra/dm-remap-v4-real.ko)
    echo "   ✓ Module file found ($((SIZE/1024)) KB)"
else
    echo "   ✗ Module file not found"
fi

# 4. Check dependencies
echo "4. Checking dependencies..."
DEPS=$(lsmod | grep -E "dm_bufio|dm_remap_v4_stats" | wc -l)
if [ $DEPS -ge 2 ]; then
    echo "   ✓ Dependencies loaded"
else
    echo "   ✗ Some dependencies missing"
fi

echo ""
echo "=== Installation Verification Complete ==="
```

Run it:
```bash
chmod +x verify_installation.sh
./verify_installation.sh
```

---

## Next Steps

After successful installation:

1. **Read the User Guide**: `docs/user/USER_GUIDE.md`
2. **Quick Start**: Follow the Quick Start section
3. **Run Tests**: `bash tests/validate_hash_resize.sh`
4. **Explore Examples**: Check `demos/` directory

---

## Support

If you encounter issues:

1. Check the Troubleshooting section above
2. Review kernel logs: `sudo dmesg | tail -50`
3. Open an issue on GitHub: https://github.com/amigatomte/dm-remap/issues
4. Include output of: `uname -a && lsmod | grep dm`

---

**Document:** dm-remap Installation Guide  
**Version:** 4.2.2  
**Status:** FINAL ✅

