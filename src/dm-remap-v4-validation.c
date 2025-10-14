/*
 * dm-remap v4.0 Metadata Validation Engine - Core Implementation
 * 
 * This module implements comprehensive metadata validation with multi-level
 * validation, fuzzy device matching, and intelligent error recovery.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/timekeeping.h>
#include <linux/crc32.h>
#include <linux/device-mapper.h>

#include "dm-remap-v4-metadata.h"
#include "dm-remap-v4-validation.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dm-remap development team");
MODULE_DESCRIPTION("dm-remap v4.0 Metadata Validation Engine");
MODULE_VERSION("4.0.0");

/*
 * Validation error message templates
 */
static const char *validation_error_messages[] = {
    "Invalid magic number (expected 0x%08x, got 0x%08x)",
    "Unsupported version (expected 0x%08x, got 0x%08x)", 
    "Invalid metadata size (expected %u, got %u)",
    "CRC32 checksum mismatch (expected 0x%08x, got 0x%08x)",
    "Invalid sequence number (%llu)",
    "Invalid timestamp (%llu, current time %llu)",
    "Invalid target configuration at index %u",
    "Invalid spare device info at index %u",
    "Invalid reassembly instructions",
    "Device fingerprint mismatch for %s",
    "Device size mismatch (expected %llu, got %llu)",
    "Device path changed (was %s, now %s)",
    "Internal consistency error: %s"
};

/*
 * Recovery suggestion templates
 */
static const char *recovery_suggestions[] = {
    "Metadata appears corrupted - try loading from backup copy at sector %llu",
    "Device path changed - update device mapping or use fuzzy matching",
    "Device size changed - verify device integrity and update configuration",
    "Minor checksum error - metadata may be repairable with auto-repair function",
    "Sequence number conflict - use newer metadata or manual conflict resolution",
    "Timestamp in future - check system clock or ignore timestamp validation",
    "Missing device - reconnect device or use alternative spare",
    "Configuration mismatch - update target configuration to match current state"
};

/*
 * Initialize validation context with default values
 */
void dm_remap_v4_init_validation_context(struct dm_remap_v4_validation_context *context)
{
    if (!context)
        return;
    
    memset(context, 0, sizeof(*context));
    context->validation_level = DM_REMAP_V4_VALIDATION_STANDARD;
    context->current_time = ktime_get_real_seconds();
    context->allow_fuzzy_matching = true;
    context->strict_size_checking = false;
    context->require_exact_paths = false;
}

/*
 * Initialize validation result structure
 */
void dm_remap_v4_init_validation_result(struct dm_remap_v4_validation_result *result)
{
    if (!result)
        return;
    
    memset(result, 0, sizeof(*result));
    result->validation_time = ktime_get_real_seconds();
}

/*
 * Add error message to validation result
 */
static void add_validation_error(struct dm_remap_v4_validation_result *result,
                                const char *format, ...)
{
    va_list args;
    char temp_msg[256];
    size_t current_len, remaining;
    
    if (!result)
        return;
    
    va_start(args, format);
    vsnprintf(temp_msg, sizeof(temp_msg), format, args);
    va_end(args);
    
    current_len = strlen(result->error_messages);
    remaining = sizeof(result->error_messages) - current_len - 1;
    
    if (remaining > 0) {
        if (current_len > 0) {
            strncat(result->error_messages, "; ", remaining);
            remaining -= 2;
        }
        strncat(result->error_messages, temp_msg, remaining);
    }
    
    result->error_count++;
}

/*
 * Add recovery suggestion to validation result
 */
static void add_recovery_suggestion(struct dm_remap_v4_validation_result *result,
                                   const char *format, ...)
{
    va_list args;
    char temp_msg[256];
    size_t current_len, remaining;
    
    if (!result)
        return;
    
    va_start(args, format);
    vsnprintf(temp_msg, sizeof(temp_msg), format, args);
    va_end(args);
    
    current_len = strlen(result->recovery_suggestions);
    remaining = sizeof(result->recovery_suggestions) - current_len - 1;
    
    if (remaining > 0) {
        if (current_len > 0) {
            strncat(result->recovery_suggestions, "; ", remaining);
            remaining -= 2;
        }
        strncat(result->recovery_suggestions, temp_msg, remaining);
    }
}

