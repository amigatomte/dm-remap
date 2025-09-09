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

Within a minute you’ll have a working `/dev/mapper/my_remap` device, a log file in `/tmp/`, and a test run completed.

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

**Flow:**  
1. I/O request hits the main device mapping.  
2. `dm-remap` checks the remap table.  
3. If the sector is remapped, I/O is redirected to the spare device.  
4. Otherwise, it proceeds to the main device.

---

## 🚀 Getting Started

### 1. Build and Load the Module
```bash
make
sudo insmod dm_remap.ko
```

---

### 2. Create a Remapped Device (Safe Wrapper)
```bash
sudo ./remap_create_safe.sh main_dev=/dev/sdX spare_dev=/dev/sdY \
    main_start=0 spare_start=0 \
    logfile=/tmp/remap_wrapper.log dm_name=my_remap
```

**Key points:**
- Arguments are **order‑independent** `key=value` pairs.
- `main_start` and `spare_start` default to `0` if omitted.
- `spare_total` defaults to the full spare device length minus `spare_start`.
- If `logfile` is omitted, one is auto‑generated in `/tmp/`.
- `dm_name` defaults to `test_remap` but can be overridden.

The wrapper:
- Checks physical sector size match.
- Auto‑aligns offsets/lengths to physical sector size.
- Returns exit codes:
  - `0` = success, no adjustments
  - `1` = success, adjustments made
  - `2` = aborted due to sector size mismatch

---

### 3. Remap a Bad Sector
```bash
sudo dmsetup message my_remap 0 remap <bad_sector>
```
Example:
```bash
sudo dmsetup message my_remap 0 remap 123456
```

---

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

**Features:**
- Live monitoring of `/sys/kernel/debug/dm_remap/remap_table`
- Logs all changes (`[ADDED]` / `[REMOVED]`)
- Captures `dmesg` output before/after runs
- Summarises completed runs with parameters and log file names
- Supports early quit (`q`) in batch mode with partial summary

---

## 📄 Example Wrapper Output

**Live stdout:**
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

---

## 📜 License
GPLv2 — Free to use, modify, and distribute.

---

## 👤 Author
Christian Roth
