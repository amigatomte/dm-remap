/*
 * dm-remap v4.0 Metadata Validation Utilities
 * 
 * Utility functions for validation result formatting, error recovery,
 * and validation context management.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/crc32.h>

#include "dm-remap-v4-validation.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dm-remap development team");
MODULE_DESCRIPTION("dm-remap v4.0 Validation Utilities");
MODULE_VERSION("4.0.0");

/*
 * Validation flag descriptions for human-readable output
 */
static const struct {
    uint32_t flag;
    const char *description;
} validation_flag_descriptions[] = {
    {DM_REMAP_V4_INVALID_MAGIC, "Invalid magic number"},
    {DM_REMAP_V4_INVALID_VERSION, "Unsupported version"},
    {DM_REMAP_V4_INVALID_SIZE, "Invalid metadata size"},
    {DM_REMAP_V4_INVALID_CHECKSUM, "CRC32 checksum mismatch"},
    {DM_REMAP_V4_INVALID_SEQUENCE, "Invalid sequence number"},
    {DM_REMAP_V4_INVALID_TIMESTAMP, "Invalid timestamp"},
    {DM_REMAP_V4_INVALID_TARGETS, "Invalid target configuration"},
    {DM_REMAP_V4_INVALID_SPARES, "Invalid spare device info"},
    {DM_REMAP_V4_INVALID_REASSEMBLY, "Invalid reassembly instructions"},
    {DM_REMAP_V4_DEVICE_MISMATCH, "Device fingerprint mismatch"},
    {DM_REMAP_V4_PARTIAL_MATCH, "Partial device match"},
    {DM_REMAP_V4_SIZE_MISMATCH, "Device size mismatch"},
    {DM_REMAP_V4_PATH_CHANGED, "Device path changed"},
    {DM_REMAP_V4_SERIAL_CHANGED, "Device serial changed"},
    {DM_REMAP_V4_CONSISTENCY_ERROR, "Internal consistency error"},
    {DM_REMAP_V4_RECOVERY_POSSIBLE, "Recovery possible"},
    {0, NULL}
};

/*
 * Validation level names
 */
static const struct {
    uint32_t level;
    const char *name;
} validation_level_names[] = {
    {DM_REMAP_V4_VALIDATION_MINIMAL, "Minimal"},
    {DM_REMAP_V4_VALIDATION_STANDARD, "Standard"},
    {DM_REMAP_V4_VALIDATION_STRICT, "Strict"},
    {DM_REMAP_V4_VALIDATION_PARANOID, "Paranoid"},
    {0, NULL}
};

/*
 * Convert validation flags to human-readable string
 */
const char *dm_remap_v4_validation_flags_to_string(uint32_t flags)
{
    static char result[1024];
    size_t pos = 0;
    int i;
    bool first = true;
    
    if (flags == DM_REMAP_V4_VALID) {
        return "Valid";
    }
    
    result[0] = '\0';
    
    for (i = 0; validation_flag_descriptions[i].description; i++) {
        if (flags & validation_flag_descriptions[i].flag) {
            if (!first && pos < sizeof(result) - 3) {
                strcat(result, ", ");
                pos += 2;
            }
            
            if (pos < sizeof(result) - strlen(validation_flag_descriptions[i].description) - 1) {
                strcat(result, validation_flag_descriptions[i].description);
                pos += strlen(validation_flag_descriptions[i].description);
                first = false;
            }
        }
    }
    
    return result;
}

/*
 * Get validation level name as string
 */
const char *dm_remap_v4_validation_level_to_string(uint32_t level)
{
    int i;
    
    for (i = 0; validation_level_names[i].name; i++) {
        if (level == validation_level_names[i].level) {
            return validation_level_names[i].name;
        }
    }
    
    /* Handle combined levels */
    if (level & DM_REMAP_V4_VALIDATION_PARANOID)
        return "Paranoid";
    if (level & DM_REMAP_V4_VALIDATION_STRICT)
        return "Strict";
    if (level & DM_REMAP_V4_VALIDATION_STANDARD)
        return "Standard";
    if (level & DM_REMAP_V4_VALIDATION_MINIMAL)
        return "Minimal";
    
    return "Unknown";
}

