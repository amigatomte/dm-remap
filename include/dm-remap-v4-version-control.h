/*
 * dm-remap v4.0 Version Control and Conflict Resolution System
 * 
 * Advanced version control system for v4.0 metadata with monotonic versioning,
 * timestamp-based conflict resolution, automatic migration, and multi-copy
 * synchronization capabilities.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#ifndef DM_REMAP_V4_VERSION_CONTROL_H
#define DM_REMAP_V4_VERSION_CONTROL_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/device-mapper.h>
#include "dm-remap-v4-metadata.h"
#include "dm-remap-v4-validation.h"

/* Version control constants */
#define DM_REMAP_V4_VERSION_CONTROL_MAGIC    0x56435254  /* "VCRT" */
#define DM_REMAP_V4_MAX_VERSION_COPIES       8           /* Maximum metadata copies */
#define DM_REMAP_V4_VERSION_CHAIN_DEPTH      16          /* Version history depth */
#define DM_REMAP_V4_CONFLICT_THRESHOLD       5000        /* 5 second conflict window */

/* Version control operation types */
#define DM_REMAP_V4_VC_OP_CREATE            0x01         /* Create new version */
#define DM_REMAP_V4_VC_OP_UPDATE            0x02         /* Update existing version */
#define DM_REMAP_V4_VC_OP_MERGE             0x04         /* Merge conflicting versions */
#define DM_REMAP_V4_VC_OP_MIGRATE           0x08         /* Migrate to new version */
#define DM_REMAP_V4_VC_OP_SYNCHRONIZE       0x10         /* Synchronize multiple copies */

/* Conflict resolution strategies */
#define DM_REMAP_V4_RESOLVE_TIMESTAMP       0x01         /* Use newest timestamp */
#define DM_REMAP_V4_RESOLVE_SEQUENCE        0x02         /* Use highest sequence number */
#define DM_REMAP_V4_RESOLVE_MANUAL          0x04         /* Require manual resolution */
#define DM_REMAP_V4_RESOLVE_CONSERVATIVE    0x08         /* Use most conservative option */
#define DM_REMAP_V4_RESOLVE_MERGE           0x10         /* Attempt automatic merge */

/* Version control status flags */
#define DM_REMAP_V4_VC_STATUS_CLEAN         0x00000000   /* No conflicts or pending changes */
#define DM_REMAP_V4_VC_STATUS_DIRTY         0x00000001   /* Uncommitted changes present */
#define DM_REMAP_V4_VC_STATUS_CONFLICT      0x00000002   /* Conflict detected */
#define DM_REMAP_V4_VC_STATUS_MIGRATING     0x00000004   /* Migration in progress */
#define DM_REMAP_V4_VC_STATUS_SYNCING       0x00000008   /* Synchronization in progress */
#define DM_REMAP_V4_VC_STATUS_CORRUPTED     0x00000010   /* Version corruption detected */
#define DM_REMAP_V4_VC_STATUS_INCONSISTENT  0x00000020   /* Cross-copy inconsistency */
#define DM_REMAP_V4_VC_STATUS_RECOVERABLE   0x80000000   /* Recovery possible */

/*
 * NOTE: struct dm_remap_v4_version_header is defined in dm-remap-v4-metadata.h
 * (included above). It contains versioning metadata for change tracking and
 * conflict resolution.
 */

/*
 * Version control context structure
 * Configuration and state for version control operations
 */
struct dm_remap_v4_version_context {
    uint32_t resolution_strategy;                        /* Default conflict resolution */
    uint32_t max_copies;                                 /* Maximum metadata copies */
    uint32_t sync_threshold;                             /* Synchronization threshold (ms) */
    uint64_t current_time;                               /* Current system time */
    
    /* Device and storage context */
    struct dm_dev **storage_devices;                     /* Available storage devices */
    uint32_t num_devices;                                /* Number of storage devices */
    sector_t *copy_locations;                            /* Metadata copy locations */
    
