/*
 * dm-remap v4.0 Automatic Setup Reassembly System - Storage I/O Functions
 * 
 * Functions for reading and writing setup metadata to/from storage devices
 * with redundant copies and integrity verification.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#define DM_MSG_PREFIX "dm-remap-v4-setup"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/device-mapper.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include "../include/dm-remap-v4-setup-reassembly.h"

/* Bio completion callback */
struct dm_remap_v4_bio_completion {
    struct completion completion;
    int result;
};

/*
 * Bio completion callback function
 */
static void dm_remap_v4_bio_completion_callback(struct bio *bio)
{
    struct dm_remap_v4_bio_completion *bc = bio->bi_private;
    
    bc->result = blk_status_to_errno(bio->bi_status);
    complete(&bc->completion);
}

/*
 * Read metadata from specific sector
 */
static int dm_remap_v4_read_metadata_sector(
    struct block_device *bdev,
    sector_t sector,
    struct dm_remap_v4_setup_metadata *metadata)
{
    struct bio *bio;
    struct dm_remap_v4_bio_completion bc;
    struct page *page;
    void *data;
    int result;
    
    if (!bdev || !metadata) {
        return -EINVAL;
    }
    
    /* Allocate page for I/O */
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        DMERR("Failed to allocate page for metadata read");
        return -ENOMEM;
    }
    
    /* Allocate and setup bio */
    bio = bio_alloc(bdev, 1, REQ_OP_READ, GFP_KERNEL);
    if (!bio) {
        DMERR("Failed to allocate bio for metadata read");
        __free_page(page);
        return -ENOMEM;
    }
    
    bio->bi_iter.bi_sector = sector;
    bio_add_page(bio, page, PAGE_SIZE, 0);
    
    /* Setup completion */
    init_completion(&bc.completion);
    bio->bi_private = &bc;
    bio->bi_end_io = dm_remap_v4_bio_completion_callback;
    
    /* Submit I/O */
    submit_bio(bio);
    wait_for_completion(&bc.completion);
    
    result = bc.result;
    if (result == 0) {
        /* Copy data from page to metadata structure */
        data = kmap(page);
        memcpy(metadata, data, sizeof(*metadata));
        kunmap(page);
        
        DMINFO("Successfully read metadata from sector %llu", (unsigned long long)sector);
    } else {
        DMWARN("Failed to read metadata from sector %llu: %d", 
               (unsigned long long)sector, result);
    }
    
    bio_put(bio);
    __free_page(page);
    
    return result;
}

/*
 * Write metadata to specific sector
 */
static int dm_remap_v4_write_metadata_sector(
    struct block_device *bdev,
    sector_t sector,
    const struct dm_remap_v4_setup_metadata *metadata)
{
    struct bio *bio;
    struct dm_remap_v4_bio_completion bc;
    struct page *page;
    void *data;
    int result;
    
    if (!bdev || !metadata) {
        return -EINVAL;
    }
    
    /* Allocate page for I/O */
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        DMERR("Failed to allocate page for metadata write");
        return -ENOMEM;
    }
    
    /* Copy metadata to page */
    data = kmap(page);
    memcpy(data, metadata, sizeof(*metadata));
    /* Zero out remaining page data */
    memset(data + sizeof(*metadata), 0, PAGE_SIZE - sizeof(*metadata));
    kunmap(page);
    
    /* Allocate and setup bio */
    bio = bio_alloc(bdev, 1, REQ_OP_WRITE | REQ_FUA, GFP_KERNEL);
    if (!bio) {
        DMERR("Failed to allocate bio for metadata write");
        __free_page(page);
        return -ENOMEM;
    }
    
    bio->bi_iter.bi_sector = sector;
    bio_add_page(bio, page, PAGE_SIZE, 0);
    
    /* Setup completion */
    init_completion(&bc.completion);
    bio->bi_private = &bc;
    bio->bi_end_io = dm_remap_v4_bio_completion_callback;
    
    /* Submit I/O */
    submit_bio(bio);
    wait_for_completion(&bc.completion);
    
    result = bc.result;
    if (result == 0) {
        DMINFO("Successfully wrote metadata to sector %llu", (unsigned long long)sector);
    } else {
        DMERR("Failed to write metadata to sector %llu: %d", 
              (unsigned long long)sector, result);
    }
    
    bio_put(bio);
    __free_page(page);
    
    return result;
}

/*
 * Write metadata with redundant copies
 */
