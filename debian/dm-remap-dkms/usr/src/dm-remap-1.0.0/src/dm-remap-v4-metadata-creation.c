/* dm-remap v4.0 Metadata Creation Functions
 * 
 * This file implements the core metadata creation and initialization functions
 * for the v4.0 enhanced metadata format with Automatic Setup Reassembly.
 *
 * Key Features:
 * - Device fingerprinting for robust identification
 * - Complete configuration storage for reassembly
 * - Multi-layer CRC32 integrity protection
 * - Fixed metadata placement at 5 sector locations
 * - Version control and conflict resolution
 *
 * Author: dm-remap development team
 * Version: 4.0
 * Date: October 2025
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/crc32.h>
#include <linux/uuid.h>
#include <linux/time.h>
#include <linux/string.h>

#include "dm-remap-v4-metadata.h"
#include "../src/dm-remap-core.h"

/* ========================================================================
 * DEVICE FINGERPRINTING FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_create_device_fingerprint - Create device identification fingerprint
 * @fp: Device fingerprint structure to populate
 * @dev: Device to fingerprint
 * 
 * Creates a comprehensive device fingerprint using multiple identification
 * methods to ensure reliable device matching during recovery operations.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_create_device_fingerprint(struct dm_remap_device_fingerprint *fp,
                                      struct dm_dev *dev)
{
    struct block_device *bdev;
    sector_t device_size;
    
    if (!fp || !dev) {
        return -EINVAL;
    }
    
    memset(fp, 0, sizeof(*fp));
    bdev = dev->bdev;
    
    /* Generate device UUID - use device name hash as substitute */
    {
        u32 name_hash = crc32(0, dev->name, strlen(dev->name));
        u64 size_factor = bdev_nr_sectors(bdev) >> 11; /* Size approximation */
        u64 time_factor = ktime_get_real_seconds();
        
        /* Create pseudo-UUID from device characteristics */
        *((u32*)&fp->uuid[0]) = name_hash;
        *((u32*)&fp->uuid[4]) = (u32)(size_factor ^ time_factor);
        *((u32*)&fp->uuid[8]) = (u32)(size_factor >> 32);
        *((u32*)&fp->uuid[12]) = crc32(name_hash, &size_factor, sizeof(size_factor));
    }
    
    /* Store device path */
    strncpy(fp->device_path, dev->name, DM_REMAP_MAX_PATH_LEN - 1);
    fp->device_path[DM_REMAP_MAX_PATH_LEN - 1] = '\0';
    
    /* Get device size information */
    device_size = bdev_nr_sectors(bdev);
    fp->device_size_sectors = device_size;
    fp->sector_size = bdev_logical_block_size(bdev);
    
    /* Create device serial hash (use device major:minor as substitute) */
    {
        u32 dev_id = new_encode_dev(bdev->bd_dev);
        fp->device_serial_hash = crc32(0, &dev_id, sizeof(dev_id));
        fp->device_serial_hash ^= (u64)device_size << 16;
    }
    
    /* Filesystem UUID hash - attempt to read superblock for UUID */
    fp->filesystem_uuid_hash = 0; /* Default to 0 if no filesystem detected */
    /* TODO: Could add filesystem UUID detection in future enhancement */
    
    /* Calculate CRC32 of the fingerprint structure */
    fp->device_fingerprint_crc = dm_remap_calculate_device_fingerprint_crc(fp);
    
    printk(KERN_INFO "dm-remap: Created device fingerprint for %s (%llu sectors)\n",
           fp->device_path, fp->device_size_sectors);
    
    return 0;
}

/**
 * dm_remap_match_device_fingerprint - Match device against fingerprint
 * @fp: Device fingerprint to match against
 * @dev: Device to check
 * 
 * Performs multi-level device matching with fallback strategies to handle
 * cases where device paths change but physical device remains the same.
 * 
 * Returns: Match confidence level (0-100), 0 = no match, 100 = perfect match
 */
int dm_remap_match_device_fingerprint(const struct dm_remap_device_fingerprint *fp,
                                     struct dm_dev *dev)
{
    struct dm_remap_device_fingerprint current_fp;
    int confidence = 0;
    int ret;
    
    if (!fp || !dev) {
        return 0;
    }
    
    /* Create fingerprint for current device */
    ret = dm_remap_create_device_fingerprint(&current_fp, dev);
    if (ret < 0) {
        return 0;
    }
    
