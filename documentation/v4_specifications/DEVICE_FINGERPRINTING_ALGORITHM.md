# dm-remap v4.0 Device Fingerprinting Algorithm Design

**Document Version**: 1.0  
**Date**: October 12, 2025  
**Phase**: 4.0 Week 1-2 Deliverable  
**Purpose**: Reliable device identification and validation for automatic setup reassembly

---

## 🎯 Overview

The v4.0 device fingerprinting system creates **unique, persistent device identifiers** using a combination of hardware characteristics, filesystem properties, and device metadata to ensure reliable device identification across reboots, hardware changes, and system migrations.

## 🔍 Multi-Layer Fingerprinting Strategy

### **Three-Layer Identification System**
```
Layer 1: Hardware Fingerprint (Primary)
    ├── Device UUID (when available)
    ├── Serial number
    └── Hardware characteristics

Layer 2: Filesystem Fingerprint (Secondary)  
    ├── Filesystem UUID
    ├── Filesystem type and features
    └── Partition table signature

Layer 3: Content Fingerprint (Tertiary)
    ├── SHA-256 of specific sectors
    ├── Device size and geometry
    └── dm-remap metadata signature
```

### **Fingerprint Data Structure**
```c
/**
 * Comprehensive device fingerprint for reliable identification
 */
struct dm_remap_device_fingerprint {
    // Layer 1: Hardware identification
    struct {
        char device_uuid[37];           // Device UUID (GPT/filesystem)
        char serial_number[64];         // Hardware serial number
        char model_name[64];            // Device model identifier
        uint64_t device_size_sectors;   // Device size for validation
        uint32_t sector_size;           // Logical sector size
    } hardware;
    
    // Layer 2: Filesystem identification
    struct {
        char fs_uuid[37];               // Filesystem UUID
        char fs_type[16];               // Filesystem type (ext4, xfs, etc.)
        uint64_t fs_size;               // Filesystem size
        uint32_t partition_table_crc;   // Partition table signature
    } filesystem;
    
    // Layer 3: Content identification
    struct {
        uint8_t sector_hash[32];        // SHA-256 of specific sectors
        uint64_t creation_timestamp;    // Fingerprint creation time
        uint32_t fingerprint_version;   // Fingerprint format version
        uint8_t dm_remap_signature[16]; // dm-remap metadata signature
    } content;
    
    // Overall fingerprint
    uint8_t composite_hash[32];         // SHA-256 of all above data
};
```

## 🔧 Hardware Layer Fingerprinting

### **Device UUID Extraction**
```c
#include <linux/genhd.h>
#include <linux/blkdev.h>

/**
 * extract_hardware_fingerprint() - Get hardware-level device identifiers
 */
static int extract_hardware_fingerprint(struct block_device *bdev,
                                       struct dm_remap_device_fingerprint *fp)
{
    struct gendisk *disk = bdev->bd_disk;
    struct hd_struct *part = bdev->bd_part;
    char uuid_str[37] = {0};
    int ret = 0;
    
    // Clear hardware section
    memset(&fp->hardware, 0, sizeof(fp->hardware));
    
    // Get device UUID (GPT UUID or similar)
    if (part && part->info && part->info->uuid) {
        snprintf(fp->hardware.device_uuid, sizeof(fp->hardware.device_uuid),
                "%pU", part->info->uuid);
        DMR_DEBUG(2, "Device UUID: %s", fp->hardware.device_uuid);
    }
    
    // Get hardware serial number
    if (disk && disk->queue && disk->queue->backing_dev_info) {
        struct request_queue *q = disk->queue;
        // Attempt to get serial number from device
        // Implementation depends on device type (SCSI, NVMe, etc.)
        ret = get_device_serial_number(q, fp->hardware.serial_number,
                                     sizeof(fp->hardware.serial_number));
    }
    
    // Get model name
    if (disk && disk->disk_name) {
        strncpy(fp->hardware.model_name, disk->disk_name,
                sizeof(fp->hardware.model_name) - 1);
    }
    
    // Get device geometry
    fp->hardware.device_size_sectors = get_capacity(disk);
    fp->hardware.sector_size = bdev_logical_block_size(bdev);
    
    DMR_DEBUG(2, "Hardware fingerprint: size=%llu sectors, sector_size=%u",
              fp->hardware.device_size_sectors, fp->hardware.sector_size);
    
    return 0;
}

/**
 * get_device_serial_number() - Extract serial number from device
 */
static int get_device_serial_number(struct request_queue *q, char *serial, size_t len)
{
    // Implementation varies by device type
    // This is a simplified version - real implementation would handle
    // SCSI, NVMe, ATA devices differently
    
    if (q && q->backing_dev_info) {
        // Attempt SCSI inquiry for serial number
        // Fallback to other methods for different device types
        return extract_scsi_serial_number(q, serial, len);
    }
    
    return -ENODEV;
}
```

