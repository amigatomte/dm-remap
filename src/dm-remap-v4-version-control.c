/*
 * dm-remap v4.0 Version Control and Conflict Resolution System
 * Core Implementation
 * 
 * Implements advanced version control with monotonic versioning,
 * timestamp-based conflict resolution, automatic migration, and
 * multi-copy synchronization for dm-remap v4.0 metadata.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#define DM_MSG_PREFIX "dm-remap-v4-version-control"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/crc32.h>
#include <linux/device-mapper.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include "../include/dm-remap-v4-version-control.h"
#include "../include/dm-remap-v4-metadata.h"
#include "../include/dm-remap-v4-validation.h"

/* Global version control state */
static atomic_t global_version_counter = ATOMIC_INIT(1);
static atomic_t global_sequence_counter = ATOMIC_INIT(1);
static DEFINE_SPINLOCK(version_control_lock);

/*
 * Initialize version control system
 */
int dm_remap_v4_vc_init(struct dm_remap_v4_version_context *context)
{
    if (!context) {
        return -EINVAL;
    }
    
    /* Initialize context with default values */
    dm_remap_v4_vc_init_context(context);
    
    /* Set current system time */
    context->current_time = ktime_get_real_seconds();
    
    DMINFO("dm-remap v4.0 version control system initialized");
    return 0;
}

/*
 * Create new version of metadata
 */
int dm_remap_v4_vc_create_version(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_version_context *context,
    uint32_t *version_number)
{
    struct dm_remap_v4_version_header *vc_header;
    uint64_t current_time;
    uint32_t new_version;
    uint32_t new_sequence;
    int i;
    
    if (!metadata || !context || !version_number) {
        return -EINVAL;
    }
    
    /* Generate new version and sequence numbers */
    new_version = dm_remap_v4_vc_generate_version_number(context);
    new_sequence = dm_remap_v4_vc_generate_sequence_number(context);
    current_time = ktime_get_real_seconds();
    
    /* Initialize version control header */
    vc_header = &metadata->version_header;
    memset(vc_header, 0, sizeof(*vc_header));
    
    vc_header->magic = DM_REMAP_V4_VERSION_CONTROL_MAGIC;
    vc_header->version_number = new_version;
    vc_header->creation_timestamp = current_time;
    vc_header->modification_timestamp = current_time;
    vc_header->sequence_number = new_sequence;
    vc_header->parent_version = 0; /* No parent for new version */
    vc_header->conflict_count = 0;
    vc_header->operation_type = DM_REMAP_V4_VC_OP_CREATE;
    
    /* Initialize version chain */
    vc_header->chain_length = 1;
    vc_header->chain_versions[0] = new_version;
    for (i = 1; i < DM_REMAP_V4_VERSION_CHAIN_DEPTH; i++) {
        vc_header->chain_versions[i] = 0;
    }
    
    /* Initialize copy tracking */
    vc_header->copy_count = 1;
    vc_header->copy_timestamps[0] = current_time;
    vc_header->copy_versions[0] = new_version;
    for (i = 1; i < DM_REMAP_V4_MAX_VERSION_COPIES; i++) {
        vc_header->copy_timestamps[i] = 0;
        vc_header->copy_versions[i] = 0;
    }
    
    /* Initialize conflict resolution */
    vc_header->resolution_strategy = context->resolution_strategy;
    vc_header->conflict_timestamp = 0;
    for (i = 0; i < 4; i++) {
        vc_header->conflicting_versions[i] = 0;
    }
    
    /* Calculate header checksum (excluding the CRC field itself) */
    vc_header->header_crc32 = 0;
    vc_header->header_crc32 = crc32(0, (unsigned char *)vc_header, 
                                   sizeof(*vc_header) - sizeof(vc_header->header_crc32));
    
    *version_number = new_version;
    
    DMINFO("Created new metadata version %u (sequence %u)", new_version, new_sequence);
    return 0;
}

/*
 * Update existing metadata version
 */