/*
 * Validate metadata header fields
 */
int dm_remap_v4_validate_header(const struct dm_remap_v4_metadata_header *header,
                               uint32_t validation_level,
                               struct dm_remap_v4_validation_result *result)
{
    uint64_t current_time;
    
    if (!header || !result)
        return -EINVAL;
    
    /* Check magic number */
    if (header->magic != DM_REMAP_V4_MAGIC) {
        result->flags |= DM_REMAP_V4_INVALID_MAGIC;
        add_validation_error(result, validation_error_messages[0],
                           DM_REMAP_V4_MAGIC, header->magic);
    }
    
    /* Check version */
    if (header->version != DM_REMAP_V4_VERSION) {
        result->flags |= DM_REMAP_V4_INVALID_VERSION;
        add_validation_error(result, validation_error_messages[1],
                           DM_REMAP_V4_VERSION, header->version);
    }
    
    /* Check metadata size */
    if (header->metadata_size != sizeof(struct dm_remap_v4_metadata)) {
        result->flags |= DM_REMAP_V4_INVALID_SIZE;
        add_validation_error(result, validation_error_messages[2],
                           (uint32_t)sizeof(struct dm_remap_v4_metadata),
                           header->metadata_size);
    }
    
    /* Check sequence number (must be non-zero in strict mode) */
    if ((validation_level & DM_REMAP_V4_VALIDATION_STRICT) && 
        header->sequence_number == 0) {
        result->flags |= DM_REMAP_V4_INVALID_SEQUENCE;
        add_validation_error(result, validation_error_messages[4],
                           header->sequence_number);
    }
    
    /* Check timestamp (shouldn't be too far in the future) */
    current_time = ktime_get_real_seconds();
    if (header->creation_time > current_time + 3600) { /* Allow 1 hour clock skew */
        result->flags |= DM_REMAP_V4_INVALID_TIMESTAMP;
        add_validation_error(result, validation_error_messages[5],
                           header->creation_time, current_time);
    }
    
    /* Check target and spare counts */
    if (header->num_targets > DM_REMAP_V4_MAX_TARGETS) {
        result->flags |= DM_REMAP_V4_INVALID_TARGETS;
        add_validation_error(result, "Too many targets (%u > %u)",
                           header->num_targets, DM_REMAP_V4_MAX_TARGETS);
    }
    
    if (header->num_spares > DM_REMAP_V4_MAX_SPARES) {
        result->flags |= DM_REMAP_V4_INVALID_SPARES;
        add_validation_error(result, "Too many spares (%u > %u)",
                           header->num_spares, DM_REMAP_V4_MAX_SPARES);
    }
    
    return (result->flags == DM_REMAP_V4_VALID) ? 0 : -EINVAL;
}

/*
 * Verify CRC32 checksums throughout the metadata
 */
int dm_remap_v4_verify_integrity(const struct dm_remap_v4_metadata *metadata,
                                struct dm_remap_v4_validation_result *result)
{
    uint32_t calculated_crc;
    const unsigned char *data_start;
    size_t data_size;
    
    if (!metadata || !result)
        return -EINVAL;
    
    /* Calculate CRC32 of everything except the header CRC field */
    data_start = (const unsigned char *)&metadata->targets;
    data_size = sizeof(metadata->targets) + sizeof(metadata->spares) + 
                sizeof(metadata->reassembly);
    
    calculated_crc = crc32(0, data_start, data_size);
    
    if (calculated_crc != metadata->header.crc32) {
        result->flags |= DM_REMAP_V4_INVALID_CHECKSUM;
        add_validation_error(result, validation_error_messages[3],
                           metadata->header.crc32, calculated_crc);
        add_recovery_suggestion(result, recovery_suggestions[3]);
        return -EINVAL;
    }
    
    return 0;
}

/*
 * Structural validation - validates basic structure integrity
 */