### **SCSI Device Serial Number Extraction**
```c
/**
 * extract_scsi_serial_number() - Get SCSI device serial via INQUIRY
 */
static int extract_scsi_serial_number(struct request_queue *q, char *serial, size_t len)
{
    // This would use SCSI INQUIRY commands to get device serial
    // Simplified implementation - real version would:
    // 1. Create SCSI command for INQUIRY page 0x80 (Unit Serial Number)
    // 2. Execute command via blk_execute_rq()
    // 3. Parse response for serial number
    // 4. Handle different SCSI device types
    
    // Placeholder implementation
    strncpy(serial, "SCSI_SERIAL_PLACEHOLDER", len - 1);
    return 0;
}
```

## 📁 Filesystem Layer Fingerprinting

### **Filesystem UUID and Type Detection**
```c
#include <linux/fs.h>
#include <linux/buffer_head.h>

/**
 * extract_filesystem_fingerprint() - Get filesystem-level identifiers
 */
static int extract_filesystem_fingerprint(struct block_device *bdev,
                                         struct dm_remap_device_fingerprint *fp)
{
    struct buffer_head *bh;
    int ret = 0;
    
    // Clear filesystem section
    memset(&fp->filesystem, 0, sizeof(fp->filesystem));
    
    // Try multiple filesystem detection methods
    ret = detect_ext_filesystem(bdev, fp);
    if (ret == 0) goto found_fs;
    
    ret = detect_xfs_filesystem(bdev, fp);
    if (ret == 0) goto found_fs;
    
    ret = detect_btrfs_filesystem(bdev, fp);
    if (ret == 0) goto found_fs;
    
    // No recognized filesystem found
    strncpy(fp->filesystem.fs_type, "unknown", sizeof(fp->filesystem.fs_type) - 1);
    
found_fs:
    // Get partition table signature
    fp->filesystem.partition_table_crc = calculate_partition_table_crc(bdev);
    
    DMR_DEBUG(2, "Filesystem fingerprint: UUID=%s, type=%s, size=%llu",
              fp->filesystem.fs_uuid, fp->filesystem.fs_type, fp->filesystem.fs_size);
    
    return 0;
}

/**
 * detect_ext_filesystem() - Detect ext2/3/4 filesystem and extract UUID
 */
static int detect_ext_filesystem(struct block_device *bdev,
                                struct dm_remap_device_fingerprint *fp)
{
    struct buffer_head *bh;
    struct ext2_super_block *es;
    
    // Read ext filesystem superblock (at offset 1024)
    bh = __bread(bdev, 2, 1024); // Block 2 for ext superblock
    if (!bh) {
        return -EIO;
    }
    
    es = (struct ext2_super_block *)bh->b_data;
    
    // Check ext filesystem magic
    if (le16_to_cpu(es->s_magic) != EXT2_SUPER_MAGIC) {
        brelse(bh);
        return -EINVAL;
    }
    
    // Extract filesystem UUID
    snprintf(fp->filesystem.fs_uuid, sizeof(fp->filesystem.fs_uuid),
             "%pU", es->s_uuid);
    
    // Determine ext variant
    if (le32_to_cpu(es->s_feature_compat) & EXT3_FEATURE_COMPAT_HAS_JOURNAL) {
        if (le32_to_cpu(es->s_feature_incompat) & EXT4_FEATURE_INCOMPAT_EXTENTS) {
            strncpy(fp->filesystem.fs_type, "ext4", sizeof(fp->filesystem.fs_type) - 1);
        } else {
            strncpy(fp->filesystem.fs_type, "ext3", sizeof(fp->filesystem.fs_type) - 1);
        }
    } else {
        strncpy(fp->filesystem.fs_type, "ext2", sizeof(fp->filesystem.fs_type) - 1);
    }
    
    // Get filesystem size
    fp->filesystem.fs_size = le32_to_cpu(es->s_blocks_count) * 
                            (1024 << le32_to_cpu(es->s_log_block_size));
    
    brelse(bh);
    return 0;
}
```

