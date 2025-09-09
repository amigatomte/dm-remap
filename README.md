# dm-remap

**dm-remap** is a custom Linux Device Mapper (DM) target that emulates firmware‑level bad sector remapping entirely in software.  
It was created for situations where a storage device is starting to fail — perhaps with a growing number of bad sectors — but you still need to keep it in service long enough to recover data, run legacy workloads, or extend its usable life.

On many drives, the firmware automatically remaps failing sectors to a hidden pool of spares. But when that firmware‑level remapping is absent, exhausted, or unreliable, the operating system will start seeing I/O errors. `dm-remap` provides a transparent, configurable remapping layer: you define a main device and a spare device (or region), and you can manually redirect reads and writes from specific logical sectors to healthy spare sectors.

Current focus is on a robust manual workflow for remapping and testing. Automatic remapping on I/O error is planned but not implemented yet.

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
- Manual remapping of bad sectors via `dmsetup message`
- Configurable spare sector pool
- Transparent I/O redirection once a sector is remapped
- Simple mapping table stored in kernel memory
- Safer, alignment‑aware device creation via `remap_create_safe.sh`
- Automated single/batch testing with `remap_test_driver.sh`

---

## 📊 Data flow diagram

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

Flow:
1. I/O request hits the main device mapping.  
2. `dm-remap` checks the remap table.  
3. If a logical sector is remapped, I/O is redirected to the spare device.  
4. Otherwise, I/O goes to the main device.  
5. You add mappings manually via `dmsetup message` (automatic on‑error remap is planned).

---

## 🚀 Getting started

### 1. Build and load the module
```bash
make
sudo insmod dm_remap.ko
```

### 2. Create a remapped device (safe wrapper)
```bash
sudo ./remap_create_safe.sh main_dev=/dev/sdX spare_dev=/dev/sdY \
    main_start=0 spare_start=0 \
    logfile=/tmp/remap_wrapper.log dm_name=my_remap
```

Key points:
- Arguments are order‑independent `key=value` pairs.
- `main_start` and `spare_start` default to `0`.
- `spare_total` defaults to full spare length minus `spare_start`.
- If `logfile` is omitted, one is auto‑generated in `/tmp/`.
- `dm_name` defaults to `test_remap` but can be overridden.

The wrapper:
- Verifies physical sector sizes match.
- Auto‑aligns offsets/lengths to the physical sector size.
- Exit codes: `0` (no adjustments), `1` (adjustments made), `2` (sector size mismatch).

### 3. Remap a bad sector manually
```bash
sudo dmsetup message my_remap 0 remap <logical_sector>
```

### 4. Test I/O
```bash
sudo dd if=/dev/zero of=/dev/mapper/my_remap bs=512 seek=123456 count=1
sudo dd if=/dev/mapper/my_remap bs=512 skip=123456 count=1 | hexdump -C
```

---

## 🧪 Automated testing

### Single run
```bash
./remap_test_driver.sh single 50 20 8k --dm-name=my_test
```

### Batch mode
```bash
./remap_test_driver.sh batch --pause --dm-name-prefix=remap
```

Features:
- Live monitoring of `/sys/kernel/debug/dm_remap/remap_table`
- Logs all changes (`[ADDED]` / `[REMOVED]`)
- Captures `dmesg` before/after runs
- Summaries with parameters and log file names
- Early quit (`q`) in batch mode with partial summary

---

## 📄 Example wrapper output

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

## 📄 Example test driver summary

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

- ERROR: Physical sector sizes differ  
  Fix: Use devices with matching physical sector sizes (`cat /sys/block/<dev>/queue/hw_sector_size`).

- dmsetup: ioctl ... failed: Device or resource busy  
  Fix: Remove old mapping with `sudo dmsetup remove <dm_name>` and ensure no process is using it.

- Permission denied  
  Fix: Run commands with `sudo`.

- Wrapper exits with code 1  
  Fix: Informational — wrapper auto‑adjusted values. Check the log.

- Wrapper exits with code 2  
  Fix: Physical sector sizes differ — choose compatible devices.

---

## 📋 Common usage patterns

### 1. Create loopback test devices
```bash
truncate -s 100M /tmp/main.img
truncate -s 100M /tmp/spare.img
sudo losetup /dev/loop0 /tmp/main.img
sudo losetup /dev/loop1 /tmp/spare.img
sudo ./remap_create_safe.sh main_dev=/dev/loop0 spare_dev=/dev/loop1 dm_name=test_remap
```

### 2. Remap a known bad sector (manual)
```bash
sudo dmsetup message test_remap 0 remap 2048
```

### 3. Run a quick fio test
```bash
sudo fio --name=remap_test --filename=/dev/mapper/test_remap \
         --direct=1 --rw=randrw --bs=4k --size=50M \
         --numjobs=1 --time_based --runtime=15 --group_reporting
```

### 4. Remove mapping and loop devices
```bash
sudo dmsetup remove test_remap
sudo losetup -d /dev/loop0
sudo losetup -d /dev/loop1
```

### 5. Simulating failures (no real bad disk needed)
1. Create a mapping (as above).  
2. Pick a logical sector within range (e.g., `4096`).  
3. Remap it:
   ```bash
   sudo dmsetup message test_remap 0 remap 4096
   ```
4. Write to that sector:
   ```bash
   sudo dd if=/dev/zero of=/dev/mapper/test_remap bs=512 seek=4096 count=1
   ```
5. Observe `/sys/kernel/debug/dm_remap/remap_table` to see the mapping.

---

## 🧠 Design overview
- Kernel module maintains an in‑memory mapping table.
- Remapped sectors are redirected to a spare pool defined at creation.
- I/O is intercepted in `map()` and redirected if needed.
- Wrapper and driver scripts add safety, reproducibility, and automation.

---

## 📦 Future enhancements
- Automatic detection/remap on I/O error from the main device
- Persistent mapping table (on‑disk metadata)
- Scrubbing and proactive bad block detection policies
- User‑space daemon for monitoring, policy control, and reporting

---

## ⚠️ Limitations
- Mapping table is volatile (not persisted across reboots)
- Spare pool size is fixed at creation

---

## 📚 References

* [Device Mapper documentation](https://www.kernel.org/doc/html/latest/admin-guide/device-mapper/dm-usage.html)
* [Linux kernel source: drivers/md](https://github.com/torvalds/linux/tree/master/drivers/md)
* [fio: Flexible I/O tester](https://github.com/axboe/fio)
* [dmsetup man page](https://man7.org/linux/man-pages/man8/dmsetup.8.html)

---

## 📜 License
GPLv2 — Free to use, modify, and distribute.

---

## 👤 Author
Christian Roth