    /* UUID exact match - highest confidence */
    if (memcmp(fp->uuid, current_fp.uuid, DM_REMAP_UUID_SIZE) == 0) {
        confidence = 100;
        printk(KERN_INFO "dm-remap: Perfect UUID match for device %s\n", dev->name);
        return confidence;
    }
    
    /* Device path + size match - high confidence */
    if (strcmp(fp->device_path, current_fp.device_path) == 0 &&
        fp->device_size_sectors == current_fp.device_size_sectors) {
        confidence = 90;
        printk(KERN_INFO "dm-remap: Path+size match for device %s\n", dev->name);
        return confidence;
    }
    
    /* Serial hash + size match - medium confidence */
    if (fp->device_serial_hash == current_fp.device_serial_hash &&
        fp->device_size_sectors == current_fp.device_size_sectors) {
        confidence = 75;
        printk(KERN_INFO "dm-remap: Serial+size match for device %s\n", dev->name);
        return confidence;
    }
    
    /* Size + sector size match - low confidence (last resort) */
    if (fp->device_size_sectors == current_fp.device_size_sectors &&
        fp->sector_size == current_fp.sector_size) {
        confidence = 50;
        printk(KERN_WARNING "dm-remap: Size-only match for device %s (low confidence)\n", 
               dev->name);
        return confidence;
    }
    
    printk(KERN_INFO "dm-remap: No match found for device %s\n", dev->name);
    return 0;
}

/* ========================================================================
 * TARGET CONFIGURATION FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_create_target_configuration - Create target configuration data
 * @config: Target configuration structure to populate
 * @target_params: Original target parameters string
 * @target_size: Target size in sectors
 * @sysfs_settings: Current sysfs parameter values
 * @settings_count: Number of sysfs settings
 * 
 * Stores complete target configuration information needed for reassembly.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_create_target_configuration(struct dm_remap_target_configuration *config,
                                        const char *target_params,
                                        u64 target_size,
                                        const u32 *sysfs_settings,
                                        u32 settings_count)
{
    if (!config || !target_params) {
        return -EINVAL;
    }
    
    memset(config, 0, sizeof(*config));
    
    /* Store target parameters */
    strncpy(config->target_params, target_params, DM_REMAP_MAX_PARAMS_LEN - 1);
    config->target_params[DM_REMAP_MAX_PARAMS_LEN - 1] = '\0';
    
    /* Store target size and flags */
    config->target_size_sectors = target_size;
    config->target_flags = 0; /* Default flags */
    
    /* Store sysfs settings */
    if (sysfs_settings && settings_count > 0) {
        u32 count = min(settings_count, (u32)DM_REMAP_MAX_SYSFS_SETTINGS);
        memcpy(config->sysfs_settings, sysfs_settings, count * sizeof(u32));
        config->sysfs_settings_count = count;
    }
    
    /* Set default policy settings */
    config->health_scan_interval = 300;     /* 5 minutes default */
    config->remap_threshold = 80;           /* Health score threshold */
    config->alert_threshold = 70;           /* Alert threshold */
    config->auto_remap_enabled = 1;         /* Enable automatic remapping */
    config->maintenance_mode = 0;           /* Normal operation mode */
    
    /* Calculate configuration CRC */
    config->config_crc = dm_remap_calculate_target_config_crc(config);
    
    printk(KERN_INFO "dm-remap: Created target configuration (%u sysfs settings)\n",
           config->sysfs_settings_count);
    
    return 0;
}