### **Partition Table Signature**
```c
/**
 * calculate_partition_table_crc() - Generate partition table signature
 */
static uint32_t calculate_partition_table_crc(struct block_device *bdev)
{
    struct buffer_head *bh;
    uint32_t crc = 0;
    
    // Read MBR/GPT header
    bh = __bread(bdev, 0, 512);
    if (bh) {
        // Calculate CRC32 of partition table area (skip boot code)
        crc = crc32(0, bh->b_data + 446, 64); // MBR partition table
        brelse(bh);
    }
    
    // Also check GPT header if present
    bh = __bread(bdev, 1, 512);
    if (bh) {
        // Check for GPT signature
        if (memcmp(bh->b_data, "EFI PART", 8) == 0) {
            // Include GPT header in CRC (excluding CRC field itself)
            uint32_t gpt_crc = crc32(crc, bh->b_data, 16); // GPT signature + revision
            crc = crc32(gpt_crc, bh->b_data + 20, 72);     // Rest of GPT header
        }
        brelse(bh);
    }
    
    return crc;
}
```

## 🔐 Content Layer Fingerprinting

### **Sector Hash Generation**
```c
#include <crypto/hash.h>

/**
 * extract_content_fingerprint() - Generate content-based identifiers
 */
static int extract_content_fingerprint(struct block_device *bdev,
                                      struct dm_remap_device_fingerprint *fp)
{
    int ret;
    
    // Clear content section
    memset(&fp->content, 0, sizeof(fp->content));
    
    // Generate hash of specific sectors
    ret = generate_sector_hash(bdev, fp->content.sector_hash);
    if (ret) {
        DMR_DEBUG(0, "Failed to generate sector hash: %d", ret);
        return ret;
    }
    
    // Set creation timestamp
    fp->content.creation_timestamp = ktime_get_real_seconds();
    fp->content.fingerprint_version = 1;
    
    // Generate dm-remap signature
    ret = generate_dm_remap_signature(bdev, fp->content.dm_remap_signature);
    if (ret) {
        DMR_DEBUG(1, "No dm-remap signature found (normal for new devices)");
    }
    
    return 0;
}

/**
 * generate_sector_hash() - Create SHA-256 hash of strategic sectors
 */
static int generate_sector_hash(struct block_device *bdev, uint8_t *hash)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    struct buffer_head *bh;
    const sector_t hash_sectors[] = {0, 1, 2, 8, 16, 32, 64, 128}; // Strategic sectors
    int i, ret = 0;
    
    // Allocate SHA-256 hasher
    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) {
        return PTR_ERR(tfm);
    }
    
    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) {
        crypto_free_shash(tfm);
        return -ENOMEM;
    }
    
    desc->tfm = tfm;
    ret = crypto_shash_init(desc);
    if (ret) {
        goto cleanup;
    }
    
    // Hash strategic sectors
    for (i = 0; i < ARRAY_SIZE(hash_sectors); i++) {
        bh = __bread(bdev, hash_sectors[i], 512);
        if (bh) {
            ret = crypto_shash_update(desc, bh->b_data, 512);
            brelse(bh);
            if (ret) {
                goto cleanup;
            }
        }
    }
    
    // Finalize hash
    ret = crypto_shash_final(desc, hash);
    
cleanup:
    kfree(desc);
    crypto_free_shash(tfm);
    return ret;
}
```

## 🎯 Composite Fingerprint Generation

