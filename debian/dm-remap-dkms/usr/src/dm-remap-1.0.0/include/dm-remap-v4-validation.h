/*
 * dm-remap v4.0 Metadata Validation Engine
 * 
 * Comprehensive validation system for v4.0 metadata structures.
 * Provides multi-level validation with fuzzy device matching and
 * intelligent error recovery suggestions.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#ifndef DM_REMAP_V4_VALIDATION_H
#define DM_REMAP_V4_VALIDATION_H

#include <linux/types.h>
#include <linux/device-mapper.h>
#include "dm-remap-v4-metadata.h"

/* Validation levels for flexible validation control */
#define DM_REMAP_V4_VALIDATION_MINIMAL     0x01  /* Basic structure validation */
#define DM_REMAP_V4_VALIDATION_STANDARD    0x02  /* Standard validation with device checks */
#define DM_REMAP_V4_VALIDATION_STRICT      0x04  /* Strict validation with full integrity checks */
#define DM_REMAP_V4_VALIDATION_PARANOID    0x08  /* Paranoid validation with deep consistency checks */

/* Validation result flags */
#define DM_REMAP_V4_VALID                  0x00000000  /* Metadata is completely valid */
#define DM_REMAP_V4_INVALID_MAGIC          0x00000001  /* Invalid magic number */
#define DM_REMAP_V4_INVALID_VERSION        0x00000002  /* Unsupported version */
#define DM_REMAP_V4_INVALID_SIZE           0x00000004  /* Invalid metadata size */
#define DM_REMAP_V4_INVALID_CHECKSUM       0x00000008  /* CRC32 checksum mismatch */
#define DM_REMAP_V4_INVALID_SEQUENCE       0x00000010  /* Invalid sequence number */
#define DM_REMAP_V4_INVALID_TIMESTAMP      0x00000020  /* Invalid or future timestamp */
#define DM_REMAP_V4_INVALID_TARGETS        0x00000040  /* Invalid target configuration */
#define DM_REMAP_V4_INVALID_SPARES         0x00000080  /* Invalid spare device info */
#define DM_REMAP_V4_INVALID_REASSEMBLY     0x00000100  /* Invalid reassembly instructions */
#define DM_REMAP_V4_DEVICE_MISMATCH        0x00000200  /* Device fingerprint mismatch */
#define DM_REMAP_V4_PARTIAL_MATCH          0x00000400  /* Partial device match (fuzzy matching) */
#define DM_REMAP_V4_SIZE_MISMATCH          0x00000800  /* Device size changed */
#define DM_REMAP_V4_PATH_CHANGED           0x00001000  /* Device path changed */
#define DM_REMAP_V4_SERIAL_CHANGED         0x00002000  /* Device serial changed */
#define DM_REMAP_V4_CONSISTENCY_ERROR      0x00004000  /* Internal consistency error */
#define DM_REMAP_V4_RECOVERY_POSSIBLE      0x80000000  /* Recovery might be possible */

/* Device matching confidence levels */
#define DM_REMAP_V4_MATCH_PERFECT          100  /* Perfect match on all criteria */
#define DM_REMAP_V4_MATCH_HIGH             80   /* High confidence match */
#define DM_REMAP_V4_MATCH_MEDIUM           60   /* Medium confidence match */
#define DM_REMAP_V4_MATCH_LOW              40   /* Low confidence match */
#define DM_REMAP_V4_MATCH_POOR             20   /* Poor match, probably wrong device */
#define DM_REMAP_V4_MATCH_NONE             0    /* No match */

/* Maximum error message length */
#define DM_REMAP_V4_MAX_ERROR_MSG          512

/*
 * Validation result structure
 * Contains detailed validation results and error information
 */
struct dm_remap_v4_validation_result {
    uint32_t flags;                                    /* Validation result flags */
    uint32_t error_count;                              /* Number of errors found */
    uint32_t warning_count;                            /* Number of warnings found */
    uint32_t validation_level;                         /* Level used for validation */
    uint64_t validation_time;                          /* Time when validation was performed */
    char error_messages[DM_REMAP_V4_MAX_ERROR_MSG];    /* Detailed error messages */
    char recovery_suggestions[DM_REMAP_V4_MAX_ERROR_MSG]; /* Recovery suggestions */
};

/*
 * Device match result structure
 * Contains results of device fingerprint matching
 */
struct dm_remap_v4_device_match {
    uint32_t confidence;                               /* Match confidence (0-100) */
    uint32_t match_flags;                              /* What matched/didn't match */
    char matched_device_path[256];                     /* Path of matched device */
    struct dm_remap_v4_device_fingerprint fingerprint; /* Current device fingerprint */
    char notes[256];                                   /* Additional matching notes */
};

/*
 * Validation context structure
 * Provides context and configuration for validation operations
 */
struct dm_remap_v4_validation_context {
    uint32_t validation_level;                         /* Requested validation level */
    uint32_t options;                                  /* Validation options */
    uint64_t current_time;                             /* Current system time */
    struct dm_dev **available_devices;                 /* Available devices for matching */
    uint32_t num_devices;                              /* Number of available devices */
    bool allow_fuzzy_matching;                         /* Enable fuzzy device matching */
    bool strict_size_checking;                         /* Enable strict size checking */
    bool require_exact_paths;                          /* Require exact device paths */
};