int dm_remap_v4_vc_update_version(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_version_context *context)
{
    struct dm_remap_v4_version_header *vc_header;
    uint64_t current_time;
    uint32_t old_version;
    int i;
    
    if (!metadata || !context) {
        return -EINVAL;
    }
    
    vc_header = &metadata->version_header;
    
    /* Verify version control header */
    if (vc_header->magic != DM_REMAP_V4_VERSION_CONTROL_MAGIC) {
        DMERR("Invalid version control magic in metadata");
        return -EINVAL;
    }
    
    /* Store old version for parent tracking */
    old_version = vc_header->version_number;
    current_time = ktime_get_real_seconds();
    
    /* Update version information */
    vc_header->parent_version = old_version;
    vc_header->version_number = dm_remap_v4_vc_generate_version_number(context);
    vc_header->modification_timestamp = current_time;
    vc_header->sequence_number = dm_remap_v4_vc_generate_sequence_number(context);
    vc_header->operation_type = DM_REMAP_V4_VC_OP_UPDATE;
    
    /* Update version chain */
    if (vc_header->chain_length < DM_REMAP_V4_VERSION_CHAIN_DEPTH) {
        /* Shift chain and add new version */
        for (i = vc_header->chain_length; i > 0; i--) {
            vc_header->chain_versions[i] = vc_header->chain_versions[i-1];
        }
        vc_header->chain_versions[0] = vc_header->version_number;
        vc_header->chain_length++;
    } else {
        /* Chain full, shift and replace oldest */
        for (i = DM_REMAP_V4_VERSION_CHAIN_DEPTH - 1; i > 0; i--) {
            vc_header->chain_versions[i] = vc_header->chain_versions[i-1];
        }
        vc_header->chain_versions[0] = vc_header->version_number;
    }
    
    /* Update copy information for primary copy */
    vc_header->copy_timestamps[0] = current_time;
    vc_header->copy_versions[0] = vc_header->version_number;
    
    /* Recalculate header checksum */
    vc_header->header_crc32 = 0;
    vc_header->header_crc32 = crc32(0, (unsigned char *)vc_header, 
                                   sizeof(*vc_header) - sizeof(vc_header->header_crc32));
    
    DMINFO("Updated metadata from version %u to %u", old_version, vc_header->version_number);
    return 0;
}

/*
 * Detect version conflicts across multiple copies
 */