int dm_remap_v4_write_metadata_redundant(
    const char *device_path,
    const struct dm_remap_v4_setup_metadata *metadata)
{
    struct file *file;
    struct inode *inode;
    struct block_device *bdev;
    int result = 0;
    int successful_writes = 0;
    int i;
    
    if (!device_path || !metadata) {
        return -EINVAL;
    }
    
    /* Verify metadata integrity before writing */
    result = dm_remap_v4_verify_metadata_integrity(metadata);
    if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        DMERR("Metadata integrity check failed before write: %d", result);
        return result;
    }
    
    /* Open device for writing */
    file = filp_open(device_path, O_RDWR, 0);
    if (IS_ERR(file)) {
        DMERR("Cannot open device %s for writing metadata", device_path);
        return PTR_ERR(file);
    }
    
    inode = file_inode(file);
    if (!S_ISBLK(inode->i_mode)) {
        DMERR("Device %s is not a block device", device_path);
        filp_close(file, NULL);
        return -EINVAL;
    }
    
    bdev = I_BDEV(inode);
    if (!bdev) {
        DMERR("Cannot get block device for %s", device_path);
        filp_close(file, NULL);
        return -EINVAL;
    }
    
    /* Write to all configured locations */
    for (i = 0; i < metadata->metadata_copies_count && i < DM_REMAP_V4_METADATA_COPY_SECTORS; i++) {
        sector_t sector = metadata->metadata_copy_locations[i];
        
        result = dm_remap_v4_write_metadata_sector(bdev, sector, metadata);
        if (result == 0) {
            successful_writes++;
            DMINFO("Metadata copy %d written successfully to sector %llu", 
                   i + 1, (unsigned long long)sector);
        } else {
            DMWARN("Failed to write metadata copy %d to sector %llu: %d",
                   i + 1, (unsigned long long)sector, result);
        }
    }
    
    filp_close(file, NULL);
    
    /* Consider success if at least one write succeeded */
    if (successful_writes == 0) {
        DMERR("Failed to write any metadata copies to %s", device_path);
        return -EIO;
    }
    
    if (successful_writes < metadata->metadata_copies_count) {
        DMWARN("Only %d of %d metadata copies written successfully to %s",
               successful_writes, metadata->metadata_copies_count, device_path);
    }
    
    DMINFO("Successfully wrote %d metadata copies to %s", successful_writes, device_path);
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Read metadata with validation from multiple copies
 */
int dm_remap_v4_read_metadata_validated(
    const char *device_path,
    struct dm_remap_v4_setup_metadata *metadata,
    struct dm_remap_v4_metadata_read_result *read_result)
{
    struct file *file;
    struct inode *inode;
    struct block_device *bdev;
    struct dm_remap_v4_setup_metadata temp_metadata;
    struct dm_remap_v4_setup_metadata best_metadata;
    sector_t copy_locations[] = {
        DM_REMAP_V4_METADATA_SECTOR_0,
        DM_REMAP_V4_METADATA_SECTOR_1,
        DM_REMAP_V4_METADATA_SECTOR_2,
        DM_REMAP_V4_METADATA_SECTOR_3,
        DM_REMAP_V4_METADATA_SECTOR_4
    };
    int copies_found = 0;
    int copies_valid = 0;
    uint64_t best_version = 0;
    bool found_valid = false;
    int result;
    int i;
    
    if (!device_path || !metadata) {
        return -EINVAL;
    }
    
    /* Initialize read result if provided */
    if (read_result) {
        memset(read_result, 0, sizeof(*read_result));
        strncpy(read_result->device_path, device_path, sizeof(read_result->device_path) - 1);
    }
    
    /* Open device for reading */
    file = filp_open(device_path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        DMWARN("Cannot open device %s for reading metadata", device_path);
        return PTR_ERR(file);
    }
    
    inode = file_inode(file);
    if (!S_ISBLK(inode->i_mode)) {
        DMERR("Device %s is not a block device", device_path);
        filp_close(file, NULL);
        return -EINVAL;
    }
    
    bdev = I_BDEV(inode);
    if (!bdev) {
        DMERR("Cannot get block device for %s", device_path);
        filp_close(file, NULL);
        return -EINVAL;
    }
    
