/* dm-remap v4.0 CRC32 and Utility Functions
 * 
 * This file implements CRC32 calculation helpers and utility functions
 * for the v4.0 metadata integrity protection system.
 *
 * Author: dm-remap development team  
 * Version: 4.0
 * Date: October 2025
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/crc32.h>
#include <linux/string.h>
#include <linux/timekeeping.h>

#include "dm-remap-v4-metadata.h"

/* ========================================================================
 * CRC32 CALCULATION FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_calculate_device_fingerprint_crc - Calculate device fingerprint CRC32
 * @fp: Device fingerprint structure
 * 
 * Calculates CRC32 of device fingerprint excluding the CRC field itself.
 * 
 * Returns: CRC32 value
 */
u32 dm_remap_calculate_device_fingerprint_crc(const struct dm_remap_device_fingerprint *fp)
{
    if (!fp) {
        return 0;
    }
    
    /* Calculate CRC32 of structure excluding the CRC field */
    return crc32(0, fp, sizeof(*fp) - sizeof(fp->device_fingerprint_crc));
}

/**
 * dm_remap_calculate_target_config_crc - Calculate target configuration CRC32
 * @config: Target configuration structure
 * 
 * Calculates CRC32 of target configuration excluding the CRC field itself.
 * 
 * Returns: CRC32 value
 */
u32 dm_remap_calculate_target_config_crc(const struct dm_remap_target_configuration *config)
{
    if (!config) {
        return 0;
    }
    
    /* Calculate CRC32 of structure excluding the CRC field */
    return crc32(0, config, sizeof(*config) - sizeof(config->config_crc));
}

/**
 * dm_remap_calculate_spare_info_crc - Calculate spare device info CRC32
 * @info: Spare device info structure
 * 
 * Calculates CRC32 of spare device information excluding the CRC field itself.
 * 
 * Returns: CRC32 value
 */
u32 dm_remap_calculate_spare_info_crc(const struct dm_remap_spare_device_info *info)
{
    if (!info) {
        return 0;
    }
    
    /* Calculate CRC32 of structure excluding the CRC field */
    return crc32(0, info, sizeof(*info) - sizeof(info->spare_info_crc));
}

/**
 * dm_remap_calculate_metadata_crc - Calculate overall metadata CRC32
 * @metadata: Complete metadata structure
 * 
 * Calculates CRC32 of entire metadata structure excluding the final CRC field.
 * 
 * Returns: CRC32 value
 */
u32 dm_remap_calculate_metadata_crc(const struct dm_remap_v4_metadata *metadata)
{
    if (!metadata) {
        return 0;
    }
    
    /* Calculate CRC32 of entire structure excluding the final CRC field */
    return crc32(0, metadata, sizeof(*metadata) - sizeof(metadata->final_crc));
}

/**
 * dm_remap_calculate_section_crcs - Calculate individual section CRC32 values
 * @metadata: Complete metadata structure  
 * @section_crcs: Output array for section CRC values (must have 8 elements)
 * 
 * Calculates CRC32 for each major section of the metadata for granular
 * corruption detection and recovery.
 * 
 * Returns: Number of sections calculated (8)
 */
int dm_remap_calculate_section_crcs(const struct dm_remap_v4_metadata *metadata,
                                   u32 *section_crcs)
{
    if (!metadata || !section_crcs) {
        return -EINVAL;
    }
    
    /* Section 0: Integrity header */
    section_crcs[0] = crc32(0, &metadata->integrity, sizeof(metadata->integrity));
    
    /* Section 1: Main device fingerprint */
    section_crcs[1] = crc32(0, &metadata->main_device, sizeof(metadata->main_device));
    
    /* Section 2: Spare device information */
    section_crcs[2] = crc32(0, &metadata->spare_devices, sizeof(metadata->spare_devices));
    
    /* Section 3: Target configuration */
    section_crcs[3] = crc32(0, &metadata->target_config, sizeof(metadata->target_config));
    
    /* Section 4: Reassembly instructions */
    section_crcs[4] = crc32(0, &metadata->reassembly, sizeof(metadata->reassembly));
    
    /* Section 5: Legacy remap data */
    section_crcs[5] = crc32(0, &metadata->legacy_remap_data, sizeof(metadata->legacy_remap_data));
    
    /* Section 6: Reserved expansion area */
    section_crcs[6] = crc32(0, &metadata->reserved_expansion, sizeof(metadata->reserved_expansion));
    
    /* Section 7: Overall metadata (excluding final CRC) */
    section_crcs[7] = dm_remap_calculate_metadata_crc(metadata);
    
    return 8;
}