int dm_remap_v4_validate_structure(const struct dm_remap_v4_metadata *metadata,
                                  struct dm_remap_v4_validation_result *result)
{
    int ret = 0;
    
    if (!metadata || !result)
        return -EINVAL;
    
    /* Validate header */
    ret = dm_remap_v4_validate_header(&metadata->header, 
                                     DM_REMAP_V4_VALIDATION_MINIMAL, result);
    
    /* Verify integrity if requested */
    if (ret == 0) {
        ret = dm_remap_v4_verify_integrity(metadata, result);
    }
    
    return ret;
}

/*
 * Calculate match confidence between two fingerprints
 */
uint32_t dm_remap_v4_calculate_match_confidence(
    const struct dm_remap_v4_device_fingerprint *expected,
    const struct dm_remap_v4_device_fingerprint *actual)
{
    uint32_t confidence = 0;
    uint32_t matches = 0;
    uint32_t total_checks = 0;
    
    if (!expected || !actual)
        return 0;
    
    /* UUID match (highest weight) */
    total_checks++;
    if (strlen(expected->device_uuid) > 0 && strlen(actual->device_uuid) > 0) {
        if (strcmp(expected->device_uuid, actual->device_uuid) == 0) {
            matches++;
            confidence += 40; /* UUID match is very strong */
        }
    }
    
    /* Path match (medium weight) */
    total_checks++;
    if (strlen(expected->device_path) > 0 && strlen(actual->device_path) > 0) {
        if (strcmp(expected->device_path, actual->device_path) == 0) {
            matches++;
            confidence += 25; /* Path match is moderately strong */
        }
    }
    
    /* Size match (medium weight) */
    total_checks++;
    if (expected->device_size > 0 && actual->device_size > 0) {
        if (expected->device_size == actual->device_size) {
            matches++;
            confidence += 25; /* Size match is moderately strong */
        } else if (abs((long)(expected->device_size - actual->device_size)) < 
                  (expected->device_size / 100)) {
            /* Size within 1% - partial match */
            confidence += 15;
        }
    }
    
    /* Serial hash match (lower weight) */
    total_checks++;
    if (expected->serial_hash != 0 && actual->serial_hash != 0) {
        if (expected->serial_hash == actual->serial_hash) {
            matches++;
            confidence += 10; /* Serial hash is weaker due to potential collisions */
        }
    }
    
    /* Ensure confidence doesn't exceed 100 */
    if (confidence > 100)
        confidence = 100;
    
    return confidence;
}

/*
 * Fuzzy device matching with confidence scoring
 */
int dm_remap_v4_fuzzy_match_device(const struct dm_remap_v4_device_fingerprint *fingerprint,
                                  struct dm_dev *candidate_device,
                                  struct dm_remap_v4_device_match *match)
{
    struct dm_remap_v4_device_fingerprint current_fingerprint;
    int ret;
    
    if (!fingerprint || !candidate_device || !match)
        return -EINVAL;
    
    /* Initialize match result */
    memset(match, 0, sizeof(*match));
    strncpy(match->matched_device_path, candidate_device->name, 
            sizeof(match->matched_device_path) - 1);
    
    /* Create fingerprint for current device */
    memset(&current_fingerprint, 0, sizeof(current_fingerprint));
    
    /* Get device path */
    strncpy(current_fingerprint.device_path, candidate_device->name,
            sizeof(current_fingerprint.device_path) - 1);
    
    /* Get device size */
    current_fingerprint.device_size = bdev_nr_sectors(candidate_device->bdev) * 512;
    
    /* Generate serial hash from device name (simplified) */
    current_fingerprint.serial_hash = crc32(0, 
        (unsigned char*)candidate_device->bdev->bd_disk->disk_name,
        strlen(candidate_device->bdev->bd_disk->disk_name));
    
    /* Calculate match confidence */
    match->confidence = dm_remap_v4_calculate_match_confidence(fingerprint, 
                                                              &current_fingerprint);
    match->fingerprint = current_fingerprint;
    