/* ========================================================================
 * SPARE DEVICE INFORMATION FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_create_spare_device_info - Create spare device information
 * @info: Spare device info structure to populate
 * @spare_devs: Array of spare devices
 * @spare_count: Number of spare devices
 * 
 * Creates comprehensive information about all spare devices associated
 * with this dm-remap target for reassembly purposes.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_create_spare_device_info(struct dm_remap_spare_device_info *info,
                                     struct dm_dev **spare_devs,
                                     int spare_count)
{
    int i, ret;
    u64 current_time = ktime_get_real_seconds();
    
    if (!info || !spare_devs || spare_count < 1 || spare_count > DM_REMAP_MAX_SPARES) {
        return -EINVAL;
    }
    
    memset(info, 0, sizeof(*info));
    
    /* Store spare device count */
    info->spare_count = spare_count;
    
    /* Create fingerprints for all spare devices */
    for (i = 0; i < spare_count; i++) {
        ret = dm_remap_create_device_fingerprint(&info->spares[i], spare_devs[i]);
        if (ret < 0) {
            printk(KERN_ERR "dm-remap: Failed to create fingerprint for spare %d: %d\n",
                   i, ret);
            return ret;
        }
        
        /* Initialize health information */
        info->spare_health_scores[i] = 100;    /* Start with perfect health */
        info->spare_last_checked[i] = current_time;
    }
    
    /* Set spare device policies */
    info->primary_spare_index = 0;               /* First spare is primary */
    info->load_balancing_policy = 0;             /* Round-robin default */
    info->spare_allocation_policy = 0;           /* Sequential allocation */
    
    /* Calculate spare info CRC */
    info->spare_info_crc = dm_remap_calculate_spare_info_crc(info);
    
    printk(KERN_INFO "dm-remap: Created spare device info for %d spare devices\n",
           spare_count);
    
    return 0;
}

/* ========================================================================
 * REASSEMBLY INSTRUCTIONS FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_create_reassembly_instructions - Create reassembly instructions
 * @instructions: Instructions structure to populate
 * @safety_level: Safety level (0=permissive, 1=standard, 2=strict)
 * 
 * Creates detailed instructions for automatic setup reassembly with
 * appropriate safety checks and validation requirements.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_create_reassembly_instructions(struct dm_remap_reassembly_instructions *instructions,
                                           u8 safety_level)
{
    if (!instructions) {
        return -EINVAL;
    }
    
    memset(instructions, 0, sizeof(*instructions));
    
    /* Set instruction version */
    instructions->instruction_version = 1;
    
    /* Configure safety settings based on level */
    switch (safety_level) {
    case 0: /* Permissive */
        instructions->requires_user_confirmation = 0;
        instructions->safe_mode_only = 0;
        instructions->validate_main_device_size = 1;
        instructions->validate_spare_device_sizes = 0;
        instructions->validate_filesystem_signatures = 0;
        instructions->allow_degraded_assembly = 1;
        instructions->allow_size_mismatch_recovery = 1;
        break;
        
    case 2: /* Strict */
        instructions->requires_user_confirmation = 1;
        instructions->safe_mode_only = 1;
        instructions->validate_main_device_size = 1;
        instructions->validate_spare_device_sizes = 1;
        instructions->validate_filesystem_signatures = 1;
        instructions->allow_degraded_assembly = 0;
        instructions->allow_size_mismatch_recovery = 0;
        break;
        
    default: /* Standard (level 1) */
        instructions->requires_user_confirmation = 0;
        instructions->safe_mode_only = 0;
        instructions->validate_main_device_size = 1;
        instructions->validate_spare_device_sizes = 1;
        instructions->validate_filesystem_signatures = 0;
        instructions->allow_degraded_assembly = 0;
        instructions->allow_size_mismatch_recovery = 0;
        break;
    }
    
    /* Set validation requirements */
    instructions->pre_assembly_checks = 0x0F;      /* Basic validation checks */
    instructions->post_assembly_verification = 0x07; /* Standard verifications */
    
    /* Calculate instructions CRC */
    instructions->instructions_crc = crc32(0, instructions, 
                                         sizeof(*instructions) - sizeof(instructions->instructions_crc));
    
    printk(KERN_INFO "dm-remap: Created reassembly instructions (safety level %u)\n",
           safety_level);
    
    return 0;
}