/* ========================================================================
 * VERSION CONTROL FUNCTIONS  
 * ======================================================================== */

/**
 * dm_remap_increment_version_counter - Increment metadata version counter
 * @metadata: Metadata structure to update
 * 
 * Increments the version counter and updates timestamps for conflict resolution.
 */
void dm_remap_increment_version_counter(struct dm_remap_v4_metadata *metadata)
{
    u64 current_time = ktime_get_real_seconds();
    
    if (!metadata) {
        return;
    }
    
    metadata->integrity.version_counter++;
    metadata->integrity.last_update_timestamp = current_time;
    metadata->integrity.update_sequence_number++;
    
    /* Recalculate integrity CRC after version update */
    metadata->integrity.integrity_crc = crc32(0, &metadata->integrity,
                                            sizeof(metadata->integrity) - sizeof(metadata->integrity.integrity_crc));
    
    /* Recalculate final metadata CRC */
    metadata->final_crc = dm_remap_calculate_metadata_crc(metadata);
}

/**
 * dm_remap_compare_metadata_versions - Compare version counters of two metadata copies
 * @meta1: First metadata structure
 * @meta2: Second metadata structure
 * 
 * Compares version counters to determine which metadata copy is newer.
 * Handles wraparound cases for long-running systems.
 * 
 * Returns: -1 if meta1 is older, 0 if same version, 1 if meta1 is newer
 */
int dm_remap_compare_metadata_versions(const struct dm_remap_v4_metadata *meta1,
                                      const struct dm_remap_v4_metadata *meta2)
{
    u64 version1, version2;
    u64 time1, time2;
    
    if (!meta1 || !meta2) {
        return 0;
    }
    
    version1 = meta1->integrity.version_counter;
    version2 = meta2->integrity.version_counter;
    
    /* Simple version comparison for most cases */
    if (version1 > version2) {
        return 1;
    } else if (version1 < version2) {
        return -1;
    }
    
    /* Same version counter - use timestamp as tiebreaker */
    time1 = meta1->integrity.last_update_timestamp;
    time2 = meta2->integrity.last_update_timestamp;
    
    if (time1 > time2) {
        return 1;
    } else if (time1 < time2) {
        return -1;
    }
    
    /* Finally use sequence number */
    if (meta1->integrity.update_sequence_number > meta2->integrity.update_sequence_number) {
        return 1;
    } else if (meta1->integrity.update_sequence_number < meta2->integrity.update_sequence_number) {
        return -1;
    }
    
    return 0; /* Identical versions */
}

/* ========================================================================
 * VALIDATION HELPER FUNCTIONS
 * ======================================================================== */

/**
 * dm_remap_validate_metadata_magic - Validate metadata magic number and signature
 * @metadata: Metadata structure to validate
 * 
 * Performs basic validation of magic number and signature to detect
 * corruption or incompatible metadata formats.
 * 
 * Returns: 0 if valid, negative error code if invalid
 */