    /* Set match flags based on individual criteria */
    if (strcmp(fingerprint->device_path, current_fingerprint.device_path) == 0)
        match->match_flags |= 0x01; /* Path match */
    
    if (fingerprint->device_size == current_fingerprint.device_size)
        match->match_flags |= 0x02; /* Size match */
    
    if (fingerprint->serial_hash == current_fingerprint.serial_hash)
        match->match_flags |= 0x04; /* Serial match */
    
    if (strlen(fingerprint->device_uuid) > 0 && 
        strlen(current_fingerprint.device_uuid) > 0 &&
        strcmp(fingerprint->device_uuid, current_fingerprint.device_uuid) == 0)
        match->match_flags |= 0x08; /* UUID match */
    
    /* Generate matching notes */
    if (match->confidence >= DM_REMAP_V4_MATCH_PERFECT) {
        strncpy(match->notes, "Perfect match on all criteria", sizeof(match->notes) - 1);
    } else if (match->confidence >= DM_REMAP_V4_MATCH_HIGH) {
        strncpy(match->notes, "High confidence match", sizeof(match->notes) - 1);
    } else if (match->confidence >= DM_REMAP_V4_MATCH_MEDIUM) {
        strncpy(match->notes, "Medium confidence match - verify manually", sizeof(match->notes) - 1);
    } else if (match->confidence >= DM_REMAP_V4_MATCH_LOW) {
        strncpy(match->notes, "Low confidence match - likely wrong device", sizeof(match->notes) - 1);
    } else {
        strncpy(match->notes, "Poor match - probably not the correct device", sizeof(match->notes) - 1);
    }
    
    return 0;
}

/*
 * Validate target configuration
 */
int dm_remap_v4_validate_targets(const struct dm_remap_v4_target_config *targets,
                                uint32_t num_targets,
                                const struct dm_remap_v4_validation_context *context,
                                struct dm_remap_v4_validation_result *result)
{
    uint32_t i;
    int ret = 0;
    
    if (!targets || !result)
        return -EINVAL;
    
    for (i = 0; i < num_targets; i++) {
        const struct dm_remap_v4_target_config *target = &targets[i];
        
        /* Check basic target fields */
        if (target->length == 0) {
            result->flags |= DM_REMAP_V4_INVALID_TARGETS;
            add_validation_error(result, "Target %u has zero length", i);
            ret = -EINVAL;
        }
        
        if (strlen(target->device_name) == 0) {
            result->flags |= DM_REMAP_V4_INVALID_TARGETS;
            add_validation_error(result, "Target %u has empty device name", i);
            ret = -EINVAL;
        }
        
        if (strlen(target->target_type) == 0) {
            result->flags |= DM_REMAP_V4_INVALID_TARGETS;
            add_validation_error(result, "Target %u has empty target type", i);
            ret = -EINVAL;
        }
        
        /* Check for overlapping targets */
        for (uint32_t j = i + 1; j < num_targets; j++) {
            const struct dm_remap_v4_target_config *other = &targets[j];
            
            /* Check for sector range overlap */
            if (target->start_sector < other->start_sector + other->length &&
                other->start_sector < target->start_sector + target->length) {
                result->flags |= DM_REMAP_V4_CONSISTENCY_ERROR;
                add_validation_error(result, "Targets %u and %u overlap", i, j);
                ret = -EINVAL;
            }
        }
    }
    
    return ret;
}

/*
 * Validate spare device information
 */
int dm_remap_v4_validate_spares(const struct dm_remap_v4_spare_device_info *spares,
                               uint32_t num_spares,
                               const struct dm_remap_v4_validation_context *context,
                               struct dm_remap_v4_validation_result *result)
{
    uint32_t i;
    int ret = 0;
    
    if (!spares || !result)
        return -EINVAL;
    
