/*
 * dm-remap v4.0 Automatic Setup Reassembly System - Core Implementation
 * 
 * Core functions for device fingerprinting, metadata creation, and storage
 * management for automatic setup discovery and reconstruction.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#define DM_MSG_PREFIX "dm-remap-v4-setup"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/crc32.h>
#include <linux/uuid.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/device-mapper.h>
#include "../include/dm-remap-v4-setup-reassembly.h"

/* Global version counter for conflict resolution */
static atomic64_t global_version_counter = ATOMIC64_INIT(1);

/*
 * Calculate CRC32 checksum for metadata
 */
uint32_t dm_remap_v4_calculate_metadata_crc32(const struct dm_remap_v4_setup_metadata *metadata)
{
    if (!metadata) {
        return 0;
    }
    
    /* Calculate CRC32 of everything except the overall_crc32 field */
    return crc32(0, (unsigned char *)metadata, 
                sizeof(*metadata) - sizeof(metadata->overall_crc32));
}

/*
 * Verify metadata integrity using CRC32 checksums
 */
int dm_remap_v4_verify_metadata_integrity(const struct dm_remap_v4_setup_metadata *metadata)
{
    uint32_t calculated_crc;
    
    if (!metadata) {
        return -EINVAL;
    }
    
    /* Check magic number */
    if (metadata->magic != DM_REMAP_V4_REASSEMBLY_MAGIC) {
        DMERR("Invalid metadata magic: 0x%x", metadata->magic);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_CORRUPTED;
    }
    
    /* Verify overall CRC32 */
    calculated_crc = dm_remap_v4_calculate_metadata_crc32(metadata);
    if (calculated_crc != metadata->overall_crc32) {
        DMERR("Metadata CRC mismatch: expected 0x%x, got 0x%x",
              metadata->overall_crc32, calculated_crc);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH;
    }
    
    /* Verify individual section CRCs */
    uint32_t header_crc = crc32(0, (unsigned char *)&metadata->magic,
                               offsetof(struct dm_remap_v4_setup_metadata, main_device) - 
                               offsetof(struct dm_remap_v4_setup_metadata, magic));
    
    if (header_crc != metadata->header_crc32) {
        DMERR("Header CRC mismatch: expected 0x%x, got 0x%x",
              metadata->header_crc32, header_crc);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH;
    }
    
    DMINFO("Metadata integrity verification passed");
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}
EXPORT_SYMBOL(dm_remap_v4_verify_metadata_integrity);

/*
 * Create device fingerprint for identification
 */
