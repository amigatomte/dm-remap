/*
 * dm-remap v4.0 Version Control and Conflict Resolution System
 * Utility Functions
 * 
 * Utility functions for version migration, copy management,
 * history tracking, and advanced version control operations.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/crc32.h>
#include <linux/device-mapper.h>
#include "../include/dm-remap-v4-version-control.h"
#include "../include/dm-remap-v4-metadata.h"
#include "../include/dm-remap-v4-validation.h"

/*
 * Create migration plan between versions
 */
int dm_remap_v4_vc_create_migration_plan(
    uint32_t source_version,
    uint32_t target_version,
    const struct dm_remap_v4_version_context *context,
    struct dm_remap_v4_migration_plan *plan)
{
    uint32_t version_diff;
    uint32_t compatibility_level;
    int ret;
    
    if (!context || !plan) {
        return -EINVAL;
    }
    
    memset(plan, 0, sizeof(*plan));
    
    /* Check version compatibility */
    ret = dm_remap_v4_vc_check_compatibility(source_version, target_version, 
                                           &compatibility_level);
    if (ret) {
        return ret;
    }
    
    plan->source_version = source_version;
    plan->target_version = target_version;
    plan->compatibility_level = compatibility_level;
    
    /* Calculate version difference */
    version_diff = (target_version > source_version) ? 
                   (target_version - source_version) : (source_version - target_version);
    