/*
 * Check if metadata can be automatically repaired
 */
bool dm_remap_v4_is_repairable(const struct dm_remap_v4_validation_result *result)
{
    if (!result)
        return false;
    
    /* Cannot repair critical structural issues */
    if (result->flags & (DM_REMAP_V4_INVALID_MAGIC | 
                        DM_REMAP_V4_INVALID_VERSION |
                        DM_REMAP_V4_INVALID_SIZE)) {
        return false;
    }
    
    /* Can potentially repair these issues */
    if (result->flags & (DM_REMAP_V4_INVALID_CHECKSUM |
                        DM_REMAP_V4_INVALID_SEQUENCE |
                        DM_REMAP_V4_INVALID_TIMESTAMP |
                        DM_REMAP_V4_PATH_CHANGED)) {
        return true;
    }
    
    /* Recovery flag explicitly set */
    if (result->flags & DM_REMAP_V4_RECOVERY_POSSIBLE) {
        return true;
    }
    
    return false;
}

/*
 * Generate comprehensive recovery suggestions
 */
int dm_remap_v4_generate_recovery_suggestions(const struct dm_remap_v4_metadata *metadata,
                                             const struct dm_remap_v4_validation_result *result,
                                             char *suggestions,
                                             size_t suggestions_len)
{
    size_t pos = 0;
    
    if (!metadata || !result || !suggestions || suggestions_len == 0)
        return -EINVAL;
    
    suggestions[0] = '\0';
    
    /* Critical errors - suggest backup recovery */
    if (result->flags & DM_REMAP_V4_INVALID_MAGIC) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "CRITICAL: Invalid magic number detected. "
                       "Try loading metadata from backup copies at sectors 1024, 2048, 4096, or 8192. ");
    }
    
    if (result->flags & DM_REMAP_V4_INVALID_VERSION) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "CRITICAL: Unsupported version. "
                       "This may require metadata format conversion or use of older dm-remap version. ");
    }
    
    /* Checksum errors */
    if (result->flags & DM_REMAP_V4_INVALID_CHECKSUM) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Checksum mismatch detected. "
                       "Try: 1) Load from backup metadata copy, "
                       "2) Use auto-repair function if available, "
                       "3) Manually verify and recreate metadata. ");
    }
    
    /* Device matching issues */
    if (result->flags & DM_REMAP_V4_DEVICE_MISMATCH) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Device not found or changed. "
                       "Try: 1) Reconnect missing device, "
                       "2) Enable fuzzy matching, "
                       "3) Update device paths in configuration. ");
    }
    
    if (result->flags & DM_REMAP_V4_PATH_CHANGED) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Device path changed. "
                       "Try: 1) Update udev rules for consistent naming, "
                       "2) Use UUID-based device identification, "
                       "3) Enable path-independent matching. ");
    }
    
    if (result->flags & DM_REMAP_V4_SIZE_MISMATCH) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Device size changed. "
                       "CAUTION: Verify device integrity before proceeding. "
                       "Try: 1) Check for device errors, "
                       "2) Update metadata if device legitimately resized. ");
    }
    
    /* Sequence/timestamp issues */
    if (result->flags & DM_REMAP_V4_INVALID_SEQUENCE) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Invalid sequence number. "
                       "Try: 1) Use metadata with higher sequence number, "
                       "2) Manually resolve version conflicts. ");
    }
    
    if (result->flags & DM_REMAP_V4_INVALID_TIMESTAMP) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Invalid timestamp (future date). "
                       "Try: 1) Check system clock, "
                       "2) Ignore timestamp validation if clock was wrong during creation. ");
    }
    
    /* Configuration issues */
    if (result->flags & DM_REMAP_V4_INVALID_TARGETS) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Invalid target configuration. "
                       "Try: 1) Verify target device availability, "
                       "2) Check for overlapping target ranges, "
                       "3) Recreate target configuration. ");
    }
    
    if (result->flags & DM_REMAP_V4_INVALID_SPARES) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Invalid spare device configuration. "
                       "Try: 1) Ensure spare devices are at least 8MB, "
                       "2) Verify spare device accessibility, "
                       "3) Update spare device fingerprints. ");
    }
    
    /* Consistency issues */
    if (result->flags & DM_REMAP_V4_CONSISTENCY_ERROR) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Internal consistency error detected. "
                       "Try: 1) Recreate metadata from scratch, "
                       "2) Check for data corruption, "
                       "3) Use backup metadata copy. ");
    }
    
    /* General recovery suggestions if no specific errors but recovery flagged */
    if ((result->flags & DM_REMAP_V4_RECOVERY_POSSIBLE) && pos == 0) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "Minor issues detected that may be recoverable. "
                       "Try: 1) Auto-repair function, "
                       "2) Load from backup metadata, "
                       "3) Manual metadata recreation. ");
    }
    
    /* Add general advice if we have any suggestions */
    if (pos > 0 && pos < suggestions_len - 100) {
        pos += snprintf(suggestions + pos, suggestions_len - pos,
                       "ALWAYS backup current data before attempting recovery. "
                       "Consider running validation in paranoid mode for detailed analysis.");
    }
    
    return 0;
}