int dm_remap_validate_metadata_magic(const struct dm_remap_v4_metadata *metadata)
{
    if (!metadata) {
        return -EINVAL;
    }
    
    /* Check magic number */
    if (metadata->integrity.magic != DM_REMAP_V4_MAGIC) {
        printk(KERN_ERR "dm-remap: Invalid metadata magic: 0x%08x (expected 0x%08x)\n",
               metadata->integrity.magic, DM_REMAP_V4_MAGIC);
        return -EINVAL;
    }
    
    /* Check version */
    if (metadata->integrity.version != DM_REMAP_V4_VERSION) {
        printk(KERN_WARNING "dm-remap: Metadata version mismatch: 0x%08x (expected 0x%08x)\n",
               metadata->integrity.version, DM_REMAP_V4_VERSION);
        /* Continue - might be compatible */
    }
    
    /* Check signature */
    if (strncmp(metadata->integrity.signature, DM_REMAP_METADATA_SIGNATURE, 
                strlen(DM_REMAP_METADATA_SIGNATURE)) != 0) {
        printk(KERN_ERR "dm-remap: Invalid metadata signature\n");
        return -EINVAL;
    }
    
    return 0;
}

/**
 * dm_remap_validate_metadata_crc - Validate metadata CRC integrity
 * @metadata: Metadata structure to validate
 * @section_errors: Optional output array for per-section error flags
 * 
 * Performs comprehensive CRC validation of metadata structure.
 * 
 * Returns: 0 if all CRCs valid, positive count of CRC errors, negative on error
 */
int dm_remap_validate_metadata_crc(const struct dm_remap_v4_metadata *metadata,
                                  u8 *section_errors)
{
    u32 calculated_crc, stored_crc;
    int error_count = 0;
    
    if (!metadata) {
        return -EINVAL;
    }
    
    /* Clear section errors if provided */
    if (section_errors) {
        memset(section_errors, 0, 8);
    }
    
    /* Validate device fingerprint CRC */
    calculated_crc = dm_remap_calculate_device_fingerprint_crc(&metadata->main_device);
    stored_crc = metadata->main_device.device_fingerprint_crc;
    if (calculated_crc != stored_crc) {
        error_count++;
        if (section_errors) section_errors[1] = 1;
        printk(KERN_ERR "dm-remap: Main device fingerprint CRC mismatch\n");
    }
    
    /* Validate target configuration CRC */
    calculated_crc = dm_remap_calculate_target_config_crc(&metadata->target_config);
    stored_crc = metadata->target_config.config_crc;
    if (calculated_crc != stored_crc) {
        error_count++;
        if (section_errors) section_errors[3] = 1;
        printk(KERN_ERR "dm-remap: Target configuration CRC mismatch\n");
    }
    
    /* Validate spare device info CRC */
    calculated_crc = dm_remap_calculate_spare_info_crc(&metadata->spare_devices);
    stored_crc = metadata->spare_devices.spare_info_crc;
    if (calculated_crc != stored_crc) {
        error_count++;
        if (section_errors) section_errors[2] = 1;
        printk(KERN_ERR "dm-remap: Spare device info CRC mismatch\n");
    }
    
    /* Validate overall metadata CRC */
    calculated_crc = dm_remap_calculate_metadata_crc(metadata);
    stored_crc = metadata->final_crc;
    if (calculated_crc != stored_crc) {
        error_count++;
        if (section_errors) section_errors[7] = 1;
        printk(KERN_ERR "dm-remap: Overall metadata CRC mismatch\n");
    }
    
    if (error_count == 0) {
        printk(KERN_INFO "dm-remap: All metadata CRC validations passed\n");
    } else {
        printk(KERN_WARNING "dm-remap: %d metadata CRC validation errors detected\n", 
               error_count);
    }
    
    return error_count;
}

EXPORT_SYMBOL(dm_remap_calculate_device_fingerprint_crc);
EXPORT_SYMBOL(dm_remap_calculate_target_config_crc);
EXPORT_SYMBOL(dm_remap_calculate_spare_info_crc);
EXPORT_SYMBOL(dm_remap_calculate_metadata_crc);
EXPORT_SYMBOL(dm_remap_increment_version_counter);
EXPORT_SYMBOL(dm_remap_compare_metadata_versions);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dm-remap development team");
MODULE_DESCRIPTION("dm-remap v4.0 metadata management functions");
MODULE_VERSION("4.0.0");