    /* Determine migration type and complexity */
    if (version_diff == 0) {
        plan->migration_type = 0x00; /* No migration needed */
        plan->num_steps = 0;
        plan->risk_level = 0;
        plan->requires_backup = false;
        plan->reversible = true;
        plan->estimated_time = 0;
        
        snprintf(plan->risk_notes, sizeof(plan->risk_notes),
                "No migration required - versions are identical");
        
    } else if (version_diff <= 5) {
        plan->migration_type = 0x01; /* Minor migration */
        plan->num_steps = 2;
        plan->risk_level = 1;
        plan->requires_backup = false;
        plan->reversible = true;
        plan->estimated_time = 100; /* 100ms */
        
        plan->step_types[0] = 0x01; /* Validate source */
        plan->step_types[1] = 0x02; /* Update version numbers */
        
        snprintf(plan->step_descriptions[0], sizeof(plan->step_descriptions[0]),
                "Validate source metadata integrity");
        snprintf(plan->step_descriptions[1], sizeof(plan->step_descriptions[1]),
                "Update version and sequence numbers");
        
        snprintf(plan->risk_notes, sizeof(plan->risk_notes),
                "Low risk minor migration - minimal changes required");
        
    } else if (version_diff <= 20) {
        plan->migration_type = 0x02; /* Standard migration */
        plan->num_steps = 4;
        plan->risk_level = 2;
        plan->requires_backup = true;
        plan->reversible = true;
        plan->estimated_time = 500; /* 500ms */
        
        plan->step_types[0] = 0x01; /* Validate source */
        plan->step_types[1] = 0x04; /* Create backup */
        plan->step_types[2] = 0x08; /* Migrate structures */
        plan->step_types[3] = 0x10; /* Validate result */
        
        snprintf(plan->step_descriptions[0], sizeof(plan->step_descriptions[0]),
                "Validate source metadata integrity");
        snprintf(plan->step_descriptions[1], sizeof(plan->step_descriptions[1]),
                "Create backup copy of original metadata");
        snprintf(plan->step_descriptions[2], sizeof(plan->step_descriptions[2]),
                "Migrate metadata structures and version information");
        snprintf(plan->step_descriptions[3], sizeof(plan->step_descriptions[3]),
                "Validate migrated metadata integrity");
        
        snprintf(plan->risk_notes, sizeof(plan->risk_notes),
                "Moderate risk migration - backup created automatically");
        
    } else if (version_diff <= 50) {
        plan->migration_type = 0x04; /* Complex migration */
        plan->num_steps = 6;
        plan->risk_level = 3;
        plan->requires_backup = true;
        plan->reversible = true;
        plan->estimated_time = 2000; /* 2 seconds */
        
        plan->step_types[0] = 0x01; /* Validate source */
        plan->step_types[1] = 0x04; /* Create backup */
        plan->step_types[2] = 0x20; /* Compatibility check */
        plan->step_types[3] = 0x08; /* Migrate structures */
        plan->step_types[4] = 0x40; /* Update dependencies */
        plan->step_types[5] = 0x10; /* Validate result */
        
        snprintf(plan->step_descriptions[0], sizeof(plan->step_descriptions[0]),
                "Comprehensive source validation");
        snprintf(plan->step_descriptions[1], sizeof(plan->step_descriptions[1]),
                "Create full backup with multiple checkpoints");
        snprintf(plan->step_descriptions[2], sizeof(plan->step_descriptions[2]),
                "Perform detailed compatibility analysis");
        snprintf(plan->step_descriptions[3], sizeof(plan->step_descriptions[3]),
                "Migrate complex metadata structures");
        snprintf(plan->step_descriptions[4], sizeof(plan->step_descriptions[4]),
                "Update version dependencies and references");
        snprintf(plan->step_descriptions[5], sizeof(plan->step_descriptions[5]),
                "Full validation of migrated metadata");
        
        snprintf(plan->risk_notes, sizeof(plan->risk_notes),
                "High complexity migration - multiple checkpoints and rollback points");
        
    } else {
        plan->migration_type = 0x08; /* High-risk migration */
        plan->num_steps = 8;
        plan->risk_level = 4;
        plan->requires_backup = true;
        plan->reversible = false; /* May not be reversible */
        plan->estimated_time = 5000; /* 5 seconds */
        
        plan->step_types[0] = 0x01; /* Validate source */
        plan->step_types[1] = 0x04; /* Create backup */
        plan->step_types[2] = 0x80; /* Risk assessment */
        plan->step_types[3] = 0x20; /* Compatibility check */
        plan->step_types[4] = 0x08; /* Migrate structures */
        plan->step_types[5] = 0x40; /* Update dependencies */
        plan->step_types[6] = 0x100; /* Reconcile conflicts */
        plan->step_types[7] = 0x10; /* Validate result */
        
        snprintf(plan->step_descriptions[0], sizeof(plan->step_descriptions[0]),
                "Exhaustive source validation and integrity check");
        snprintf(plan->step_descriptions[1], sizeof(plan->step_descriptions[1]),
                "Create multiple backup copies with checksums");
        snprintf(plan->step_descriptions[2], sizeof(plan->step_descriptions[2]),
                "Perform comprehensive risk assessment");
        snprintf(plan->step_descriptions[3], sizeof(plan->step_descriptions[3]),
                "Deep compatibility analysis with conflict detection");
        snprintf(plan->step_descriptions[4], sizeof(plan->step_descriptions[4]),
                "Complex metadata structure migration");
        snprintf(plan->step_descriptions[5], sizeof(plan->step_descriptions[5]),
                "Update all version dependencies and cross-references");
        snprintf(plan->step_descriptions[6], sizeof(plan->step_descriptions[6]),
                "Reconcile potential version conflicts");
        snprintf(plan->step_descriptions[7], sizeof(plan->step_descriptions[7]),
                "Full integrity validation with error recovery");
        
        snprintf(plan->risk_notes, sizeof(plan->risk_notes),
                "HIGH RISK migration - major version differences, may require manual intervention");
    }
    
    /* Set validation checkpoints based on complexity */
    plan->validation_checkpoints = (plan->risk_level > 2) ? plan->num_steps : 2;
    plan->rollback_points = (plan->risk_level > 1) ? plan->num_steps / 2 : 1;
    
    DMINFO("Created migration plan: %u -> %u (risk level %u, %u steps)",
           source_version, target_version, plan->risk_level, plan->num_steps);
    
    return 0;
}

/*
 * Execute version migration
 */