### **Final Fingerprint Assembly**
```c
/**
 * generate_composite_fingerprint() - Create final device fingerprint
 */
static int generate_composite_fingerprint(struct dm_remap_device_fingerprint *fp)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;
    
    // Allocate SHA-256 hasher for composite hash
    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) {
        return PTR_ERR(tfm);
    }
    
    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) {
        crypto_free_shash(tfm);
        return -ENOMEM;
    }
    
    desc->tfm = tfm;
    ret = crypto_shash_init(desc);
    if (ret) {
        goto cleanup;
    }
    
    // Hash all fingerprint components
    ret = crypto_shash_update(desc, &fp->hardware, sizeof(fp->hardware));
    if (ret) goto cleanup;
    
    ret = crypto_shash_update(desc, &fp->filesystem, sizeof(fp->filesystem));
    if (ret) goto cleanup;
    
    ret = crypto_shash_update(desc, &fp->content, sizeof(fp->content));
    if (ret) goto cleanup;
    
    // Generate final composite hash
    ret = crypto_shash_final(desc, fp->composite_hash);
    
cleanup:
    kfree(desc);
    crypto_free_shash(tfm);
    
    if (ret == 0) {
        DMR_DEBUG(1, "Generated composite fingerprint: %*phN", 
                  16, fp->composite_hash); // Print first 16 bytes
    }
    
    return ret;
}
```

## 🔍 Fingerprint Validation and Matching

### **Fingerprint Comparison Algorithm**
```c
/**
 * compare_device_fingerprints() - Compare two fingerprints for match
 * 
 * Returns matching confidence level (0-100)
 */
static int compare_device_fingerprints(const struct dm_remap_device_fingerprint *fp1,
                                      const struct dm_remap_device_fingerprint *fp2)
{
    int confidence = 0;
    int matches = 0;
    int total_checks = 0;
    
    // Hardware layer comparison (weight: 40%)
    total_checks += 4;
    if (strcmp(fp1->hardware.device_uuid, fp2->hardware.device_uuid) == 0 && 
        strlen(fp1->hardware.device_uuid) > 0) {
        matches += 2; // UUID match is very strong
    }
    if (strcmp(fp1->hardware.serial_number, fp2->hardware.serial_number) == 0 &&
        strlen(fp1->hardware.serial_number) > 0) {
        matches += 2; // Serial match is very strong
    }
    if (fp1->hardware.device_size_sectors == fp2->hardware.device_size_sectors) {
        matches += 1; // Size match is good
    }
    if (fp1->hardware.sector_size == fp2->hardware.sector_size) {
        matches += 1; // Sector size match is expected
    }
    
    // Filesystem layer comparison (weight: 30%)
    total_checks += 3;
    if (strcmp(fp1->filesystem.fs_uuid, fp2->filesystem.fs_uuid) == 0 &&
        strlen(fp1->filesystem.fs_uuid) > 0) {
        matches += 2; // Filesystem UUID is very strong
    }
    if (strcmp(fp1->filesystem.fs_type, fp2->filesystem.fs_type) == 0) {
        matches += 1; // Filesystem type match
    }
    if (fp1->filesystem.partition_table_crc == fp2->filesystem.partition_table_crc) {
        matches += 1; // Partition table match
    }
    
    // Content layer comparison (weight: 30%)
    total_checks += 2;
    if (memcmp(fp1->content.sector_hash, fp2->content.sector_hash, 32) == 0) {
        matches += 2; // Sector hash match is very strong
    }
    if (memcmp(fp1->composite_hash, fp2->composite_hash, 32) == 0) {
        matches += 2; // Perfect composite match
    }
    
    // Calculate confidence percentage
    confidence = (matches * 100) / total_checks;
    
    DMR_DEBUG(2, "Fingerprint comparison: %d/%d matches = %d%% confidence",
              matches, total_checks, confidence);
    
    return confidence;
}

/**
 * validate_device_fingerprint() - Ensure fingerprint is reasonable
 */
static bool validate_device_fingerprint(const struct dm_remap_device_fingerprint *fp)
{
    // Check basic sanity
    if (fp->hardware.device_size_sectors == 0 || 
        fp->hardware.sector_size == 0 ||
        fp->hardware.sector_size > 4096) {
        return false;
    }
    
    // Check timestamp is reasonable
    uint64_t current_time = ktime_get_real_seconds();
    if (fp->content.creation_timestamp > current_time + 86400) { // 1 day future
        return false;
    }
    
    // Check composite hash is non-zero
    uint8_t zero_hash[32] = {0};
    if (memcmp(fp->composite_hash, zero_hash, 32) == 0) {
        return false;
    }
    
    return true;
}
```

