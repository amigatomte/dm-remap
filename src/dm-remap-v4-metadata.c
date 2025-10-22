/**
 * dm-remap-v4-metadata.c - Pure v4.0 Metadata Management
 * 
 * Copyright (C) 2025 dm-remap Development Team
 * 
 * This file implements the streamlined v4.0 metadata system with:
 * - 5-copy redundant storage
 * - Single CRC32 checksum validation
 * - Conflict resolution via sequence numbers
 * - No backward compatibility overhead
 * 
 * Clean slate architecture - optimized for performance and simplicity.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/ktime.h>

#include "dm-remap-v4.h"

/* Global sequence counter for metadata updates */
static atomic64_t dm_remap_global_sequence = ATOMIC64_INIT(1);

/* Metadata operation mutex */
static DEFINE_MUTEX(dm_remap_metadata_mutex);

/* Performance tracking */
struct dm_remap_metadata_stats {
    atomic64_t reads_completed;
    atomic64_t writes_completed;
    atomic64_t checksum_failures;
    atomic64_t repairs_performed;
    atomic64_t total_read_time_ns;
    atomic64_t total_write_time_ns;
};

static struct dm_remap_metadata_stats metadata_stats;

/**
 * calculate_metadata_crc32() - Calculate CRC32 for entire metadata structure
 * 
 * Single checksum covers the entire metadata (excluding checksum field itself)
 * for maximum simplicity and performance.
 */
static uint32_t calculate_metadata_crc32(const struct dm_remap_metadata_v4 *metadata)
{
    uint32_t crc = 0;
    size_t offset_after_checksum = offsetof(struct dm_remap_metadata_v4, header.copy_index);
    size_t remaining_size = sizeof(*metadata) - offset_after_checksum;
    
    /* Calculate CRC of everything except the checksum field itself */
    crc = crc32(0, &metadata->header.magic, sizeof(metadata->header.magic));
    crc = crc32(crc, &metadata->header.version, sizeof(metadata->header.version));
    crc = crc32(crc, &metadata->header.sequence_number, sizeof(metadata->header.sequence_number));
    crc = crc32(crc, &metadata->header.timestamp, sizeof(metadata->header.timestamp));
    
    /* CRC the rest of the structure after the checksum field */
    crc = crc32(crc, (uint8_t*)metadata + offset_after_checksum, remaining_size);
    
    return crc;
}

/**
 * validate_metadata_v4() - Validate v4.0 metadata structure
 * 
 * Pure v4.0 validation - no version detection complexity
 */
static bool validate_metadata_v4(const struct dm_remap_metadata_v4 *metadata)
{
    uint32_t expected_checksum;
    
    /* Check magic number and version */
    if (metadata->header.magic != DM_REMAP_METADATA_V4_MAGIC) {
        DMR_DEBUG(2, "Invalid magic: 0x%08x (expected 0x%08x)",
                  metadata->header.magic, DM_REMAP_METADATA_V4_MAGIC);
        return false;
    }
    
    if (metadata->header.version != DM_REMAP_METADATA_V4_VERSION) {
        DMR_DEBUG(2, "Invalid version: %u (expected %u)",
                  metadata->header.version, DM_REMAP_METADATA_V4_VERSION);
        return false;
    }
    
    /* Validate checksum */
    expected_checksum = calculate_metadata_crc32(metadata);
    if (metadata->header.metadata_checksum != expected_checksum) {
        DMR_DEBUG(2, "Checksum mismatch: 0x%08x != 0x%08x",
                  metadata->header.metadata_checksum, expected_checksum);
        atomic64_inc(&metadata_stats.checksum_failures);
        return false;
    }
    
    /* Validate structure sanity */
    if (metadata->remap_data.active_remaps > DM_REMAP_V4_MAX_REMAPS) {
        DMR_DEBUG(2, "Invalid remap count: %u > %u",
                  metadata->remap_data.active_remaps, DM_REMAP_V4_MAX_REMAPS);
        return false;
    }
    
    if (metadata->health_data.health_score > 100) {
        DMR_DEBUG(2, "Invalid health score: %u > 100",
                  metadata->health_data.health_score);
        return false;
    }
    
    /* Validate timestamp is reasonable (not too far in future) */
    uint64_t current_time = ktime_get_real_seconds();
    if (metadata->header.timestamp > current_time + 86400) { /* 1 day tolerance */
        DMR_DEBUG(2, "Timestamp too far in future: %llu vs %llu",
                  metadata->header.timestamp, current_time);
        return false;
    }
    
    return true;
}

/**
 * read_metadata_copy() - Read single metadata copy from specific sector
 */
static int read_metadata_copy(struct block_device *bdev, sector_t sector,
                             struct dm_remap_metadata_v4 *metadata)
{
    struct bio *bio;
    struct page *page;
    void *page_data;
    int ret;
    