int dm_remap_v4_vc_detect_conflicts(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context,
    struct dm_remap_v4_version_conflict *conflicts,
    uint32_t *num_conflicts)
{
    uint32_t conflict_count = 0;
    uint64_t current_time;
    int i, j, k;
    
    if (!metadata_copies || !context || !conflicts || !num_conflicts || num_copies < 2) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    *num_conflicts = 0;
    
    /* Compare each pair of metadata copies */
    for (i = 0; i < num_copies - 1; i++) {
        for (j = i + 1; j < num_copies; j++) {
            struct dm_remap_v4_version_header *header_i, *header_j;
            struct dm_remap_v4_version_conflict *conflict;
            
            if (!metadata_copies[i] || !metadata_copies[j]) {
                continue;
            }
            
            header_i = &metadata_copies[i]->version_header;
            header_j = &metadata_copies[j]->version_header;
            
            /* Check for version conflicts */
            bool has_conflict = false;
            
            /* Different versions with overlapping timestamps */
            if (header_i->version_number != header_j->version_number &&
                dm_remap_v4_vc_within_conflict_window(
                    header_i->modification_timestamp, 
                    header_j->modification_timestamp)) {
                has_conflict = true;
            }
            
            /* Same version with different modification timestamps */
            if (header_i->version_number == header_j->version_number &&
                header_i->modification_timestamp != header_j->modification_timestamp) {
                has_conflict = true;
            }
            
            /* Different sequence numbers for same version */
            if (header_i->version_number == header_j->version_number &&
                header_i->sequence_number != header_j->sequence_number) {
                has_conflict = true;
            }
            
            if (has_conflict && conflict_count < 8) {
                conflict = &conflicts[conflict_count];
                memset(conflict, 0, sizeof(*conflict));
                
                conflict->conflict_id = conflict_count + 1;
                conflict->num_versions = 2;
                conflict->version_numbers[0] = header_i->version_number;
                conflict->version_numbers[1] = header_j->version_number;
                conflict->timestamps[0] = header_i->modification_timestamp;
                conflict->timestamps[1] = header_j->modification_timestamp;
                conflict->sequence_numbers[0] = header_i->sequence_number;
                conflict->sequence_numbers[1] = header_j->sequence_number;
                
                /* Determine conflict type */
                if (header_i->version_number != header_j->version_number) {
                    conflict->conflict_type = 0x01; /* Version number conflict */
                } else if (header_i->sequence_number != header_j->sequence_number) {
                    conflict->conflict_type = 0x02; /* Sequence number conflict */
                } else {
                    conflict->conflict_type = 0x04; /* Timestamp conflict */
                }
                
                /* Assess severity */
                uint64_t time_diff = (header_i->modification_timestamp > header_j->modification_timestamp) ?
                                    (header_i->modification_timestamp - header_j->modification_timestamp) :
                                    (header_j->modification_timestamp - header_i->modification_timestamp);
                
                if (time_diff < 1000) {
                    conflict->severity = 3; /* High severity - very close in time */
                } else if (time_diff < 5000) {
                    conflict->severity = 2; /* Medium severity */
                } else {
                    conflict->severity = 1; /* Low severity */
                }
                
                /* Recommend resolution strategy */
                if (context->resolution_strategy & DM_REMAP_V4_RESOLVE_TIMESTAMP) {
                    conflict->recommended_strategy = DM_REMAP_V4_RESOLVE_TIMESTAMP;
                } else if (context->resolution_strategy & DM_REMAP_V4_RESOLVE_SEQUENCE) {
                    conflict->recommended_strategy = DM_REMAP_V4_RESOLVE_SEQUENCE;
                } else {
                    conflict->recommended_strategy = DM_REMAP_V4_RESOLVE_MANUAL;
                }
                
                conflict->detection_time = current_time;
                conflict->resolution_status = 0; /* Unresolved */
                
                snprintf(conflict->resolution_notes, sizeof(conflict->resolution_notes),
                        "Conflict between copies %d and %d: versions %u vs %u, time diff %llu seconds",
                        i, j, header_i->version_number, header_j->version_number, time_diff);
                
                conflict_count++;
            }
        }
    }
    
    *num_conflicts = conflict_count;
    
    if (conflict_count > 0) {
        DMWARN("Detected %u version conflicts across %u metadata copies", 
               conflict_count, num_copies);
    }
    
    return 0;
}

/*
 * Resolve detected version conflicts
 */
int dm_remap_v4_vc_resolve_conflict(
    const struct dm_remap_v4_version_conflict *conflict,
    struct dm_remap_v4_metadata **metadata_copies,
    const struct dm_remap_v4_version_context *context,
    struct dm_remap_v4_metadata *resolved_metadata)
{
    uint32_t chosen_version_idx = 0;
    uint64_t current_time;
    int i;
    
    if (!conflict || !metadata_copies || !context || !resolved_metadata) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    
    /* Apply resolution strategy */
    switch (conflict->recommended_strategy) {
    case DM_REMAP_V4_RESOLVE_TIMESTAMP:
        /* Choose version with newest timestamp */
        for (i = 1; i < conflict->num_versions; i++) {
            if (conflict->timestamps[i] > conflict->timestamps[chosen_version_idx]) {
                chosen_version_idx = i;
            }
        }
        break;
        
    case DM_REMAP_V4_RESOLVE_SEQUENCE:
        /* Choose version with highest sequence number */
        for (i = 1; i < conflict->num_versions; i++) {
            if (conflict->sequence_numbers[i] > conflict->sequence_numbers[chosen_version_idx]) {
                chosen_version_idx = i;
            }
        }
        break;
        
    case DM_REMAP_V4_RESOLVE_CONSERVATIVE:
        /* Choose version with oldest timestamp (most conservative) */
        for (i = 1; i < conflict->num_versions; i++) {
            if (conflict->timestamps[i] < conflict->timestamps[chosen_version_idx]) {
                chosen_version_idx = i;
            }
        }
        break;
        
    default:
        DMERR("Unsupported conflict resolution strategy: %u", conflict->recommended_strategy);
        return -EINVAL;
    }
    
