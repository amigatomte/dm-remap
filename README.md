# dm-remap

**dm-remap** is a custom Linux Device Mapper (DM) target that emulates firmware‑level bad sector remapping entirely in software.  
It was created for situations where a storage device is starting to fail — perhaps with a growing number of bad sectors — but you still need to keep it in service long enough to recover data, run legacy workloads, or extend its usable life.

On many drives, the firmware automatically remaps failing sectors to a hidden pool of spares. But when that firmware‑level remapping is absent, exhausted, or unreliable, the operating system will start seeing I/O errors. `dm-remap` steps in at the kernel level to provide a **transparent, configurable remapping layer**: you define a main device and a spare device (or region), and the module dynamically redirects reads and writes from bad sectors to healthy spare sectors.

This approach allows:
- Salvaging partially damaged disks by isolating and bypassing bad areas
- Testing and simulating remapping behaviour for research or development
- Creating reproducible fault‑injection scenarios for storage systems

The project now includes two companion scripts that make working with `dm-remap` safer and more productive:
- **`remap_create_safe.sh`** — an alignment‑aware, order‑independent wrapper for creating mappings. It validates sector sizes, applies safe defaults, auto‑aligns offsets, and logs all actions.
- **`remap_test_driver.sh`** — an automated test harness for running single or batch I/O workloads against a remapped device, monitoring the kernel’s remap table in real time, and producing detailed logs and summaries.

Together, the kernel module and these tools let you go from “failing disk” to “controlled, monitored remap environment” in minutes — without risking further corruption from misaligned mappings or manual setup errors.

---

## ⚡ Quick Start

```bash
# 1. Clone and build
git clone https://github.com/amigatomte/dm-remap.git
cd dm-remap
make

# 2. Load the module
sudo insmod dm_remap.ko

# 3. Create a safe mapping (loop devices as example)
truncate -s 50M /tmp/primary.img
truncate -s 50M /tmp/spare.img
sudo losetup /dev/loop0 /tmp/primary.img
sudo losetup /dev/loop1 /tmp/spare.img

sudo ./remap_create_safe.sh main_dev=/dev/loop0 spare_dev=/dev/loop1 dm_name=my_remap

# 4. Run a quick test
sudo ./remap_test_driver.sh single 50 10 4k --dm-name=my_remap
```

---

## 🔧 Features
- Dynamic remapping of bad sectors via `dmsetup message`
- Configurable spare sector pool
- Transparent I/O redirection
- Simple mapping table stored in kernel memory
- Designed for salvaging aging or partially damaged drives
- **New**: Safer, alignment‑aware device creation via `remap_create_safe.sh`
- **New**: Automated single/batch testing with `remap_test_driver.sh`

---

## 📊 Data Flow Diagram

```
          +-------------------+
          |    Main Device    |
          |  (/dev/sdX etc.)  |
          +---------+---------+
                    |
                    v
          +-------------------+
          |   dm-remap Target |
          | (Kernel Module)   |
          +---------+---------+
                    |
        +-----------+-----------+
        | Remap Table (in-kernel)|
        +-----------+-----------+
                    |
          +---------+---------+
          |   Spare Device    |
          | (/dev/sdY etc.)   |
          +-------------------+
```

---

## 🚀 Getting Started

### 1. Build and Load the Module
```bash
make
sudo insmod dm_remap.ko
```

### 2. Create a Remapped Device (Safe Wrapper)
```bash
sudo ./remap_create_safe.sh main_dev=/dev/sdX spare_dev=/dev/sdY \
    main_start=0 spare_start=0 \
    logfile=/tmp/remap_wrapper.log dm_name=my_remap
```

### 3. Remap a Bad Sector
```bash
sudo dmsetup message my_remap 0 remap 123456
```

### 4. Test I/O
```bash
sudo dd if=/dev/zero of=/dev/mapper/my_remap bs=512 seek=123456 count=1
sudo dd if=/dev/mapper/my_remap bs=512 skip=123456 count=1 | hexdump -C
```

---

## 🧪 Automated Testing

### Single Run
```bash
./remap_test_driver.sh single 50 20 8k --dm-name=my_test
```

### Batch Mode
```bash
./remap_test_driver.sh batch --pause --dm-name-prefix=remap
```

---

## 📄 Example Wrapper Output