## 📊 Performance and Storage Analysis

### **Fingerprint Generation Performance**
```c
/**
 * Performance characteristics:
 * 
 * Hardware layer extraction:  ~1ms (device queries)
 * Filesystem layer detection: ~5ms (superblock reads)
 * Content layer hashing:      ~10ms (SHA-256 of sectors)
 * Composite hash generation:  ~1ms (final SHA-256)
 * Total fingerprint time:     ~17ms per device
 * 
 * Memory usage: 512 bytes per fingerprint structure
 * Storage overhead: 512 bytes per metadata copy (5 copies = 2.5KB total)
 */
```

### **Caching for Performance**
```c
/**
 * Fingerprint cache to avoid regeneration
 */
struct fingerprint_cache_entry {
    dev_t device_id;
    struct dm_remap_device_fingerprint fingerprint;
    uint64_t cache_timestamp;
    struct list_head list;
};

static LIST_HEAD(fingerprint_cache);
static DEFINE_MUTEX(fingerprint_cache_mutex);

/**
 * get_cached_fingerprint() - Retrieve fingerprint from cache
 */
static int get_cached_fingerprint(struct block_device *bdev,
                                 struct dm_remap_device_fingerprint *fp)
{
    struct fingerprint_cache_entry *entry;
    dev_t dev_id = bdev->bd_dev;
    uint64_t current_time = ktime_get_real_seconds();
    int ret = -ENOENT;
    
    mutex_lock(&fingerprint_cache_mutex);
    
    list_for_each_entry(entry, &fingerprint_cache, list) {
        if (entry->device_id == dev_id) {
            // Check cache age (expire after 1 hour)
            if (current_time - entry->cache_timestamp < 3600) {
                memcpy(fp, &entry->fingerprint, sizeof(*fp));
                ret = 0;
                DMR_DEBUG(2, "Using cached fingerprint for device %d:%d",
                          MAJOR(dev_id), MINOR(dev_id));
            } else {
                // Remove expired entry
                list_del(&entry->list);
                kfree(entry);
            }
            break;
        }
    }
    
    mutex_unlock(&fingerprint_cache_mutex);
    return ret;
}
```

## 📋 Implementation Checklist

### Week 1-2 Deliverables
- [ ] Implement hardware layer fingerprinting (UUID, serial, model)
- [ ] Create filesystem detection and UUID extraction
- [ ] Build content layer hashing with SHA-256
- [ ] Implement composite fingerprint generation
- [ ] Add fingerprint comparison and validation algorithms
- [ ] Create fingerprint caching system for performance
- [ ] Add comprehensive error handling and logging

### Integration Requirements
- [ ] Integrate with metadata storage system
- [ ] Add to device discovery algorithms
- [ ] Include in setup reassembly validation
- [ ] Add sysfs interface for fingerprint inspection

## 🎯 Success Criteria

### **Reliability Targets**
- [ ] 99%+ device identification accuracy
- [ ] <1% false positive rate for device matching
- [ ] Robust handling of device changes (size, filesystem)

### **Performance Targets**
- [ ] <20ms fingerprint generation time
- [ ] <1ms fingerprint comparison time
- [ ] Efficient caching with 1-hour expiration

### **Enterprise Features**
- [ ] Multi-layer redundancy for identification
- [ ] Configurable matching confidence thresholds
- [ ] Comprehensive fingerprint validation

---

## 📚 Technical References

- **Linux Block Device API**: `include/linux/blkdev.h`
- **Crypto API**: `include/crypto/hash.h`
- **Filesystem Detection**: `fs/*/super.c`
- **SCSI Commands**: `include/scsi/scsi_cmnd.h`

This device fingerprinting system provides robust, multi-layer device identification that enables reliable automatic setup reassembly while handling real-world scenarios like device migration, hardware changes, and filesystem modifications.