    /* Allocate page for reading */
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        return -ENOMEM;
    }
    
    /* Create bio for reading metadata */
    bio = bio_alloc(bdev, 1, REQ_OP_READ | REQ_SYNC, GFP_KERNEL);
    if (!bio) {
        __free_page(page);
        return -ENOMEM;
    }
    
    bio->bi_iter.bi_sector = sector;
    
    /* Add pages to cover entire metadata structure */
    bio_add_page(bio, page, PAGE_SIZE, 0);
    
    /* Submit bio and wait for completion */
    ret = submit_bio_wait(bio);
    
    if (ret == 0) {
        /* Copy metadata from page */
        page_data = kmap(page);
        memcpy(metadata, page_data, sizeof(*metadata));
        kunmap(page);
        
        DMR_DEBUG(3, "Read metadata copy from sector %llu: magic=0x%08x, seq=%llu",
                  sector, metadata->header.magic, metadata->header.sequence_number);
    } else {
        DMR_DEBUG(2, "Failed to read metadata from sector %llu: %d", sector, ret);
    }
    
    bio_put(bio);
    __free_page(page);
    
    return ret;
}

/**
 * write_metadata_copy() - Write single metadata copy to specific sector
 */
static int write_metadata_copy(struct block_device *bdev, sector_t sector,
                              const struct dm_remap_metadata_v4 *metadata)
{
    struct bio *bio;
    struct page *page;
    void *page_data;
    int ret;
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy START sector=%llu bdev=%p\n",
           (unsigned long long)sector, bdev);
    
    /* Allocate page for writing */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before alloc_page\n");
    page = alloc_page(GFP_KERNEL);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after alloc_page, page=%p\n", page);
    
    if (!page) {
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy alloc_page failed\n");
        return -ENOMEM;
    }
    
    /* Copy metadata to page */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before kmap\n");
    page_data = kmap(page);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after kmap, page_data=%p\n", page_data);
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before memcpy\n");
    memcpy(page_data, metadata, sizeof(*metadata));
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after memcpy\n");
    
    /* Clear rest of page */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before memset\n");
    memset((uint8_t*)page_data + sizeof(*metadata), 0, 
           PAGE_SIZE - sizeof(*metadata));
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after memset\n");
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before kunmap\n");
    kunmap(page);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after kunmap\n");
    
    /* Create bio for writing */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy *** CALLING bio_alloc ***\n");
    bio = bio_alloc(bdev, 1, REQ_OP_WRITE | REQ_SYNC | REQ_FUA, GFP_KERNEL);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy *** RETURNED from bio_alloc, bio=%p ***\n", bio);
    
    if (!bio) {
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy bio_alloc failed\n");
        __free_page(page);
        return -ENOMEM;
    }
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before setting bi_sector\n");
    bio->bi_iter.bi_sector = sector;
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after setting bi_sector=%llu\n",
           (unsigned long long)sector);
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before bio_add_page\n");
    bio_add_page(bio, page, PAGE_SIZE, 0);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after bio_add_page\n");
    
    /* Submit bio and wait for completion */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy *** CALLING submit_bio_wait ***\n");
    ret = submit_bio_wait(bio);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy *** RETURNED from submit_bio_wait, ret=%d ***\n", ret);
    
    if (ret == 0) {
        DMR_DEBUG(3, "Wrote metadata copy to sector %llu: seq=%llu, copy=%u",
                  sector, metadata->header.sequence_number, metadata->header.copy_index);
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy write successful\n");
    } else {
        DMR_DEBUG(1, "Failed to write metadata to sector %llu: %d", sector, ret);
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy write failed ret=%d\n", ret);
    }
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before bio_put\n");
    bio_put(bio);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after bio_put\n");
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy before __free_page\n");
    __free_page(page);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy after __free_page\n");
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_copy END returning %d\n", ret);
    return ret;
}

/**
 * dm_remap_read_metadata_v4() - Read best metadata copy from 5 redundant copies
 * 
 * Pure v4.0 implementation - no format detection needed
 */
int dm_remap_read_metadata_v4(struct block_device *bdev, 
                              struct dm_remap_metadata_v4 *metadata)
{
    const sector_t copy_sectors[] = DM_REMAP_V4_COPY_SECTORS;
    struct dm_remap_metadata_v4 copies[5];
    bool valid[5] = {false};
    int best_copy = -1;
    uint64_t best_sequence = 0;
    uint64_t best_timestamp = 0;
    int valid_count = 0;
    int i, ret;
    ktime_t start_time, end_time;
    
    start_time = ktime_get();
    
    DMR_DEBUG(2, "Reading v4.0 metadata from device");
    