int dm_remap_v4_create_device_fingerprint(
    struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path)
{
    struct file *file;
    struct inode *inode;
    struct block_device *bdev;
    uint64_t current_time;
    
    if (!fingerprint || !device_path) {
        return -EINVAL;
    }
    
    memset(fingerprint, 0, sizeof(*fingerprint));
    current_time = ktime_get_real_seconds();
    
    /* Initialize fingerprint */
    fingerprint->magic = DM_REMAP_V4_DEVICE_FINGERPRINT_MAGIC;
    fingerprint->creation_timestamp = current_time;
    fingerprint->last_seen_timestamp = current_time;
    strncpy(fingerprint->device_path, device_path, 
            sizeof(fingerprint->device_path) - 1);
    
    /* Try to open the device to get information */
    file = filp_open(device_path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        DMWARN("Cannot open device %s for fingerprinting", device_path);
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
    
    /* Extract device information */
    fingerprint->device_size = bdev_nr_sectors(bdev);
    fingerprint->device_capacity = fingerprint->device_size * bdev_logical_block_size(bdev);
    fingerprint->sector_size = bdev_logical_block_size(bdev);
    
    /* Generate UUID based on device characteristics */
    /* In a real implementation, this would use proper device UUID or generate
     * a stable UUID based on device serial number, etc. */
    uuid_gen(&fingerprint->device_uuid);
    
    /* Try to get device serial and model (simplified for this implementation) */
    snprintf(fingerprint->device_serial, sizeof(fingerprint->device_serial),
             "SER-%llx", (unsigned long long)fingerprint->device_size);
    snprintf(fingerprint->device_model, sizeof(fingerprint->device_model),
             "Generic Block Device");
    
    fingerprint->device_type = 0x01; /* Generic block device */
    
    filp_close(file, NULL);
    
    /* Calculate fingerprint CRC */
    fingerprint->fingerprint_crc32 = crc32(0, (unsigned char *)fingerprint,
                                          sizeof(*fingerprint) - sizeof(fingerprint->fingerprint_crc32));
    
    DMINFO("Created device fingerprint for %s: size=%llu sectors, capacity=%llu bytes",
           device_path, fingerprint->device_size, fingerprint->device_capacity);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Verify device fingerprint against actual device
 */
int dm_remap_v4_verify_device_fingerprint(
    const struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path)
{
    struct dm_remap_v4_device_fingerprint current_fingerprint;
    int result;
    
    if (!fingerprint || !device_path) {
        return -EINVAL;
    }
    
    /* Verify fingerprint CRC first */
    uint32_t calculated_crc = crc32(0, (unsigned char *)fingerprint,
                                   sizeof(*fingerprint) - sizeof(fingerprint->fingerprint_crc32));
    if (calculated_crc != fingerprint->fingerprint_crc32) {
        DMERR("Device fingerprint CRC mismatch");
        return -DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH;
    }
    
    /* Create current fingerprint for comparison */
    result = dm_remap_v4_create_device_fingerprint(&current_fingerprint, device_path);
    if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        return result;
    }
    
    /* Compare critical characteristics */
    if (fingerprint->device_size != current_fingerprint.device_size) {
        DMERR("Device size mismatch: expected %llu, got %llu",
              fingerprint->device_size, current_fingerprint.device_size);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISMATCH;
    }
    
    if (fingerprint->sector_size != current_fingerprint.sector_size) {
        DMERR("Sector size mismatch: expected %u, got %u",
              fingerprint->sector_size, current_fingerprint.sector_size);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISMATCH;
    }
    
    if (strcmp(fingerprint->device_path, device_path) != 0) {
        DMWARN("Device path changed: was %s, now %s", 
               fingerprint->device_path, device_path);
        /* This is a warning, not an error - devices can move */
    }
    
    DMINFO("Device fingerprint verification passed for %s", device_path);
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Compare two device fingerprints
 */
int dm_remap_v4_compare_device_fingerprints(
    const struct dm_remap_v4_device_fingerprint *fp1,
    const struct dm_remap_v4_device_fingerprint *fp2)
{
    if (!fp1 || !fp2) {
        return -EINVAL;
    }
    
    /* Compare UUIDs first */
    if (uuid_equal(&fp1->device_uuid, &fp2->device_uuid)) {
        return 0; /* Identical devices */
    }
    
    /* Compare size and characteristics */
    if (fp1->device_size == fp2->device_size &&
        fp1->sector_size == fp2->sector_size &&
        strcmp(fp1->device_serial, fp2->device_serial) == 0) {
        return 0; /* Same device, possibly different UUID generation */
    }
    
    return 1; /* Different devices */
}

/*
 * Update metadata version with conflict resolution
 */
int dm_remap_v4_update_metadata_version(struct dm_remap_v4_setup_metadata *metadata)
{
    if (!metadata) {
        return -EINVAL;
    }
    
    /* Increment global version counter */
    metadata->version_counter = atomic64_inc_return(&global_version_counter);
    metadata->modified_timestamp = ktime_get_real_seconds();
    
    /* Recalculate CRCs */
    metadata->header_crc32 = crc32(0, (unsigned char *)&metadata->magic,
                                  offsetof(struct dm_remap_v4_setup_metadata, main_device) - 
                                  offsetof(struct dm_remap_v4_setup_metadata, magic));
    
    metadata->overall_crc32 = dm_remap_v4_calculate_metadata_crc32(metadata);
    
    DMINFO("Updated metadata version to %llu", metadata->version_counter);
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}
EXPORT_SYMBOL(dm_remap_v4_update_metadata_version);

/*
 * Create complete setup metadata
 */
int dm_remap_v4_create_setup_metadata(
    struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_device_fingerprint *main_device,
    const struct dm_remap_v4_target_config *target_config)
{
    uint64_t current_time;
    
    if (!metadata || !main_device || !target_config) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    
    /* Initialize metadata */
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = DM_REMAP_V4_REASSEMBLY_MAGIC;
    metadata->metadata_version = 1;
    metadata->version_counter = atomic64_inc_return(&global_version_counter);
    metadata->created_timestamp = current_time;
    metadata->modified_timestamp = current_time;
    
    /* Set up description */
    snprintf(metadata->setup_description, sizeof(metadata->setup_description),
             "dm-remap v4.0 setup for %s", main_device->device_path);
    
    /* Copy device information */
    metadata->main_device = *main_device;
    metadata->num_spare_devices = 0; /* Will be filled when spares are added */
    
    /* Copy target configuration */
    metadata->target_config = *target_config;
    
    /* Initialize metadata copy information */
    metadata->metadata_copies_count = DM_REMAP_V4_METADATA_COPY_SECTORS;
    metadata->metadata_copy_locations[0] = DM_REMAP_V4_METADATA_SECTOR_0;
    metadata->metadata_copy_locations[1] = DM_REMAP_V4_METADATA_SECTOR_1;
    metadata->metadata_copy_locations[2] = DM_REMAP_V4_METADATA_SECTOR_2;
    metadata->metadata_copy_locations[3] = DM_REMAP_V4_METADATA_SECTOR_3;
    metadata->metadata_copy_locations[4] = DM_REMAP_V4_METADATA_SECTOR_4;
    
    /* Initialize empty configurations */
    metadata->sysfs_config.num_settings = 0;
    metadata->sysfs_config.config_timestamp = current_time;
    metadata->policy_config.num_rules = 0;
    metadata->policy_config.policy_timestamp = current_time;
    
    /* Calculate checksums */
    metadata->header_crc32 = crc32(0, (unsigned char *)&metadata->magic,
                                  offsetof(struct dm_remap_v4_setup_metadata, main_device) - 
                                  offsetof(struct dm_remap_v4_setup_metadata, magic));
    
    metadata->devices_crc32 = crc32(0, (unsigned char *)&metadata->main_device,
                                   sizeof(metadata->main_device) + 
                                   sizeof(metadata->num_spare_devices) +
                                   sizeof(metadata->spare_devices));
    
    metadata->config_crc32 = crc32(0, (unsigned char *)&metadata->target_config,
                                  sizeof(metadata->target_config) +
                                  sizeof(metadata->sysfs_config) +
                                  sizeof(metadata->policy_config));
    
    metadata->overall_crc32 = dm_remap_v4_calculate_metadata_crc32(metadata);
    
    DMINFO("Created setup metadata: version=%llu, main_device=%s",
           metadata->version_counter, main_device->device_path);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Add spare device to setup metadata
 */
int dm_remap_v4_add_spare_device_to_metadata(
    struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_device_fingerprint *spare_device,
    uint32_t priority)
{
    struct dm_remap_v4_spare_relationship *spare_rel;
    uint64_t current_time;
    
    if (!metadata || !spare_device) {
        return -EINVAL;
    }
    
    if (metadata->num_spare_devices >= DM_REMAP_V4_MAX_SPARE_DEVICES) {
        DMERR("Maximum number of spare devices reached: %u", DM_REMAP_V4_MAX_SPARE_DEVICES);
        return -ENOSPC;
    }
    
    current_time = ktime_get_real_seconds();
    spare_rel = &metadata->spare_devices[metadata->num_spare_devices];
    
    /* Initialize spare relationship */
    memset(spare_rel, 0, sizeof(*spare_rel));
    spare_rel->spare_fingerprint = *spare_device;
    spare_rel->spare_priority = priority;
    spare_rel->spare_status = 0x01; /* Active */
    spare_rel->assigned_timestamp = current_time;
    spare_rel->capacity_available = spare_device->device_size;
    spare_rel->metadata_copies_stored = DM_REMAP_V4_METADATA_COPY_SECTORS;
    
    /* Calculate spare relationship CRC */
    spare_rel->spare_crc32 = crc32(0, (unsigned char *)spare_rel,
                                  sizeof(*spare_rel) - sizeof(spare_rel->spare_crc32));
    
    metadata->num_spare_devices++;
    
    /* Update metadata version and checksums */
    dm_remap_v4_update_metadata_version(metadata);
    
    DMINFO("Added spare device to metadata: %s (priority %u)",
           spare_device->device_path, priority);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Calculate confidence score for discovery result
 * Returns integer percentage (0-100) instead of float
 */
uint32_t dm_remap_v4_calculate_confidence_score(const struct dm_remap_v4_discovery_result *result)
{
    int32_t confidence = 0;  /* Use signed int for intermediate calculations */
    
    if (!result) {
        return 0;
    }
    
    /* Base confidence from valid copies (0-30 points) */
    if (result->copies_valid > 0 && result->copies_found > 0) {
        confidence += (30 * result->copies_valid) / result->copies_found;
    }
    
    /* Bonus for multiple valid copies (20 points) */
    if (result->copies_valid >= 3) {
        confidence += 20;
    }
    
    /* Penalty for corruption (up to -10 points) */
    if (result->corruption_level > 0) {
        int32_t penalty = (10 * result->corruption_level) / 10;
        if (penalty > 10) penalty = 10;
        confidence -= penalty;
    }
    
    /* Bonus for recent metadata (10 points) */
    uint64_t current_time = ktime_get_real_seconds();
    uint64_t age_hours = (current_time - result->metadata.modified_timestamp) / 3600;
    if (age_hours < 24) {
        confidence += 10;
    } else if (age_hours > 168) { /* > 1 week (-10 points) */
        confidence -= 10;
    }
    
    /* Bonus for complete metadata (30 points) */
    if (result->metadata.magic == DM_REMAP_V4_REASSEMBLY_MAGIC &&
        result->metadata.num_spare_devices > 0) {
        confidence += 30;
    }
    
    /* Clamp to valid range [0, 100] */
    if (confidence < 0) confidence = 0;
    if (confidence > 100) confidence = 100;
    
    return (uint32_t)confidence;
}
EXPORT_SYMBOL(dm_remap_v4_calculate_confidence_score);

/*
 * Convert error code to human-readable string
 */
const char* dm_remap_v4_reassembly_error_to_string(int error_code)
{
    switch (error_code) {
    case DM_REMAP_V4_REASSEMBLY_SUCCESS:
        return "Success";
    case DM_REMAP_V4_REASSEMBLY_ERROR_INVALID_PARAMS:
        return "Invalid parameters";
    case DM_REMAP_V4_REASSEMBLY_ERROR_NO_METADATA:
        return "No metadata found";
    case DM_REMAP_V4_REASSEMBLY_ERROR_CORRUPTED:
        return "Metadata corrupted";
    case DM_REMAP_V4_REASSEMBLY_ERROR_VERSION_CONFLICT:
        return "Version conflict detected";
    case DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISSING:
        return "Device missing or unavailable";
    case DM_REMAP_V4_REASSEMBLY_ERROR_SETUP_CONFLICT:
        return "Setup conflict detected";
    case DM_REMAP_V4_REASSEMBLY_ERROR_INSUFFICIENT_COPIES:
        return "Insufficient valid metadata copies";
    case DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH:
        return "CRC checksum mismatch";
    case DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISMATCH:
        return "Device characteristics mismatch";
    case DM_REMAP_V4_REASSEMBLY_ERROR_PERMISSION_DENIED:
        return "Permission denied";
    default:
        return "Unknown error";
    }
}

/*
 * Print setup metadata for debugging
 */
void dm_remap_v4_print_setup_metadata(const struct dm_remap_v4_setup_metadata *metadata)
{
    if (!metadata) {
        DMINFO("Setup metadata: NULL");
        return;
    }
    
    DMINFO("=== Setup Metadata ===");
    DMINFO("Magic: 0x%x", metadata->magic);
    DMINFO("Version: %u (counter: %llu)", metadata->metadata_version, metadata->version_counter);
    DMINFO("Description: %s", metadata->setup_description);
    DMINFO("Created: %llu, Modified: %llu", 
           metadata->created_timestamp, metadata->modified_timestamp);
    DMINFO("Main device: %s (%llu sectors)", 
           metadata->main_device.device_path, metadata->main_device.device_size);
    DMINFO("Spare devices: %u", metadata->num_spare_devices);
    DMINFO("Target params: %s", metadata->target_config.target_params);
    DMINFO("Metadata copies: %u", metadata->metadata_copies_count);
    DMINFO("Overall CRC32: 0x%x", metadata->overall_crc32);
}

MODULE_DESCRIPTION("dm-remap v4.0 Setup Reassembly Core Functions");
MODULE_AUTHOR("dm-remap development team");
MODULE_LICENSE("GPL");