    for (i = 0; i < num_spares; i++) {
        const struct dm_remap_v4_spare_device_info *spare = &spares[i];
        
        /* Check device size (must be at least 8MB for metadata) */
        if (spare->device_size < (8 * 1024 * 1024)) {
            result->flags |= DM_REMAP_V4_INVALID_SPARES;
            add_validation_error(result, "Spare %u too small (%llu bytes, need 8MB)", 
                               i, spare->device_size);
            ret = -EINVAL;
        }
        
        /* Check fingerprint has some identifying information */
        if (strlen(spare->fingerprint.device_path) == 0 &&
            strlen(spare->fingerprint.device_uuid) == 0 &&
            spare->fingerprint.serial_hash == 0) {
            result->flags |= DM_REMAP_V4_INVALID_SPARES;
            add_validation_error(result, "Spare %u has no identifying information", i);
            ret = -EINVAL;
        }
        
        /* If we have available devices, try to match */
        if (context && context->available_devices && context->num_devices > 0) {
            struct dm_remap_v4_device_match best_match;
            bool found_match = false;
            
            for (uint32_t j = 0; j < context->num_devices; j++) {
                struct dm_remap_v4_device_match match;
                
                if (dm_remap_v4_fuzzy_match_device(&spare->fingerprint,
                                                  context->available_devices[j],
                                                  &match) == 0) {
                    if (match.confidence >= DM_REMAP_V4_MATCH_MEDIUM) {
                        best_match = match;
                        found_match = true;
                        break;
                    }
                }
            }
            
            if (!found_match && (context->validation_level & DM_REMAP_V4_VALIDATION_STRICT)) {
                result->flags |= DM_REMAP_V4_DEVICE_MISMATCH;
                add_validation_error(result, "Spare %u device not found", i);
                add_recovery_suggestion(result, recovery_suggestions[6]);
                ret = -EINVAL;
            }
        }
    }
    
    return ret;
}

/*
 * Primary validation function - validates complete metadata structure
 */
int dm_remap_v4_validate_metadata_comprehensive(const struct dm_remap_v4_metadata *metadata,
                                               const struct dm_remap_v4_validation_context *context,
                                               struct dm_remap_v4_validation_result *result)
{
    int ret = 0;
    uint32_t validation_level;
    
    if (!metadata || !result)
        return -EINVAL;
    
    validation_level = context ? context->validation_level : DM_REMAP_V4_VALIDATION_STANDARD;
    result->validation_level = validation_level;
    
    /* Level 1: Structural validation */
    ret = dm_remap_v4_validate_structure(metadata, result);
    if (ret != 0 && (validation_level & DM_REMAP_V4_VALIDATION_MINIMAL))
        return ret;
    
    /* Level 2: Header validation */
    ret = dm_remap_v4_validate_header(&metadata->header, validation_level, result);
    if (ret != 0 && (validation_level & DM_REMAP_V4_VALIDATION_STANDARD))
        return ret;
    
    /* Level 3: Target validation */
    if (metadata->header.num_targets > 0) {
        ret = dm_remap_v4_validate_targets(metadata->targets, 
                                          metadata->header.num_targets,
                                          context, result);
    }
    
    /* Level 4: Spare validation */
    if (metadata->header.num_spares > 0) {
        ret = dm_remap_v4_validate_spares(metadata->spares,
                                         metadata->header.num_spares,
                                         context, result);
    }
    
    /* Set recovery possible flag if we have non-critical errors */
    if (result->error_count > 0 && result->error_count <= 3 &&
        !(result->flags & (DM_REMAP_V4_INVALID_MAGIC | DM_REMAP_V4_INVALID_VERSION))) {
        result->flags |= DM_REMAP_V4_RECOVERY_POSSIBLE;
    }
    
    return (result->error_count == 0) ? 0 : -EINVAL;
}

EXPORT_SYMBOL(dm_remap_v4_validate_metadata_comprehensive);
EXPORT_SYMBOL(dm_remap_v4_validate_structure);
EXPORT_SYMBOL(dm_remap_v4_validate_header);
EXPORT_SYMBOL(dm_remap_v4_verify_integrity);
EXPORT_SYMBOL(dm_remap_v4_calculate_match_confidence);
EXPORT_SYMBOL(dm_remap_v4_fuzzy_match_device);
EXPORT_SYMBOL(dm_remap_v4_init_validation_context);
EXPORT_SYMBOL(dm_remap_v4_init_validation_result);