/* ========================================================================
 * METADATA INTEGRITY FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_create_metadata_integrity - Create integrity and versioning info
 * @integrity: Integrity structure to populate
 * @total_metadata_size: Total size of metadata structure
 * 
 * Creates comprehensive integrity protection and version control information.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_create_metadata_integrity(struct dm_remap_metadata_integrity *integrity,
                                      u32 total_metadata_size)
{
    u64 current_time = ktime_get_real_seconds();
    
    if (!integrity) {
        return -EINVAL;
    }
    
    memset(integrity, 0, sizeof(*integrity));
    
    /* Set magic number and version */
    integrity->magic = DM_REMAP_V4_MAGIC;
    integrity->version = DM_REMAP_V4_VERSION;
    strncpy(integrity->signature, DM_REMAP_METADATA_SIGNATURE, 
            DM_REMAP_SIGNATURE_SIZE - 1);
    
    /* Initialize version control */
    integrity->version_counter = 1;              /* Start at version 1 */
    integrity->creation_timestamp = current_time;
    integrity->last_update_timestamp = current_time;
    integrity->update_sequence_number = 1;
    
    /* Set metadata size information */
    integrity->metadata_size = total_metadata_size;
    
    /* Initialize redundancy information */
    integrity->total_copies = DM_REMAP_METADATA_LOCATIONS;
    integrity->minimum_valid_copies = 1;         /* Can recover from 1 valid copy */
    integrity->copy_location_map = 0x1F;         /* All 5 locations active */
    
    /* CRC calculations will be done later when full metadata is available */
    memset(integrity->individual_section_crcs, 0, sizeof(integrity->individual_section_crcs));
    integrity->overall_metadata_crc = 0;
    integrity->integrity_crc = 0;
    
    printk(KERN_INFO "dm-remap: Created metadata integrity info (version %llu)\n",
           integrity->version_counter);
    
    return 0;
}

/* ========================================================================
 * MASTER METADATA CREATION FUNCTION
 * ======================================================================== */

/**
 * dm_remap_v4_create_metadata - Create complete v4.0 metadata structure
 * @metadata: Metadata structure to populate
 * @main_dev: Main device
 * @spare_devs: Array of spare devices
 * @spare_count: Number of spare devices
 * @target_params: Original target parameters
 * 
 * Creates a complete v4.0 metadata structure with all components populated
 * and integrity protection in place.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_v4_create_metadata(struct dm_remap_v4_metadata *metadata,
                                struct dm_dev *main_dev,
                                struct dm_dev **spare_devs,
                                int spare_count,
                                const char *target_params)
{
    u64 target_size;
    int ret;
    
    if (!metadata || !main_dev || !spare_devs || !target_params) {
        return -EINVAL;
    }
    
    if (spare_count < 1 || spare_count > DM_REMAP_MAX_SPARES) {
        return -EINVAL;
    }
    
    memset(metadata, 0, sizeof(*metadata));
    
    /* Get target size from main device */
    target_size = bdev_nr_sectors(main_dev->bdev);
    
    /* Create integrity information first */
    ret = dm_remap_create_metadata_integrity(&metadata->integrity, sizeof(*metadata));
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Failed to create integrity info: %d\n", ret);
        return ret;
    }
    
    /* Create main device fingerprint */
    ret = dm_remap_create_device_fingerprint(&metadata->main_device, main_dev);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Failed to create main device fingerprint: %d\n", ret);
        return ret;
    }
    
    /* Create spare device information */
    ret = dm_remap_create_spare_device_info(&metadata->spare_devices, spare_devs, spare_count);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Failed to create spare device info: %d\n", ret);
        return ret;
    }
    
    /* Create target configuration */
    ret = dm_remap_create_target_configuration(&metadata->target_config, target_params,
                                              target_size, NULL, 0);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Failed to create target configuration: %d\n", ret);
        return ret;
    }
    
    /* Create reassembly instructions (standard safety level) */
    ret = dm_remap_create_reassembly_instructions(&metadata->reassembly, 1);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: Failed to create reassembly instructions: %d\n", ret);
        return ret;
    }
    
    /* Initialize legacy remap data */
    metadata->legacy_remap_data.remap_count = 0;
    metadata->legacy_remap_data.next_spare_sector = DM_REMAP_METADATA_RESERVED_SECTORS;
    
    /* Clear reserved expansion area */
    memset(metadata->reserved_expansion, 0, sizeof(metadata->reserved_expansion));
    
    /* Calculate final CRC */
    metadata->final_crc = dm_remap_calculate_metadata_crc(metadata);
    
    printk(KERN_INFO "dm-remap: Created complete v4.0 metadata (%zu bytes)\n", 
           sizeof(*metadata));
    printk(KERN_INFO "dm-remap: Main device: %s, Spare devices: %d\n",
           metadata->main_device.device_path, spare_count);
    
    return 0;
}

EXPORT_SYMBOL(dm_remap_create_device_fingerprint);
EXPORT_SYMBOL(dm_remap_match_device_fingerprint);
EXPORT_SYMBOL(dm_remap_v4_create_metadata);