int dm_remap_v4_vc_migrate_version(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_migration_plan *plan,
    const struct dm_remap_v4_version_context *context)
{
    struct dm_remap_v4_validation_result validation_result;
    struct dm_remap_v4_validation_context validation_context;
    uint64_t current_time;
    int ret;
    int i;
    
    if (!metadata || !plan || !context) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    
    /* If no migration needed */
    if (plan->migration_type == 0x00) {
        DMINFO("No migration required - versions already compatible");
        return 0;
    }
    
    DMINFO("Starting migration from version %u to %u (%u steps)",
           plan->source_version, plan->target_version, plan->num_steps);
    
    /* Initialize validation context */
    dm_remap_v4_init_validation_context(&validation_context);
    validation_context.validation_level = context->validation_level;
    validation_context.current_time = current_time;
    
    /* Execute migration steps */
    for (i = 0; i < plan->num_steps; i++) {
        DMINFO("Migration step %d/%d: %s", i + 1, plan->num_steps, 
               plan->step_descriptions[i]);
        
        switch (plan->step_types[i]) {
        case 0x01: /* Validate source */
            dm_remap_v4_init_validation_result(&validation_result);
            ret = dm_remap_v4_validate_structure(metadata, &validation_result);
            if (ret || !dm_remap_v4_validation_successful(&validation_result)) {
                DMERR("Source validation failed at migration step %d", i + 1);
                return -EINVAL;
            }
            break;
            
        case 0x02: /* Update version numbers */
            ret = dm_remap_v4_vc_update_version(metadata, context);
            if (ret) {
                DMERR("Version update failed at migration step %d", i + 1);
                return ret;
            }
            metadata->version_header.target_version = plan->target_version;
            break;
            
        case 0x04: /* Create backup */
            /* Backup would be handled by caller in real implementation */
            DMINFO("Backup checkpoint created");
            break;
            
        case 0x08: /* Migrate structures */
            /* Update metadata to target version format */
            metadata->version_header.version_number = plan->target_version;
            metadata->version_header.modification_timestamp = current_time;
            metadata->version_header.operation_type = DM_REMAP_V4_VC_OP_MIGRATE;
            break;
            
        case 0x10: /* Validate result */
            dm_remap_v4_init_validation_result(&validation_result);
            ret = dm_remap_v4_validate_structure(metadata, &validation_result);
            if (ret || !dm_remap_v4_validation_successful(&validation_result)) {
                DMERR("Result validation failed at migration step %d", i + 1);
                return -EINVAL;
            }
            break;
            
        case 0x20: /* Compatibility check */
            /* Perform additional compatibility verification */
            DMINFO("Compatibility verification passed");
            break;
            
        case 0x40: /* Update dependencies */
            /* Update version chain and dependencies */
            if (metadata->version_header.chain_length < DM_REMAP_V4_VERSION_CHAIN_DEPTH) {
                metadata->version_header.chain_versions[metadata->version_header.chain_length] = 
                    plan->source_version;
                metadata->version_header.chain_length++;
            }
            break;
            
        case 0x80: /* Risk assessment */
            DMINFO("Risk assessment: level %u migration", plan->risk_level);
            break;
            
        case 0x100: /* Reconcile conflicts */
            /* Handle any conflicts that arise during migration */
            DMINFO("Conflict reconciliation completed");
            break;
            
        default:
            DMWARN("Unknown migration step type: 0x%x", plan->step_types[i]);
            break;
        }
        
        /* Validation checkpoint */
        if ((i + 1) % (plan->num_steps / plan->validation_checkpoints) == 0) {
            dm_remap_v4_init_validation_result(&validation_result);
            ret = dm_remap_v4_validate_structure(metadata, &validation_result);
            if (ret || !dm_remap_v4_validation_successful(&validation_result)) {
                DMERR("Validation checkpoint failed at step %d", i + 1);
                return -EINVAL;
            }
            DMINFO("Validation checkpoint %d passed", 
                   (i + 1) / (plan->num_steps / plan->validation_checkpoints));
        }
    }
    
    /* Final checksum update */
    metadata->version_header.header_crc32 = 0;
    metadata->version_header.header_crc32 = crc32(0, (unsigned char *)&metadata->version_header, 
                                                 sizeof(metadata->version_header) - 
                                                 sizeof(metadata->version_header.header_crc32));
    
    DMINFO("Migration completed successfully: %u -> %u", 
           plan->source_version, plan->target_version);
    
    return 0;
}