    /* Copy chosen version to resolved metadata */
    if (chosen_version_idx < conflict->num_versions) {
        /* Find the metadata copy with the chosen version */
        for (i = 0; i < DM_REMAP_V4_MAX_VERSION_COPIES; i++) {
            if (metadata_copies[i] && 
                metadata_copies[i]->version_header.version_number == 
                conflict->version_numbers[chosen_version_idx]) {
                
                memcpy(resolved_metadata, metadata_copies[i], sizeof(*resolved_metadata));
                
                /* Update conflict resolution information */
                resolved_metadata->version_header.conflict_count++;
                resolved_metadata->version_header.conflict_timestamp = current_time;
                resolved_metadata->version_header.operation_type = DM_REMAP_V4_VC_OP_MERGE;
                
                /* Store conflicting versions for audit trail */
                for (int j = 0; j < 4 && j < conflict->num_versions; j++) {
                    resolved_metadata->version_header.conflicting_versions[j] = 
                        conflict->version_numbers[j];
                }
                
                /* Recalculate header checksum */
                resolved_metadata->version_header.header_crc32 = 0;
                resolved_metadata->version_header.header_crc32 = 
                    crc32(0, (unsigned char *)&resolved_metadata->version_header, 
                         sizeof(resolved_metadata->version_header) - 
                         sizeof(resolved_metadata->version_header.header_crc32));
                
                DMINFO("Resolved conflict %u: chose version %u (strategy: %s)",
                       conflict->conflict_id, conflict->version_numbers[chosen_version_idx],
                       dm_remap_v4_vc_strategy_to_string(conflict->recommended_strategy));
                
                return 0;
            }
        }
    }
    
    DMERR("Failed to find chosen version %u for conflict resolution", 
          conflict->version_numbers[chosen_version_idx]);
    return -ENOENT;
}

/*
 * Check version compatibility
 */
int dm_remap_v4_vc_check_compatibility(
    uint32_t version_a,
    uint32_t version_b,
    uint32_t *compatibility_level)
{
    uint32_t version_diff;
    
    if (!compatibility_level) {
        return -EINVAL;
    }
    
    /* Calculate version difference */
    version_diff = (version_a > version_b) ? (version_a - version_b) : (version_b - version_a);
    
    /* Determine compatibility level */
    if (version_diff == 0) {
        *compatibility_level = 100; /* Identical versions */
    } else if (version_diff <= 5) {
        *compatibility_level = 90;  /* Highly compatible */
    } else if (version_diff <= 20) {
        *compatibility_level = 75;  /* Compatible with minor differences */
    } else if (version_diff <= 50) {
        *compatibility_level = 50;  /* Moderately compatible */
    } else if (version_diff <= 100) {
        *compatibility_level = 25;  /* Low compatibility */
    } else {
        *compatibility_level = 0;   /* Incompatible */
    }
    
    return 0;
}

/*
 * Synchronize multiple metadata copies
 */
int dm_remap_v4_vc_synchronize_copies(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context)
{
    uint32_t authoritative_copy = 0;
    uint64_t newest_timestamp = 0;
    uint64_t current_time;
    int i, j;
    
    if (!metadata_copies || !context || num_copies < 2) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    
    /* Find the most recent copy as authoritative */
    for (i = 0; i < num_copies; i++) {
        if (metadata_copies[i] && 
            metadata_copies[i]->version_header.modification_timestamp > newest_timestamp) {
            newest_timestamp = metadata_copies[i]->version_header.modification_timestamp;
            authoritative_copy = i;
        }
    }
    
    /* Synchronize all copies to the authoritative version */
    for (i = 0; i < num_copies; i++) {
        if (i != authoritative_copy && metadata_copies[i]) {
            /* Copy from authoritative version */
            memcpy(metadata_copies[i], metadata_copies[authoritative_copy], 
                   sizeof(*metadata_copies[i]));
            
            /* Update copy-specific information */
            metadata_copies[i]->version_header.copy_timestamps[i] = current_time;
            metadata_copies[i]->version_header.copy_versions[i] = 
                metadata_copies[i]->version_header.version_number;
            metadata_copies[i]->version_header.operation_type = DM_REMAP_V4_VC_OP_SYNCHRONIZE;
            
            /* Recalculate checksum */
            metadata_copies[i]->version_header.header_crc32 = 0;
            metadata_copies[i]->version_header.header_crc32 = 
                crc32(0, (unsigned char *)&metadata_copies[i]->version_header, 
                     sizeof(metadata_copies[i]->version_header) - 
                     sizeof(metadata_copies[i]->version_header.header_crc32));
        }
    }
    
    DMINFO("Synchronized %u metadata copies to version %u", 
           num_copies, metadata_copies[authoritative_copy]->version_header.version_number);
    
    return 0;
}