/* Validation option flags */
#define DM_REMAP_V4_VALIDATE_IGNORE_TIME      0x01    /* Ignore timestamp validation */
#define DM_REMAP_V4_VALIDATE_IGNORE_SEQUENCE  0x02    /* Ignore sequence number validation */
#define DM_REMAP_V4_VALIDATE_ALLOW_PARTIAL    0x04    /* Allow partial device matches */
#define DM_REMAP_V4_VALIDATE_SUGGEST_RECOVERY 0x08    /* Generate recovery suggestions */

/*
 * Core validation functions
 */

/* Primary validation function - validates complete metadata structure */
int dm_remap_v4_validate_metadata_comprehensive(
    const struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_validation_result *result
);

/* Structural validation - validates basic structure integrity */
int dm_remap_v4_validate_structure(
    const struct dm_remap_v4_metadata *metadata,
    struct dm_remap_v4_validation_result *result
);

/* Header validation - validates metadata header fields */
int dm_remap_v4_validate_header(
    const struct dm_remap_v4_metadata_header *header,
    uint32_t validation_level,
    struct dm_remap_v4_validation_result *result
);

/* Target configuration validation */
int dm_remap_v4_validate_targets(
    const struct dm_remap_v4_target_config *targets,
    uint32_t num_targets,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_validation_result *result
);

/* Spare device validation */
int dm_remap_v4_validate_spares(
    const struct dm_remap_v4_spare_device_info *spares,
    uint32_t num_spares,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_validation_result *result
);

/* Reassembly instruction validation */
int dm_remap_v4_validate_reassembly(
    const struct dm_remap_v4_reassembly_instructions *reassembly,
    struct dm_remap_v4_validation_result *result
);

/*
 * Device matching and fingerprinting functions
 */

/* Match device fingerprint against available devices */
int dm_remap_v4_match_device(
    const struct dm_remap_v4_device_fingerprint *fingerprint,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_device_match *match
);

/* Fuzzy device matching with confidence scoring */
int dm_remap_v4_fuzzy_match_device(
    const struct dm_remap_v4_device_fingerprint *fingerprint,
    struct dm_dev *candidate_device,
    struct dm_remap_v4_device_match *match
);

/* Calculate match confidence between two fingerprints */
uint32_t dm_remap_v4_calculate_match_confidence(
    const struct dm_remap_v4_device_fingerprint *expected,
    const struct dm_remap_v4_device_fingerprint *actual
);

/* Find best matching device from available devices */
struct dm_dev *dm_remap_v4_find_best_match(
    const struct dm_remap_v4_device_fingerprint *fingerprint,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_device_match *best_match
);

/*
 * Integrity and consistency checking functions
 */

/* Verify CRC32 checksums throughout the metadata */
int dm_remap_v4_verify_integrity(
    const struct dm_remap_v4_metadata *metadata,
    struct dm_remap_v4_validation_result *result
);

/* Check internal consistency of metadata structures */
int dm_remap_v4_check_consistency(
    const struct dm_remap_v4_metadata *metadata,
    struct dm_remap_v4_validation_result *result
);

/* Validate metadata against current system state */
int dm_remap_v4_validate_against_system(
    const struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_validation_result *result
);

/*
 * Error recovery and suggestion functions
 */

/* Generate recovery suggestions based on validation results */
int dm_remap_v4_generate_recovery_suggestions(
    const struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_validation_result *result,
    char *suggestions,
    size_t suggestions_len
);

/* Check if metadata can be automatically repaired */
bool dm_remap_v4_is_repairable(
    const struct dm_remap_v4_validation_result *result
);

/* Attempt automatic repair of minor metadata issues */
int dm_remap_v4_auto_repair(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_validation_context *context,
    struct dm_remap_v4_validation_result *result
);

/*
 * Utility functions
 */

/* Initialize validation context with default values */
void dm_remap_v4_init_validation_context(
    struct dm_remap_v4_validation_context *context
);

/* Initialize validation result structure */
void dm_remap_v4_init_validation_result(
    struct dm_remap_v4_validation_result *result
);

/* Convert validation flags to human-readable string */
const char *dm_remap_v4_validation_flags_to_string(uint32_t flags);

/* Get validation level name as string */
const char *dm_remap_v4_validation_level_to_string(uint32_t level);

/* Check if validation result indicates success */
static inline bool dm_remap_v4_validation_successful(
    const struct dm_remap_v4_validation_result *result)
{
    return (result->flags == DM_REMAP_V4_VALID) && (result->error_count == 0);
}

/* Check if validation found warnings */
static inline bool dm_remap_v4_validation_has_warnings(
    const struct dm_remap_v4_validation_result *result)
{
    return result->warning_count > 0;
}

/* Check if recovery is possible */
static inline bool dm_remap_v4_recovery_possible(
    const struct dm_remap_v4_validation_result *result)
{
    return (result->flags & DM_REMAP_V4_RECOVERY_POSSIBLE) != 0;
}

#endif /* DM_REMAP_V4_VALIDATION_H */