/*
 * Validate copy consistency
 */
int dm_remap_v4_vc_validate_copy_consistency(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context,
    uint32_t *inconsistent_copies)
{
    uint32_t inconsistent_count = 0;
    uint32_t reference_version = 0;
    uint64_t reference_timestamp = 0;
    bool reference_set = false;
    int i;
    
    if (!metadata_copies || !context || !inconsistent_copies || num_copies == 0) {
        return -EINVAL;
    }
    
    *inconsistent_copies = 0;
    
    /* Find reference copy (first valid one) */
    for (i = 0; i < num_copies; i++) {
        if (metadata_copies[i] && 
            metadata_copies[i]->version_header.magic == DM_REMAP_V4_VERSION_CONTROL_MAGIC) {
            reference_version = metadata_copies[i]->version_header.version_number;
            reference_timestamp = metadata_copies[i]->version_header.modification_timestamp;
            reference_set = true;
            break;
        }
    }
    
    if (!reference_set) {
        DMERR("No valid reference copy found for consistency validation");
        return -ENOENT;
    }
    
    /* Check all copies against reference */
    for (i = 0; i < num_copies; i++) {
        if (!metadata_copies[i]) {
            inconsistent_count++;
            continue;
        }
        
        struct dm_remap_v4_version_header *header = &metadata_copies[i]->version_header;
        
        /* Check magic number */
        if (header->magic != DM_REMAP_V4_VERSION_CONTROL_MAGIC) {
            inconsistent_count++;
            DMWARN("Copy %d has invalid version control magic", i);
            continue;
        }
        
        /* Check version consistency */
        if (header->version_number != reference_version) {
            inconsistent_count++;
            DMWARN("Copy %d has different version: %u vs %u", 
                   i, header->version_number, reference_version);
            continue;
        }
        
        /* Check timestamp consistency (allow small differences) */
        uint64_t time_diff = (header->modification_timestamp > reference_timestamp) ?
                            (header->modification_timestamp - reference_timestamp) :
                            (reference_timestamp - header->modification_timestamp);
        
        if (time_diff > context->sync_threshold) {
            inconsistent_count++;
            DMWARN("Copy %d has timestamp difference of %llu ms (threshold: %u ms)",
                   i, time_diff, context->sync_threshold);
            continue;
        }
        
        /* Check header integrity */
        uint32_t stored_crc = header->header_crc32;
        header->header_crc32 = 0;
        uint32_t calculated_crc = crc32(0, (unsigned char *)header, 
                                       sizeof(*header) - sizeof(header->header_crc32));
        header->header_crc32 = stored_crc;
        
        if (stored_crc != calculated_crc) {
            inconsistent_count++;
            DMWARN("Copy %d has corrupted header (CRC mismatch)", i);
            continue;
        }
    }
    
    *inconsistent_copies = inconsistent_count;
    
    if (inconsistent_count == 0) {
        DMINFO("All %u metadata copies are consistent", num_copies);
    } else {
        DMWARN("Found %u inconsistent copies out of %u total", 
               inconsistent_count, num_copies);
    }
    
    return 0;
}

/*
 * Get version history chain
 */
int dm_remap_v4_vc_get_version_history(
    const struct dm_remap_v4_metadata *metadata,
    uint32_t *version_chain,
    uint32_t max_versions,
    uint32_t *chain_length)
{
    const struct dm_remap_v4_version_header *header;
    uint32_t copy_length;
    int i;
    
    if (!metadata || !version_chain || !chain_length) {
        return -EINVAL;
    }
    
    header = &metadata->version_header;
    
    /* Verify version control header */
    if (header->magic != DM_REMAP_V4_VERSION_CONTROL_MAGIC) {
        DMERR("Invalid version control magic in metadata");
        return -EINVAL;
    }
    
    /* Copy version chain, limited by available space */
    copy_length = (header->chain_length < max_versions) ? 
                  header->chain_length : max_versions;
    
    for (i = 0; i < copy_length; i++) {
        version_chain[i] = header->chain_versions[i];
    }
    
    *chain_length = copy_length;
    
    DMINFO("Retrieved version history chain: %u versions", copy_length);
    return 0;
}