    /* Try to read from all possible locations */
    for (i = 0; i < ARRAY_SIZE(copy_locations); i++) {
        result = dm_remap_v4_read_metadata_sector(bdev, copy_locations[i], &temp_metadata);
        
        if (result == 0) {
            copies_found++;
            
            /* Verify metadata integrity */
            result = dm_remap_v4_verify_metadata_integrity(&temp_metadata);
            if (result == DM_REMAP_V4_REASSEMBLY_SUCCESS) {
                copies_valid++;
                
                /* Check if this is the newest version */
                if (!found_valid || temp_metadata.version_counter > best_version) {
                    best_metadata = temp_metadata;
                    best_version = temp_metadata.version_counter;
                    found_valid = true;
                }
                
                DMINFO("Found valid metadata at sector %llu (version %llu)",
                       (unsigned long long)copy_locations[i], temp_metadata.version_counter);
            } else {
                DMWARN("Metadata at sector %llu failed integrity check: %d",
                       (unsigned long long)copy_locations[i], result);
                if (read_result) {
                    read_result->corruption_level++;
                }
            }
        } else {
            DMINFO("No metadata found at sector %llu", (unsigned long long)copy_locations[i]);
        }
    }
    
    filp_close(file, NULL);
    
    /* Fill in read result */
    if (read_result) {
        read_result->copies_found = copies_found;
        read_result->copies_valid = copies_valid;
        read_result->read_timestamp = ktime_get_real_seconds();
        if (found_valid) {
            read_result->confidence_score = dm_remap_v4_calculate_confidence_score(
                &(struct dm_remap_v4_discovery_result) {
                    .copies_found = copies_found,
                    .copies_valid = copies_valid,
                    .corruption_level = read_result->corruption_level,
                    .metadata = best_metadata
                });
        }
    }
    
    /* Return results */
    if (!found_valid) {
        if (copies_found == 0) {
            DMINFO("No metadata found on device %s", device_path);
            return -DM_REMAP_V4_REASSEMBLY_ERROR_NO_METADATA;
        } else {
            DMERR("Found %d metadata copies but none are valid on device %s", 
                  copies_found, device_path);
            return -DM_REMAP_V4_REASSEMBLY_ERROR_CORRUPTED;
        }
    }
    
    if (copies_valid < 2) {
        DMWARN("Only %d valid metadata copies found on device %s (recommended: 3+)",
               copies_valid, device_path);
    }
    
    *metadata = best_metadata;
    
    DMINFO("Successfully read metadata from %s: version %llu, %d of %d copies valid",
           device_path, best_version, copies_valid, copies_found);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Store metadata on all devices in a setup
 */
int dm_remap_v4_store_metadata_on_setup(
    const struct dm_remap_v4_setup_metadata *metadata)
{
    int result;
    int successful_stores = 0;
    int total_devices = 0;
    int i;
    
    if (!metadata) {
        return -EINVAL;
    }
    
    /* Store on main device */
    total_devices++;
    result = dm_remap_v4_write_metadata_redundant(metadata->main_device.device_path, metadata);
    if (result == DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        successful_stores++;
        DMINFO("Metadata stored successfully on main device: %s", 
               metadata->main_device.device_path);
    } else {
        DMERR("Failed to store metadata on main device %s: %d",
              metadata->main_device.device_path, result);
    }
    
    /* Store on all spare devices */
    for (i = 0; i < metadata->num_spare_devices; i++) {
        const struct dm_remap_v4_spare_relationship *spare = &metadata->spare_devices[i];
        
        total_devices++;
        result = dm_remap_v4_write_metadata_redundant(
            spare->spare_fingerprint.device_path, metadata);
        
        if (result == DM_REMAP_V4_REASSEMBLY_SUCCESS) {
            successful_stores++;
            DMINFO("Metadata stored successfully on spare device %d: %s", 
                   i + 1, spare->spare_fingerprint.device_path);
        } else {
            DMERR("Failed to store metadata on spare device %d (%s): %d",
                  i + 1, spare->spare_fingerprint.device_path, result);
        }
    }
    
    /* Evaluate success */
    if (successful_stores == 0) {
        DMERR("Failed to store metadata on any device in the setup");
        return -EIO;
    }
    
    if (successful_stores < total_devices) {
        DMWARN("Metadata stored on only %d of %d devices in setup",
               successful_stores, total_devices);
    } else {
        DMINFO("Metadata stored successfully on all %d devices in setup", total_devices);
    }
    
    return successful_stores == total_devices ? DM_REMAP_V4_REASSEMBLY_SUCCESS : -ECOMM;
}

/*
 * Update metadata on existing setup
 */
int dm_remap_v4_update_metadata_on_setup(
    struct dm_remap_v4_setup_metadata *metadata)
{
    if (!metadata) {
        return -EINVAL;
    }
    