/*
 * Attempt automatic repair of minor metadata issues
 */
int dm_remap_v4_auto_repair(struct dm_remap_v4_metadata *metadata,
                           const struct dm_remap_v4_validation_context *context,
                           struct dm_remap_v4_validation_result *result)
{
    int repairs_made = 0;
    uint32_t new_crc;
    uint64_t current_time;
    
    if (!metadata || !result)
        return -EINVAL;
    
    /* Cannot repair critical structural issues */
    if (result->flags & (DM_REMAP_V4_INVALID_MAGIC |
                        DM_REMAP_V4_INVALID_VERSION |
                        DM_REMAP_V4_INVALID_SIZE)) {
        return -EINVAL;
    }
    
    /* Fix invalid sequence number */
    if (result->flags & DM_REMAP_V4_INVALID_SEQUENCE) {
        if (metadata->header.sequence_number == 0) {
            metadata->header.sequence_number = 1;
            repairs_made++;
            result->flags &= ~DM_REMAP_V4_INVALID_SEQUENCE;
        }
    }
    
    /* Fix timestamp if it's in the future (assume clock was wrong) */
    if (result->flags & DM_REMAP_V4_INVALID_TIMESTAMP) {
        current_time = ktime_get_real_seconds();
        if (metadata->header.creation_time > current_time + 3600) {
            metadata->header.creation_time = current_time;
            repairs_made++;
            result->flags &= ~DM_REMAP_V4_INVALID_TIMESTAMP;
        }
    }
    
    /* Recalculate CRC32 if we made any changes or if CRC was wrong */
    if (repairs_made > 0 || (result->flags & DM_REMAP_V4_INVALID_CHECKSUM)) {
        const unsigned char *data_start = (const unsigned char *)&metadata->targets;
        size_t data_size = sizeof(metadata->targets) + sizeof(metadata->spares) + 
                          sizeof(metadata->reassembly);
        
        new_crc = crc32(0, data_start, data_size);
        
        if (metadata->header.crc32 != new_crc) {
            metadata->header.crc32 = new_crc;
            repairs_made++;
            result->flags &= ~DM_REMAP_V4_INVALID_CHECKSUM;
        }
    }
    
    /* Update error count */
    if (repairs_made > 0) {
        result->error_count = max(0, (int)result->error_count - repairs_made);
        
        /* Add repair notification to error messages */
        if (strlen(result->error_messages) > 0) {
            strncat(result->error_messages, "; ", 
                   sizeof(result->error_messages) - strlen(result->error_messages) - 1);
        }
        
        snprintf(result->error_messages + strlen(result->error_messages),
                sizeof(result->error_messages) - strlen(result->error_messages),
                "AUTO-REPAIRED %d issues", repairs_made);
    }
    
    return repairs_made;
}

/*
 * Find best matching device from available devices
 */