/*
 * Utility functions
 */

/* Initialize version control context */
void dm_remap_v4_vc_init_context(struct dm_remap_v4_version_context *context)
{
    if (!context) {
        return;
    }
    
    memset(context, 0, sizeof(*context));
    
    /* Set default values */
    context->resolution_strategy = DM_REMAP_V4_RESOLVE_TIMESTAMP;
    context->max_copies = 4;
    context->sync_threshold = 1000; /* 1 second */
    context->auto_migrate = true;
    context->conservative_merge = false;
    context->require_consensus = false;
    context->backup_before_merge = true;
    context->max_chain_length = DM_REMAP_V4_VERSION_CHAIN_DEPTH;
    context->cleanup_threshold = 100; /* Keep 100 old versions */
    context->validation_level = DM_REMAP_V4_VALIDATION_STANDARD;
}

/* Generate monotonic version number */
uint32_t dm_remap_v4_vc_generate_version_number(
    const struct dm_remap_v4_version_context *context)
{
    return atomic_inc_return(&global_version_counter);
}

/* Generate global sequence number */
uint32_t dm_remap_v4_vc_generate_sequence_number(
    const struct dm_remap_v4_version_context *context)
{
    return atomic_inc_return(&global_sequence_counter);
}

/* Compare version timestamps for conflict resolution */
int dm_remap_v4_vc_compare_timestamps(
    uint64_t timestamp_a,
    uint64_t timestamp_b,
    uint32_t threshold_ms)
{
    uint64_t diff = (timestamp_a > timestamp_b) ? 
                    (timestamp_a - timestamp_b) : (timestamp_b - timestamp_a);
    
    if (diff <= threshold_ms) {
        return 0; /* Within threshold - conflict */
    } else if (timestamp_a > timestamp_b) {
        return 1; /* A is newer */
    } else {
        return -1; /* B is newer */
    }
}

/* Convert version control status to string */
const char *dm_remap_v4_vc_status_to_string(uint32_t status)
{
    if (status == DM_REMAP_V4_VC_STATUS_CLEAN) return "Clean";
    if (status & DM_REMAP_V4_VC_STATUS_CONFLICT) return "Conflict";
    if (status & DM_REMAP_V4_VC_STATUS_DIRTY) return "Dirty";
    if (status & DM_REMAP_V4_VC_STATUS_MIGRATING) return "Migrating";
    if (status & DM_REMAP_V4_VC_STATUS_SYNCING) return "Syncing";
    if (status & DM_REMAP_V4_VC_STATUS_CORRUPTED) return "Corrupted";
    if (status & DM_REMAP_V4_VC_STATUS_INCONSISTENT) return "Inconsistent";
    return "Unknown";
}

/* Convert resolution strategy to string */
const char *dm_remap_v4_vc_strategy_to_string(uint32_t strategy)
{
    switch (strategy) {
    case DM_REMAP_V4_RESOLVE_TIMESTAMP: return "Timestamp-based";
    case DM_REMAP_V4_RESOLVE_SEQUENCE: return "Sequence-based";
    case DM_REMAP_V4_RESOLVE_MANUAL: return "Manual";
    case DM_REMAP_V4_RESOLVE_CONSERVATIVE: return "Conservative";
    case DM_REMAP_V4_RESOLVE_MERGE: return "Automatic merge";
    default: return "Unknown";
    }
}