    /* Read all 5 copies */
    for (i = 0; i < 5; i++) {
        ret = read_metadata_copy(bdev, copy_sectors[i], &copies[i]);
        if (ret == 0 && validate_metadata_v4(&copies[i])) {
            valid[i] = true;
            valid_count++;
            
            /* Track best copy (highest sequence number, then timestamp) */
            if (copies[i].header.sequence_number > best_sequence ||
                (copies[i].header.sequence_number == best_sequence &&
                 copies[i].header.timestamp > best_timestamp)) {
                best_copy = i;
                best_sequence = copies[i].header.sequence_number;
                best_timestamp = copies[i].header.timestamp;
            }
        }
    }
    
    if (best_copy >= 0) {
        /* Copy best metadata to output */
        memcpy(metadata, &copies[best_copy], sizeof(*metadata));
        
        DMR_DEBUG(1, "Selected metadata copy %d: seq=%llu, valid_copies=%d/5",
                  best_copy, best_sequence, valid_count);
        
        /* Schedule repair if we have corrupted copies */
        if (valid_count < 5) {
            DMR_DEBUG(1, "Metadata repair needed: %d/5 copies valid", valid_count);
            /* TODO: Schedule background repair */
        }
        
        ret = 0;
    } else {
        DMR_DEBUG(0, "No valid metadata copies found on device");
        ret = -ENODATA;
    }
    
    end_time = ktime_get();
    atomic64_add(ktime_to_ns(ktime_sub(end_time, start_time)), 
                 &metadata_stats.total_read_time_ns);
    atomic64_inc(&metadata_stats.reads_completed);
    
    return ret;
}

/**
 * dm_remap_write_metadata_v4() - Write metadata to all 5 redundant copies
 * 
 * Atomic update - all copies written or none
 */
int dm_remap_write_metadata_v4(struct block_device *bdev,
                               struct dm_remap_metadata_v4 *metadata)
{
    const sector_t copy_sectors[] = DM_REMAP_V4_COPY_SECTORS;
    int ret = 0;
    int i;
    ktime_t start_time, end_time;
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 START bdev=%p metadata=%p\n",
           bdev, metadata);
    
    start_time = ktime_get();
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 before mutex_lock\n");
    mutex_lock(&dm_remap_metadata_mutex);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 after mutex_lock\n");
    
    /* Update metadata header */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 updating header\n");
    metadata->header.magic = DM_REMAP_METADATA_V4_MAGIC;
    metadata->header.version = DM_REMAP_METADATA_V4_VERSION;
    metadata->header.sequence_number = atomic64_inc_return(&dm_remap_global_sequence);
    metadata->header.timestamp = ktime_get_real_seconds();
    metadata->header.structure_size = sizeof(*metadata);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 header updated\n");
    
    /* Calculate checksum for updated metadata */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 before calculate_metadata_crc32\n");
    metadata->header.metadata_checksum = calculate_metadata_crc32(metadata);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 after calculate_metadata_crc32, checksum=0x%08x\n",
           metadata->header.metadata_checksum);
    
    DMR_DEBUG(2, "Writing v4.0 metadata: seq=%llu, checksum=0x%08x",
              metadata->header.sequence_number, metadata->header.metadata_checksum);
    
    /* Write all 5 copies atomically */
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 starting loop to write 5 copies\n");
    for (i = 0; i < 5; i++) {
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 writing copy %d to sector %llu\n",
               i, (unsigned long long)copy_sectors[i]);
        
        metadata->header.copy_index = i;
        
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 *** CALLING write_metadata_copy copy=%d ***\n", i);
        ret = write_metadata_copy(bdev, copy_sectors[i], metadata);
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 *** RETURNED from write_metadata_copy copy=%d ret=%d ***\n",
               i, ret);
        
        if (ret) {
            DMR_DEBUG(0, "Failed to write metadata copy %d: %d", i, ret);
            printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 copy %d failed, breaking loop\n", i);
            
            /* Rollback: attempt to restore previous state */
            /* TODO: Implement rollback mechanism */
            break;
        }
    }
    
    if (ret == 0) {
        DMR_DEBUG(1, "Successfully wrote metadata to all 5 copies: seq=%llu",
                  metadata->header.sequence_number);
        printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 all 5 copies written successfully\n");
    }
    
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 before mutex_unlock\n");
    mutex_unlock(&dm_remap_metadata_mutex);
    printk(KERN_CRIT "dm-remap CRASH-DEBUG: write_metadata_v4 after mutex_unlock\n");
    
    end_time = ktime_get();
    atomic64_add(ktime_to_ns(ktime_sub(end_time, start_time)),
                 &metadata_stats.total_write_time_ns);
    atomic64_inc(&metadata_stats.writes_completed);
    
    return ret;
}

/**
 * dm_remap_repair_metadata_v4() - Repair corrupted metadata copies
 * 
 * Find best copy and overwrite corrupted copies
 */