struct dm_dev *dm_remap_v4_find_best_match(const struct dm_remap_v4_device_fingerprint *fingerprint,
                                          const struct dm_remap_v4_validation_context *context,
                                          struct dm_remap_v4_device_match *best_match)
{
    struct dm_dev *best_device = NULL;
    uint32_t best_confidence = 0;
    uint32_t i;
    
    if (!fingerprint || !context || !context->available_devices || !best_match)
        return NULL;
    
    memset(best_match, 0, sizeof(*best_match));
    
    for (i = 0; i < context->num_devices; i++) {
        struct dm_remap_v4_device_match match;
        
        if (dm_remap_v4_fuzzy_match_device(fingerprint, context->available_devices[i], 
                                          &match) == 0) {
            if (match.confidence > best_confidence) {
                best_confidence = match.confidence;
                best_device = context->available_devices[i];
                *best_match = match;
            }
        }
    }
    
    /* Only return device if confidence meets minimum threshold */
    if (best_confidence < DM_REMAP_V4_MATCH_LOW) {
        return NULL;
    }
    
    return best_device;
}

/*
 * Validate reassembly instruction structure
 */
int dm_remap_v4_validate_reassembly(const struct dm_remap_v4_reassembly_instructions *reassembly,
                                    struct dm_remap_v4_validation_result *result)
{
    if (!reassembly || !result)
        return -EINVAL;
    
    /* Check reassembly mode is within valid range */
    if (reassembly->reassembly_mode > 3) {
        result->flags |= DM_REMAP_V4_INVALID_REASSEMBLY;
        return -EINVAL;
    }
    
    /* Check validation level is within valid range */
    if (reassembly->validation_level > (DM_REMAP_V4_VALIDATION_MINIMAL |
                                       DM_REMAP_V4_VALIDATION_STANDARD |
                                       DM_REMAP_V4_VALIDATION_STRICT |
                                       DM_REMAP_V4_VALIDATION_PARANOID)) {
        result->flags |= DM_REMAP_V4_INVALID_REASSEMBLY;
        return -EINVAL;
    }
    
    return 0;
}

/*
 * Check internal consistency of metadata structures
 */
int dm_remap_v4_check_consistency(const struct dm_remap_v4_metadata *metadata,
                                 struct dm_remap_v4_validation_result *result)
{
    uint32_t i, j;
    uint64_t total_target_size = 0;
    uint64_t total_spare_size = 0;
    
    if (!metadata || !result)
        return -EINVAL;
    
    /* Check that target count matches actual targets */
    for (i = 0; i < metadata->header.num_targets; i++) {
        const struct dm_remap_v4_target_config *target = &metadata->targets[i];
        
        if (target->length == 0 && i < metadata->header.num_targets) {
            result->flags |= DM_REMAP_V4_CONSISTENCY_ERROR;
            return -EINVAL;
        }
        
        total_target_size += target->length;
    }
    
    /* Check that spare count matches actual spares */
    for (i = 0; i < metadata->header.num_spares; i++) {
        const struct dm_remap_v4_spare_device_info *spare = &metadata->spares[i];
        
        if (spare->device_size == 0 && i < metadata->header.num_spares) {
            result->flags |= DM_REMAP_V4_CONSISTENCY_ERROR;
            return -EINVAL;
        }
        
        total_spare_size += spare->device_size;
    }
    
    /* Check that we have enough spare space for the targets */
    if (total_spare_size < total_target_size) {
        result->flags |= DM_REMAP_V4_CONSISTENCY_ERROR;
        return -EINVAL;
    }
    
    return 0;
}

EXPORT_SYMBOL(dm_remap_v4_validation_flags_to_string);
EXPORT_SYMBOL(dm_remap_v4_validation_level_to_string);
EXPORT_SYMBOL(dm_remap_v4_is_repairable);
EXPORT_SYMBOL(dm_remap_v4_generate_recovery_suggestions);
EXPORT_SYMBOL(dm_remap_v4_auto_repair);
EXPORT_SYMBOL(dm_remap_v4_find_best_match);
EXPORT_SYMBOL(dm_remap_v4_validate_reassembly);
EXPORT_SYMBOL(dm_remap_v4_check_consistency);