```
=== remap_create_safe.sh parameters ===
Main device:   /dev/loop0
Main start:    0 sectors
Spare device:  /dev/loop1
Spare start:   0 sectors
Spare total:   20480 sectors
Main phys size:  4096 B
Spare phys size: 4096 B
DM name:       my_remap
Log file:      /tmp/remap_wrapper_20250909_204200.log
=======================================
Creating device-mapper target...
dmsetup create my_remap with: 0 20480 remap /dev/loop0 0 /dev/loop1 0 20480
Mapping created successfully via wrapper.
```

---

## 📄 Example Test Driver Summary

```
=== SUMMARY for 50MB, 20s, 8k, dm_name=remap_50MB_20s_8k ===
First snapshot: 20:42:10
Last snapshot:  20:42:30
Total added lines:   12
Total removed lines: 0
Log file: remap_changes_50MB_20s_8k_20250909_204210.log
==============================
```

---

## 🛠 Troubleshooting

**Problem:** `ERROR: Physical sector sizes differ`  
**Fix:** Use devices with matching physical sector sizes (`cat /sys/block/<dev>/queue/hw_sector_size`).

**Problem:** `dmsetup: ioctl ... failed: Device or resource busy`  
**Fix:** Remove old mapping with `sudo dmsetup remove <dm_name>` and ensure no process is using it.

**Problem:** `Permission denied`  
**Fix:** Run commands with `sudo`.

**Problem:** Wrapper exits with code `1`  
**Fix:** Informational — wrapper auto‑adjusted values. Check log.

**Problem:** Wrapper exits with code `2`  
**Fix:** Physical sector sizes differ — choose compatible devices.

---

## 📋 Common Usage Patterns

### 1. Create Loopback Test Devices
```bash
truncate -s 100M /tmp/main.img
truncate -s 100M /tmp/spare.img
sudo losetup /dev/loop0 /tmp/main.img
sudo losetup /dev/loop1 /tmp/spare.img
sudo ./remap_create_safe.sh main_dev=/dev/loop0 spare_dev=/dev/loop1 dm_name=test_remap
```

### 2. Remap a Known Bad Sector
```bash
sudo dmsetup message test_remap 0 remap 2048
```

### 3. Run a Quick fio Test
```bash
sudo fio --name=remap_test --filename=/dev/mapper/test_remap \
         --direct=1 --rw=randrw --bs=4k --size=50M \
         --numjobs=1 --time_based --runtime=15 --group_reporting
```

### 4. Remove Mapping and Loop Devices
```bash
sudo dmsetup remove test_remap
sudo losetup -d /dev/loop0
sudo losetup -d /dev/loop1
```

### 5. Simulating Failures (No Real Bad Disk Needed)
You can simulate a bad sector by:
1. Creating a mapping as above.
2. Choosing a sector number within the main device range (e.g., `4096`).
3. Issuing a remap command for that sector:
   ```bash
   sudo dmsetup message test_remap 0 remap 4096
   ```
4. Writing to that sector:
   ```bash
   sudo dd if=/dev/zero of=/dev/mapper/test_remap bs=512 seek=4096 count=1
   ```
5. Observing in `/sys/kernel/debug/dm_remap/remap_table` that the sector is now mapped to a spare.

---

## 🧠 Design Overview
- Kernel module maintains an in‑memory mapping table.
- Remapped sectors are redirected to a spare pool defined at creation.
- I/O is intercepted in `map()` and redirected if needed.
- Wrapper and driver scripts add safety, reproducibility, and automation.

---

## 📦 Future Enhancements
- Persistent mapping table (on‑disk metadata)
- Automatic remapping on I/O failure
- Scrubbing and proactive bad block detection
- User‑space daemon for monitoring and remapping

---

## ⚠️ Limitations
- Mapping table is volatile (not persisted across reboots)
- No automatic detection of bad sectors (yet)
- Spare pool size is fixed at creation
- Not suitable for production use without further validation
---
## 📚 References

* [Device Mapper documentation](https://www.kernel.org/doc/html/latest/admin-guide/device-mapper/dm-usage.html)
* [Linux kernel source: drivers/md](https://github.com/torvalds/linux/tree/master/drivers/md)
* [fio: Flexible I/O tester](https://github.com/axboe/fio)
* [dmsetup man page](https://man7.org/linux/man-pages/man8/dmsetup.8.html)


## 📜 License
GPLv2 — Free to use, modify, and distribute.

---

## 👤 Author
Christian Roth
