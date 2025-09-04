# dm-remap

**dm-remap** is a custom Linux Device Mapper (DM) target that emulates firmware-level bad sector remapping in software. It allows you to dynamically redirect I/O from failing sectors to a reserved pool of spare sectors, creating a resilient virtual block device on top of degraded hardware.

## 🔧 Features

- Dynamic remapping of bad sectors via `dmsetup message`
- Configurable spare sector pool
- Transparent I/O redirection
- Simple mapping table stored in kernel memory
- Designed for salvaging aging or partially damaged drives

## 🚀 Getting Started

### 1. Build the Module

```bash  
make  
sudo insmod dm_remap.ko  
```

### 2. Create a Remapped Device

```bash  
dmsetup create remap0 --table "0 <usable_sectors> remap /dev/sdX <start_sector> <spare_start_sector>"  
```

Example:

```bash  
dmsetup create remap0 --table "0 2096152 remap /dev/sdX 0 2096152"  
```

This maps the first 2096152 sectors and reserves the last 1000 sectors for remapping.

### 3. Remap a Bad Sector

```bash  
dmsetup message remap0 0 remap <bad_sector>  
```

Example:

```bash  
dmsetup message remap0 0 remap 123456  
```

This redirects logical sector 123456 to the next available spare.

### 4. Test I/O

```bash  
# Write to the remapped sector  
dd if=/dev/zero of=/dev/mapper/remap0 bs=512 seek=123456 count=1  

# Read it back  
dd if=/dev/mapper/remap0 bs=512 skip=123456 count=1 | hexdump -C  
```

## 🧠 Design Overview

- The module maintains a simple in-memory mapping table.
- Remapped sectors are redirected to a spare pool defined at module creation.
- I/O is intercepted in the `map()` function and transparently redirected.
- Messages sent via `dmsetup` allow runtime remapping.

## 📦 Future Enhancements

- Persistent mapping table (stored on disk or metadata block)
- Automatic remapping on I/O failure
- Scrubbing and proactive bad block detection
- User-space daemon for monitoring and remapping

## ⚠️ Limitations

- Mapping table is volatile (not persisted across reboots)
- No automatic detection of bad sectors (yet)
- Spare pool size is fixed at module creation

## 📜 License

GPLv2 — Free to use, modify, and distribute.

## 👤 Author

Christian Roth