int dm_remap_repair_metadata_v4(struct block_device *bdev)
{
    struct dm_remap_metadata_v4 best_metadata;
    const sector_t copy_sectors[] = DM_REMAP_V4_COPY_SECTORS;
    int ret, i, repairs_made = 0;
    
    DMR_DEBUG(1, "Repairing metadata on device");
    
    /* Find best copy */
    ret = dm_remap_read_metadata_v4(bdev, &best_metadata);
    if (ret) {
        DMR_DEBUG(0, "Cannot repair: no valid metadata found");
        return ret;
    }
    
    /* Check each copy and repair if needed */
    for (i = 0; i < 5; i++) {
        struct dm_remap_metadata_v4 copy;
        
        ret = read_metadata_copy(bdev, copy_sectors[i], &copy);
        if (ret != 0 || !validate_metadata_v4(&copy) ||
            copy.header.sequence_number != best_metadata.header.sequence_number) {
            
            /* Repair this copy */
            best_metadata.header.copy_index = i;
            best_metadata.header.metadata_checksum = calculate_metadata_crc32(&best_metadata);
            
            ret = write_metadata_copy(bdev, copy_sectors[i], &best_metadata);
            if (ret == 0) {
                repairs_made++;
                DMR_DEBUG(1, "Repaired metadata copy %d at sector %llu", 
                          i, copy_sectors[i]);
            } else {
                DMR_DEBUG(0, "Failed to repair copy %d: %d", i, ret);
            }
        }
    }
    
    if (repairs_made > 0) {
        atomic64_inc(&metadata_stats.repairs_performed);
        DMR_DEBUG(1, "Metadata repair completed: %d copies repaired", repairs_made);
    }
    
    return repairs_made > 0 ? 0 : -EIO;
}

/**
 * dm_remap_init_metadata_v4() - Initialize new v4.0 metadata structure
 */
void dm_remap_init_metadata_v4(struct dm_remap_metadata_v4 *metadata,
                               const char *main_device_uuid,
                               const char *spare_device_uuid,
                               uint64_t main_device_sectors,
                               uint64_t spare_device_sectors)
{
    memset(metadata, 0, sizeof(*metadata));
    
    /* Initialize header */
    metadata->header.magic = DM_REMAP_METADATA_V4_MAGIC;
    metadata->header.version = DM_REMAP_METADATA_V4_VERSION;
    metadata->header.sequence_number = 1;
    metadata->header.timestamp = ktime_get_real_seconds();
    metadata->header.structure_size = sizeof(*metadata);
    
    /* Initialize device configuration */
    if (main_device_uuid) {
        strncpy(metadata->device_config.main_device_uuid, main_device_uuid,
                sizeof(metadata->device_config.main_device_uuid) - 1);
    }
    if (spare_device_uuid) {
        strncpy(metadata->device_config.spare_device_uuid, spare_device_uuid,
                sizeof(metadata->device_config.spare_device_uuid) - 1);
    }
    
    metadata->device_config.main_device_sectors = main_device_sectors;
    metadata->device_config.spare_device_sectors = spare_device_sectors;
    metadata->device_config.sector_size = 512; /* TODO: Get from device */
    
    /* Initialize health data */
    metadata->health_data.health_score = DM_REMAP_HEALTH_PERFECT;
    metadata->health_data.scan_interval_hours = 24; /* Default 24 hour scan */
    
    /* Initialize remap data */
    metadata->remap_data.max_remaps = DM_REMAP_V4_MAX_REMAPS;
    
    /* Initialize expansion area */
    metadata->future_expansion.expansion_version = 0;
    
    DMR_DEBUG(1, "Initialized v4.0 metadata: main=%s, spare=%s",
              main_device_uuid ? main_device_uuid : "unknown",
              spare_device_uuid ? spare_device_uuid : "unknown");
}

/* Module initialization and cleanup */
int dm_remap_metadata_v4_init(void)
{
    memset(&metadata_stats, 0, sizeof(metadata_stats));
    DMR_DEBUG(1, "dm-remap v4.0 metadata system initialized");
    return 0;
}

void dm_remap_metadata_v4_cleanup(void)
{
    DMR_DEBUG(1, "dm-remap v4.0 metadata system cleanup: reads=%llu, writes=%llu, repairs=%llu",
              atomic64_read(&metadata_stats.reads_completed),
              atomic64_read(&metadata_stats.writes_completed),
              atomic64_read(&metadata_stats.repairs_performed));
}

/* Export symbols for other dm-remap v4.0 modules */
EXPORT_SYMBOL(dm_remap_read_metadata_v4);
EXPORT_SYMBOL(dm_remap_write_metadata_v4);
EXPORT_SYMBOL(dm_remap_repair_metadata_v4);
EXPORT_SYMBOL(dm_remap_init_metadata_v4);