    /* Operation preferences */
    bool auto_migrate;                                   /* Enable automatic migration */
    bool conservative_merge;                             /* Use conservative merging */
    bool require_consensus;                              /* Require majority consensus */
    bool backup_before_merge;                            /* Backup before conflict resolution */
    
    /* Performance tuning */
    uint32_t max_chain_length;                           /* Maximum version chain length */
    uint32_t cleanup_threshold;                          /* Old version cleanup threshold */
    uint32_t validation_level;                           /* Validation level for operations */
};

/*
 * Version conflict structure
 * Represents a detected version conflict with resolution information
 */
struct dm_remap_v4_version_conflict {
    uint32_t conflict_id;                                /* Unique conflict identifier */
    uint32_t num_versions;                               /* Number of conflicting versions */
    uint32_t version_numbers[8];                         /* Conflicting version numbers */
    uint64_t timestamps[8];                              /* Version timestamps */
    uint32_t sequence_numbers[8];                        /* Version sequence numbers */
    
    /* Conflict analysis */
    uint32_t conflict_type;                              /* Type of conflict detected */
    uint32_t affected_components;                        /* Which components conflict */
    uint32_t severity;                                   /* Conflict severity level */
    uint32_t recommended_strategy;                       /* Recommended resolution strategy */
    
    /* Resolution information */
    uint32_t resolution_status;                          /* Current resolution status */
    uint32_t chosen_version;                             /* Version chosen for resolution */
    char resolution_notes[256];                          /* Resolution notes and reasoning */
    
    uint64_t detection_time;                             /* When conflict was detected */
    uint64_t resolution_time;                            /* When conflict was resolved */
};

/*
 * Version migration plan structure
 * Contains information for migrating between metadata versions
 */
struct dm_remap_v4_migration_plan {
    uint32_t source_version;                             /* Source version number */
    uint32_t target_version;                             /* Target version number */
    uint32_t migration_type;                             /* Type of migration required */
    uint32_t compatibility_level;                        /* Compatibility assessment */
    
    /* Migration steps */
    uint32_t num_steps;                                  /* Number of migration steps */
    uint32_t step_types[16];                             /* Types of migration steps */
    char step_descriptions[16][128];                     /* Step descriptions */
    
    /* Risk assessment */
    uint32_t risk_level;                                 /* Migration risk assessment */
    bool requires_backup;                                /* Backup required before migration */
    bool reversible;                                     /* Migration is reversible */
    uint32_t estimated_time;                             /* Estimated migration time (ms) */
    
    /* Validation requirements */
    uint32_t validation_checkpoints;                     /* Required validation points */
    uint32_t rollback_points;                            /* Available rollback points */
    char risk_notes[256];                                /* Risk assessment notes */
};

/*
 * Core version control functions
 */

/* Initialize version control system */
int dm_remap_v4_vc_init(
    struct dm_remap_v4_version_context *context
);

/* Create new version of metadata */
int dm_remap_v4_vc_create_version(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_version_context *context,
    uint32_t *version_number
);

/* Update existing metadata version */
int dm_remap_v4_vc_update_version(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_version_context *context
);

/* Detect version conflicts across multiple copies */
int dm_remap_v4_vc_detect_conflicts(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context,
    struct dm_remap_v4_version_conflict *conflicts,
    uint32_t *num_conflicts
);

/* Resolve detected version conflicts */
int dm_remap_v4_vc_resolve_conflict(
    const struct dm_remap_v4_version_conflict *conflict,
    struct dm_remap_v4_metadata **metadata_copies,
    const struct dm_remap_v4_version_context *context,
    struct dm_remap_v4_metadata *resolved_metadata
);

/*
 * Migration and compatibility functions
 */

/* Check version compatibility */
int dm_remap_v4_vc_check_compatibility(
    uint32_t version_a,
    uint32_t version_b,
    uint32_t *compatibility_level
);