    /* Update version and checksums */
    int result = dm_remap_v4_update_metadata_version(metadata);
    if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        return result;
    }
    
    /* Store updated metadata on all devices */
    return dm_remap_v4_store_metadata_on_setup(metadata);
}

/*
 * Repair corrupted metadata using valid copies
 */
int dm_remap_v4_repair_metadata_corruption(
    const char *device_path,
    const struct dm_remap_v4_setup_metadata *reference_metadata)
{
    struct dm_remap_v4_metadata_read_result read_result;
    struct dm_remap_v4_setup_metadata current_metadata;
    int result;
    
    if (!device_path || !reference_metadata) {
        return -EINVAL;
    }
    
    DMINFO("Attempting to repair metadata corruption on device %s", device_path);
    
    /* Read current state */
    result = dm_remap_v4_read_metadata_validated(device_path, &current_metadata, &read_result);
    
    if (result == DM_REMAP_V4_REASSEMBLY_SUCCESS && 
        read_result.copies_valid >= DM_REMAP_V4_MIN_VALID_COPIES) {
        DMINFO("Device %s already has sufficient valid metadata copies (%d)",
               device_path, read_result.copies_valid);
        return DM_REMAP_V4_REASSEMBLY_SUCCESS;
    }
    
    /* Write reference metadata to repair corruption */
    result = dm_remap_v4_write_metadata_redundant(device_path, reference_metadata);
    if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        DMERR("Failed to write repair metadata to device %s: %d", device_path, result);
        return result;
    }
    
    /* Verify repair was successful */
    result = dm_remap_v4_read_metadata_validated(device_path, &current_metadata, &read_result);
    if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        DMERR("Metadata repair verification failed for device %s: %d", device_path, result);
        return result;
    }
    
    if (read_result.copies_valid < DM_REMAP_V4_MIN_VALID_COPIES) {
        DMERR("After repair, device %s still has insufficient valid copies: %d",
              device_path, read_result.copies_valid);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_INSUFFICIENT_COPIES;
    }
    
    DMINFO("Successfully repaired metadata corruption on device %s: %d valid copies",
           device_path, read_result.copies_valid);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Clean/remove metadata from device
 */
int dm_remap_v4_clean_metadata_from_device(const char *device_path)
{
    struct file *file;
    struct inode *inode;
    struct block_device *bdev;
    struct dm_remap_v4_setup_metadata zero_metadata;
    sector_t copy_locations[] = {
        DM_REMAP_V4_METADATA_SECTOR_0,
        DM_REMAP_V4_METADATA_SECTOR_1,
        DM_REMAP_V4_METADATA_SECTOR_2,
        DM_REMAP_V4_METADATA_SECTOR_3,
        DM_REMAP_V4_METADATA_SECTOR_4
    };
    int successful_clears = 0;
    int result;
    int i;
    
    if (!device_path) {
        return -EINVAL;
    }
    
    DMINFO("Cleaning metadata from device %s", device_path);
    
    /* Open device for writing */
    file = filp_open(device_path, O_RDWR, 0);
    if (IS_ERR(file)) {
        DMERR("Cannot open device %s for cleaning metadata", device_path);
        return PTR_ERR(file);
    }
    
    inode = file_inode(file);
    if (!S_ISBLK(inode->i_mode)) {
        DMERR("Device %s is not a block device", device_path);
        filp_close(file, NULL);
        return -EINVAL;
    }
    
    bdev = I_BDEV(inode);
    if (!bdev) {
        DMERR("Cannot get block device for %s", device_path);
        filp_close(file, NULL);
        return -EINVAL;
    }
    
    /* Create zeroed metadata structure */
    memset(&zero_metadata, 0, sizeof(zero_metadata));
    
    /* Clear all metadata locations */
    for (i = 0; i < ARRAY_SIZE(copy_locations); i++) {
        result = dm_remap_v4_write_metadata_sector(bdev, copy_locations[i], &zero_metadata);
        if (result == 0) {
            successful_clears++;
            DMINFO("Cleared metadata at sector %llu", (unsigned long long)copy_locations[i]);
        } else {
            DMWARN("Failed to clear metadata at sector %llu: %d",
                   (unsigned long long)copy_locations[i], result);
        }
    }
    
    filp_close(file, NULL);
    
    if (successful_clears == 0) {
        DMERR("Failed to clear any metadata locations on device %s", device_path);
        return -EIO;
    }
    
    DMINFO("Successfully cleaned %d metadata locations from device %s",
           successful_clears, device_path);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}