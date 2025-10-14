/*
 * dm-remap v4.0 Setup Reassembly Test Implementation
 * 
 * Simplified implementations of setup reassembly functions for testing
 * in userspace without full kernel environment.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
/* Mock kernel functions */
#define ktime_get_real_seconds() ((uint64_t)time(NULL))
#define DMINFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define DMWARN(fmt, ...) printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define DMERR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)

/* Include header */
#include "../include/dm-remap-v4-setup-reassembly.h"

/* Simple CRC32 implementation for testing */
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table(void)
{
    uint32_t c;
    int i, j;
    
    if (crc32_table_initialized) return;
    
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++) {
            if (c & 1) {
                c = 0xEDB88320L ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = true;
}

uint32_t crc32(uint32_t crc, const unsigned char *buf, size_t len)
{
    if (!crc32_table_initialized) {
        init_crc32_table();
    }
    
    crc = crc ^ 0xFFFFFFFF;
    while (len--) {
        crc = crc32_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* Mock UUID generation */
static void uuid_gen(uint8_t uuid[16])
{
    int i;
    srand(time(NULL) + getpid());
    for (i = 0; i < 16; i++) {
        uuid[i] = rand() % 256;
    }
    /* Set version and variant bits for UUID4 */
    uuid[6] = (uuid[6] & 0x0f) | 0x40; /* Version 4 */
    uuid[8] = (uuid[8] & 0x3f) | 0x80; /* Variant */
}

static bool uuid_equal(const uint8_t u1[16], const uint8_t u2[16])
{
    return memcmp(u1, u2, 16) == 0;
}

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
    
    /* Verify header CRC */
    uint32_t header_crc = crc32(0, (unsigned char *)&metadata->magic,
                               sizeof(metadata->magic) + sizeof(metadata->metadata_version) +
                               sizeof(metadata->version_counter));
    
    if (header_crc != metadata->header_crc32) {
        DMERR("Header CRC mismatch: expected 0x%x, got 0x%x",
              metadata->header_crc32, header_crc);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH;
    }
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Create device fingerprint for identification
 */
int dm_remap_v4_create_device_fingerprint(
    struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path)
{
    struct stat st;
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
    
    /* Try to get device information */
    if (stat(device_path, &st) == 0) {
        fingerprint->device_size = st.st_size / 512; /* Convert to sectors */
        fingerprint->device_capacity = st.st_size;
        fingerprint->sector_size = 512; /* Standard sector size */
    } else {
        /* For testing, create mock values */
        fingerprint->device_size = 1024; /* 1024 sectors */
        fingerprint->device_capacity = 1024 * 512; /* 512KB */
        fingerprint->sector_size = 512;
    }
    
    /* Generate UUID */
    uuid_gen(fingerprint->device_uuid);
    
    /* Create mock serial and model */
    snprintf(fingerprint->device_serial, sizeof(fingerprint->device_serial),
             "TEST-%08llx", (unsigned long long)fingerprint->device_size);
    snprintf(fingerprint->device_model, sizeof(fingerprint->device_model),
             "Test Block Device");
    
    fingerprint->device_type = 0x01; /* Generic block device */
    
    /* Calculate fingerprint CRC */
    fingerprint->fingerprint_crc32 = crc32(0, (unsigned char *)fingerprint,
                                          sizeof(*fingerprint) - sizeof(fingerprint->fingerprint_crc32));
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Verify device fingerprint
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
        DMERR("Device size mismatch: expected %lu, got %lu",
              (unsigned long)fingerprint->device_size, (unsigned long)current_fingerprint.device_size);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISMATCH;
    }
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Create complete setup metadata
 */
int dm_remap_v4_create_setup_metadata(
    struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_device_fingerprint *main_device,
    const struct dm_remap_v4_target_config *target_config)
{
    uint64_t current_time;
    static uint64_t version_counter = 1;
    
    if (!metadata || !main_device || !target_config) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    
    /* Initialize metadata */
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = DM_REMAP_V4_REASSEMBLY_MAGIC;
    metadata->metadata_version = 1;
    metadata->version_counter = version_counter++;
    metadata->created_timestamp = current_time;
    metadata->modified_timestamp = current_time;
    
    /* Set up description */
    snprintf(metadata->setup_description, sizeof(metadata->setup_description),
             "dm-remap v4.0 setup for %s", main_device->device_path);
    
    /* Copy device information */
    metadata->main_device = *main_device;
    metadata->num_spare_devices = 0;
    
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
                                  sizeof(metadata->magic) + sizeof(metadata->metadata_version) +
                                  sizeof(metadata->version_counter));
    
    metadata->devices_crc32 = crc32(0, (unsigned char *)&metadata->main_device,
                                   sizeof(metadata->main_device) + 
                                   sizeof(metadata->num_spare_devices) +
                                   sizeof(metadata->spare_devices));
    
    metadata->config_crc32 = crc32(0, (unsigned char *)&metadata->target_config,
                                  sizeof(metadata->target_config) +
                                  sizeof(metadata->sysfs_config) +
                                  sizeof(metadata->policy_config));
    
    metadata->overall_crc32 = dm_remap_v4_calculate_metadata_crc32(metadata);
    
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
    metadata->modified_timestamp = current_time;
    
    /* Recalculate checksums */
    metadata->devices_crc32 = crc32(0, (unsigned char *)&metadata->main_device,
                                   sizeof(metadata->main_device) + 
                                   sizeof(metadata->num_spare_devices) +
                                   sizeof(metadata->spare_devices));
    
    metadata->overall_crc32 = dm_remap_v4_calculate_metadata_crc32(metadata);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Calculate confidence score for discovery result
 */
float dm_remap_v4_calculate_confidence_score(const struct dm_remap_v4_discovery_result *result)
{
    float confidence = 0.0f;
    
    if (!result) {
        return 0.0f;
    }
    
    /* Base confidence from valid copies */
    if (result->copies_valid > 0) {
        confidence += 0.3f * ((float)result->copies_valid / (float)result->copies_found);
    }
    
    /* Bonus for multiple valid copies */
    if (result->copies_valid >= 3) {
        confidence += 0.2f;
    }
    
    /* Penalty for corruption */
    if (result->corruption_level > 0) {
        confidence -= 0.1f * (float)result->corruption_level / 10.0f;
    }
    
    /* Bonus for recent metadata */
    uint64_t current_time = ktime_get_real_seconds();
    uint64_t age_hours = (current_time - result->metadata.modified_timestamp) / 3600;
    if (age_hours < 24) {
        confidence += 0.1f;
    } else if (age_hours > 168) { /* > 1 week */
        confidence -= 0.1f;
    }
    
    /* Bonus for complete metadata (only if we have valid copies) */
    if (result->copies_valid > 0 &&
        result->metadata.magic == DM_REMAP_V4_REASSEMBLY_MAGIC &&
        result->metadata.num_spare_devices > 0) {
        confidence += 0.3f;
    }
    
    /* Clamp to valid range */
    if (confidence < 0.0f) confidence = 0.0f;
    if (confidence > 1.0f) confidence = 1.0f;
    
    return confidence;
}

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
    int i;
    
    if (!metadata) {
        DMINFO("Setup metadata: NULL");
        return;
    }
    
    DMINFO("=== Setup Metadata ===");
    DMINFO("Magic: 0x%x", metadata->magic);
    DMINFO("Version: %u (counter: %lu)", metadata->metadata_version, (unsigned long)metadata->version_counter);
    DMINFO("Description: %s", metadata->setup_description);
    DMINFO("Created: %lu, Modified: %lu", 
           (unsigned long)metadata->created_timestamp, (unsigned long)metadata->modified_timestamp);
    DMINFO("Main device: %s (%lu sectors)", 
           metadata->main_device.device_path, (unsigned long)metadata->main_device.device_size);
    DMINFO("Spare devices: %u", metadata->num_spare_devices);
    
    for (i = 0; i < metadata->num_spare_devices; i++) {
        DMINFO("  Spare %d: %s (priority: %u)", i + 1,
               metadata->spare_devices[i].spare_fingerprint.device_path,
               metadata->spare_devices[i].spare_priority);
    }
    
    DMINFO("Target params: %s", metadata->target_config.target_params);
    DMINFO("Target size: %lu sectors", (unsigned long)metadata->target_config.target_device_size);
    DMINFO("Metadata copies: %u", metadata->metadata_copies_count);
    DMINFO("Overall CRC32: 0x%x", metadata->overall_crc32);
}