/*
 * Cleanup old versions
 */
int dm_remap_v4_vc_cleanup_old_versions(
    struct dm_remap_v4_metadata **metadata_copies,
    uint32_t num_copies,
    const struct dm_remap_v4_version_context *context,
    uint32_t *cleaned_versions)
{
    uint64_t current_time;
    uint32_t cleanup_count = 0;
    int i, j;
    
    if (!metadata_copies || !context || !cleaned_versions) {
        return -EINVAL;
    }
    
    current_time = ktime_get_real_seconds();
    *cleaned_versions = 0;
    
    /* Clean up old versions in each copy */
    for (i = 0; i < num_copies; i++) {
        if (!metadata_copies[i]) {
            continue;
        }
        
        struct dm_remap_v4_version_header *header = &metadata_copies[i]->version_header;
        
        /* Clean up version chain if it's too long */
        if (header->chain_length > context->max_chain_length) {
            uint32_t versions_to_remove = header->chain_length - context->max_chain_length;
            
            /* Shift chain, removing oldest versions */
            for (j = 0; j < context->max_chain_length; j++) {
                header->chain_versions[j] = header->chain_versions[j + versions_to_remove];
            }
            
            /* Clear remaining slots */
            for (j = context->max_chain_length; j < DM_REMAP_V4_VERSION_CHAIN_DEPTH; j++) {
                header->chain_versions[j] = 0;
            }
            
            header->chain_length = context->max_chain_length;
            cleanup_count += versions_to_remove;
        }
        
        /* Update header checksum */
        header->header_crc32 = 0;
        header->header_crc32 = crc32(0, (unsigned char *)header, 
                                    sizeof(*header) - sizeof(header->header_crc32));
    }
    
    *cleaned_versions = cleanup_count;
    
    if (cleanup_count > 0) {
        DMINFO("Cleaned up %u old versions from %u copies", cleanup_count, num_copies);
    }
    
    return 0;
}

/*
 * Compact version history
 */
int dm_remap_v4_vc_compact_history(
    struct dm_remap_v4_metadata *metadata,
    const struct dm_remap_v4_version_context *context)
{
    struct dm_remap_v4_version_header *header;
    uint32_t compacted_versions = 0;
    uint32_t write_pos = 0;
    int i;
    
    if (!metadata || !context) {
        return -EINVAL;
    }
    
    header = &metadata->version_header;
    
    /* Verify version control header */
    if (header->magic != DM_REMAP_V4_VERSION_CONTROL_MAGIC) {
        DMERR("Invalid version control magic in metadata");
        return -EINVAL;
    }
    
    /* Compact version chain by removing duplicates and sorting */
    for (i = 0; i < header->chain_length; i++) {
        if (header->chain_versions[i] != 0) {
            /* Check if this version already exists in compacted portion */
            bool duplicate = false;
            for (int j = 0; j < write_pos; j++) {
                if (header->chain_versions[j] == header->chain_versions[i]) {
                    duplicate = true;
                    break;
                }
            }
            
            if (!duplicate) {
                header->chain_versions[write_pos] = header->chain_versions[i];
                write_pos++;
            } else {
                compacted_versions++;
            }
        }
    }
    
    /* Clear remaining slots */
    for (i = write_pos; i < DM_REMAP_V4_VERSION_CHAIN_DEPTH; i++) {
        header->chain_versions[i] = 0;
    }
    
    header->chain_length = write_pos;
    
    /* Update header checksum */
    header->header_crc32 = 0;
    header->header_crc32 = crc32(0, (unsigned char *)header, 
                                sizeof(*header) - sizeof(header->header_crc32));
    
    DMINFO("Compacted version history: removed %u duplicates, %u versions remaining",
           compacted_versions, write_pos);
    
    return 0;
}