/* Create migration plan between versions */
int dm_remap_v4_vc_create_migration_plan(
    uint32_t source_version,
    uint32_t target_version,
    const struct dm_remap_v4_version_context *context,
    struct dm_remap_v4_migration_plan *plan
);

/* Execute version migration */
int dm_remap_v4_vc_migrate_version(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_migration_plan *plan,
    const struct dm_remap_v4_version_context *context
);

/* Automatic migration with safety checks */
int dm_remap_v4_vc_auto_migrate(
    struct dm_remap_v4_metadata *metadata,
    uint32_t target_version,
    const struct dm_remap_v4_version_context *context
);

/*
 * Multi-copy synchronization functions
 */

/* Synchronize multiple metadata copies */
int dm_remap_v4_vc_synchronize_copies(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context
);

/* Validate copy consistency */
int dm_remap_v4_vc_validate_copy_consistency(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context,
    uint32_t *inconsistent_copies
);

/* Repair inconsistent copies */
int dm_remap_v4_vc_repair_copies(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    uint32_t authoritative_copy,
    const struct dm_remap_v4_version_context *context
);

/* Create additional metadata copies */
int dm_remap_v4_vc_create_copies(
    const struct dm_remap_v4_metadata *source_metadata,
    struct dm_remap_v4_metadata **target_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context
);

/*
 * Version history and cleanup functions
 */

/* Get version history chain */
int dm_remap_v4_vc_get_version_history(
    const struct dm_remap_v4_metadata *metadata,
    uint32_t *version_chain,
    uint32_t max_versions,
    uint32_t *chain_length
);

/* Cleanup old versions */
int dm_remap_v4_vc_cleanup_old_versions(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context,
    uint32_t *cleaned_versions
);

/* Compact version history */
int dm_remap_v4_vc_compact_history(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_version_context *context
);

/*
 * Utility functions
 */

/* Initialize version control context */
void dm_remap_v4_vc_init_context(
    struct dm_remap_v4_version_context *context
);

/* Generate monotonic version number */
uint32_t dm_remap_v4_vc_generate_version_number(
    const struct dm_remap_v4_version_context *context
);

/* Generate global sequence number */
uint32_t dm_remap_v4_vc_generate_sequence_number(
    const struct dm_remap_v4_version_context *context
);

/* Compare version timestamps for conflict resolution */
int dm_remap_v4_vc_compare_timestamps(
    uint64_t timestamp_a,
    uint64_t timestamp_b,
    uint32_t threshold_ms
);

/* Convert version control status to string */
const char *dm_remap_v4_vc_status_to_string(uint32_t status);

/* Convert conflict type to string */
const char *dm_remap_v4_vc_conflict_type_to_string(uint32_t conflict_type);

/* Convert resolution strategy to string */
const char *dm_remap_v4_vc_strategy_to_string(uint32_t strategy);

/*
 * Inline utility functions
 */

/* Check if version control status indicates conflicts */
static inline bool dm_remap_v4_vc_has_conflicts(uint32_t status)
{
    return (status & DM_REMAP_V4_VC_STATUS_CONFLICT) != 0;
}

/* Check if version control is in clean state */
static inline bool dm_remap_v4_vc_is_clean(uint32_t status)
{
    return status == DM_REMAP_V4_VC_STATUS_CLEAN;
}

/* Check if recovery is possible */
static inline bool dm_remap_v4_vc_is_recoverable(uint32_t status)
{
    return (status & DM_REMAP_V4_VC_STATUS_RECOVERABLE) != 0;
}

/* Check if versions are within conflict threshold */
static inline bool dm_remap_v4_vc_within_conflict_window(
    uint64_t timestamp_a,
    uint64_t timestamp_b)
{
    uint64_t diff = (timestamp_a > timestamp_b) ? 
                    (timestamp_a - timestamp_b) : (timestamp_b - timestamp_a);
    return diff <= DM_REMAP_V4_CONFLICT_THRESHOLD;
}

#endif /* DM_REMAP_V4_VERSION_CONTROL_H */