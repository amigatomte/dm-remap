/* dm-remap v4.0 Enhanced Metadata Format Design
 * 
 * This header defines the comprehensive metadata structures that enable:
 * - Automatic Setup Reassembly (Priority 6)
 * - Multiple Spare Device Support (Priority 5) 
 * - Configuration Storage and Discovery
 * - Redundant Storage with Integrity Protection
 * - Version Control and Conflict Resolution
 *
 * Author: dm-remap development team
 * Version: 4.0
 * Date: October 2025
 */

#ifndef DM_REMAP_V4_METADATA_H
#define DM_REMAP_V4_METADATA_H

#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/crc32.h>
#include <linux/time.h>

/* Forward declarations */
struct dm_dev;

/* ========================================================================
 * CONSTANTS AND LIMITS
 * ======================================================================== */

#define DM_REMAP_V4_MAGIC           0x444D524D  /* "DMRM" */
#define DM_REMAP_V4_VERSION         0x00040000  /* Version 4.0.0 */
#define DM_REMAP_METADATA_SIGNATURE "dm-remap-v4.0-metadata"

/* Metadata storage locations (sector offsets) */
#define DM_REMAP_METADATA_LOCATIONS 5
static const u64 metadata_sector_offsets[DM_REMAP_METADATA_LOCATIONS] = {
    0,      /* Primary metadata at sector 0 */
    1024,   /* Backup 1 at sector 1024 (512KB offset) */
    2048,   /* Backup 2 at sector 2048 (1MB offset) */
    4096,   /* Backup 3 at sector 4096 (2MB offset) */
    8192    /* Backup 4 at sector 8192 (4MB offset) */
};

/* Minimum spare device size requirements */
#define DM_REMAP_MIN_SPARE_SIZE_SECTORS 16384   /* 8MB minimum (assumes 512-byte sectors) */
#define DM_REMAP_METADATA_RESERVED_SECTORS 8192 /* Sectors reserved for metadata copies */
#define DM_REMAP_MIN_USABLE_SPARE_SECTORS 8192  /* Minimum sectors available for remapping */

/* Configuration limits */
#define DM_REMAP_MAX_SPARES         8
#define DM_REMAP_MAX_PATH_LEN       256
#define DM_REMAP_MAX_PARAMS_LEN     512
#define DM_REMAP_MAX_SYSFS_SETTINGS 32
#define DM_REMAP_UUID_SIZE          16
#define DM_REMAP_SIGNATURE_SIZE     32

/* ========================================================================
 * CORE METADATA STRUCTURES
 * ======================================================================== */

/**
 * struct dm_remap_device_fingerprint - Unique device identification
 * 
 * This structure provides multiple ways to identify a device to handle
 * cases where device paths change but the physical device remains the same.
 */
struct dm_remap_device_fingerprint {
    u8 uuid[DM_REMAP_UUID_SIZE];           /* Device UUID */
    char device_path[DM_REMAP_MAX_PATH_LEN]; /* Original device path */
    u64 device_size_sectors;                /* Device size in sectors */
    u32 sector_size;                        /* Sector size in bytes */
    u64 device_serial_hash;                 /* Hash of device serial number */
    u64 filesystem_uuid_hash;               /* Hash of filesystem UUID if present */
    u32 device_fingerprint_crc;            /* CRC32 of this structure */
} __packed;

/**
 * struct dm_remap_target_configuration - dm-remap target setup parameters
 * 
 * Stores all information needed to reconstruct the dm-remap target exactly
 * as it was originally configured.
 */
struct dm_remap_target_configuration {
    char target_params[DM_REMAP_MAX_PARAMS_LEN]; /* Original target parameters */
    u64 target_size_sectors;                     /* Target size in sectors */
    u32 target_flags;                            /* Target creation flags */
    
    /* Runtime configuration */
    u32 sysfs_settings[DM_REMAP_MAX_SYSFS_SETTINGS]; /* sysfs parameter values */
    u32 sysfs_settings_count;                         /* Number of valid settings */
    
    /* Policy settings */
    u32 health_scan_interval;                    /* Health scanning interval (seconds) */
    u32 remap_threshold;                         /* Health threshold for remapping */
    u32 alert_threshold;                         /* Health threshold for alerts */
    u8 auto_remap_enabled;                       /* Automatic remapping flag */
    u8 maintenance_mode;                         /* Maintenance mode flag */
    
    u32 config_crc;                             /* CRC32 of this structure */
} __packed;

/**
 * struct dm_remap_spare_device_info - Information about spare devices
 * 
 * Stores information about all spare devices associated with this dm-remap target.
 */
struct dm_remap_spare_device_info {
    u8 spare_count;                                              /* Number of spare devices */
    struct dm_remap_device_fingerprint spares[DM_REMAP_MAX_SPARES]; /* Spare device fingerprints */
    
    /* Spare device relationships and policies */
    u8 primary_spare_index;                     /* Index of primary spare device */
    u8 load_balancing_policy;                   /* Load balancing algorithm */
    u32 spare_allocation_policy;                /* Spare sector allocation strategy */
    
    /* Spare device health and status */
    u32 spare_health_scores[DM_REMAP_MAX_SPARES]; /* Last known health scores */
    u64 spare_last_checked[DM_REMAP_MAX_SPARES];  /* Timestamps of last health checks */
    
    u32 spare_info_crc;                        /* CRC32 of this structure */
} __packed;

/**
 * struct dm_remap_reassembly_instructions - Setup reconstruction instructions
 * 
 * Contains step-by-step instructions for automatically reconstructing the
 * dm-remap target configuration.
 */
struct dm_remap_reassembly_instructions {
    u8 instruction_version;                     /* Instruction format version */
    u8 requires_user_confirmation;              /* Requires manual confirmation */
    u8 safe_mode_only;                         /* Only allow safe mode reassembly */
    
    /* Device validation requirements */
    u8 validate_main_device_size;              /* Verify main device size matches */
    u8 validate_spare_device_sizes;            /* Verify spare device sizes match */
    u8 validate_filesystem_signatures;         /* Check filesystem signatures */
    
    /* Reassembly steps and safety checks */
    u32 pre_assembly_checks;                   /* Bitmask of required pre-checks */
    u32 post_assembly_verification;            /* Bitmask of post-assembly verifications */
    
    /* Emergency recovery options */
    u8 allow_degraded_assembly;               /* Allow assembly with missing spares */
    u8 allow_size_mismatch_recovery;          /* Allow recovery despite size mismatches */
    
    u32 instructions_crc;                      /* CRC32 of this structure */
} __packed;

/**
 * struct dm_remap_metadata_integrity - Integrity and versioning information
 * 
 * Provides integrity protection and version control for the entire metadata.
 */
struct dm_remap_metadata_integrity {
    u32 magic;                                  /* Magic number for validation */
    u32 version;                                /* Metadata format version */
    char signature[DM_REMAP_SIGNATURE_SIZE];   /* Human-readable signature */
    
    /* Version control */
    u64 version_counter;                        /* Monotonic version counter */
    u64 creation_timestamp;                     /* Original creation time */
    u64 last_update_timestamp;                  /* Last update time */
    u32 update_sequence_number;                 /* Update sequence for ordering */
    
    /* Integrity protection */
    u32 metadata_size;                          /* Total metadata size */
    u32 individual_section_crcs[8];            /* CRC32 of each major section */
    u32 overall_metadata_crc;                   /* CRC32 of entire metadata */
    
    /* Redundancy information */
    u8 total_copies;                           /* Total number of metadata copies */
    u8 minimum_valid_copies;                   /* Minimum copies needed for recovery */
    u32 copy_location_map;                     /* Bitmask of copy locations */
    
    u32 integrity_crc;                         /* CRC32 of this structure */
} __packed;

/* ========================================================================
 * MASTER METADATA STRUCTURE
 * ======================================================================== */

/**
 * struct dm_remap_v4_metadata - Complete v4.0 metadata structure
 * 
 * This is the master structure that contains all metadata needed for
 * automatic setup reassembly and advanced v4.0 features.
 */
struct dm_remap_v4_metadata {
    /* Integrity and versioning (must be first for validation) */
    struct dm_remap_metadata_integrity integrity;
    
    /* Device identification */
    struct dm_remap_device_fingerprint main_device;
    struct dm_remap_spare_device_info spare_devices;
    
    /* Configuration and setup */
    struct dm_remap_target_configuration target_config;
    struct dm_remap_reassembly_instructions reassembly;
    
    /* Legacy v3.0 compatibility (existing remap data) */
    struct {
        u32 remap_count;                       /* Number of active remaps */
        u64 next_spare_sector;                 /* Next available spare sector */
        /* Note: Full v3.0 remap table stored separately for size efficiency */
    } legacy_remap_data;
    
    /* Future expansion area */
    u8 reserved_expansion[512];                /* Reserved for future features */
    
    /* Final integrity check */
    u32 final_crc;                            /* CRC32 of entire structure */
} __packed;

/* ========================================================================
 * METADATA VALIDATION AND UTILITY FUNCTIONS
 * ======================================================================== */

/**
 * Metadata validation levels
 */
enum dm_remap_validation_level {
    DM_REMAP_VALIDATE_BASIC,        /* Basic structure and magic validation */
    DM_REMAP_VALIDATE_INTEGRITY,    /* Full CRC and integrity validation */
    DM_REMAP_VALIDATE_COMPLETE,     /* Complete validation including device checks */
    DM_REMAP_VALIDATE_FORENSIC      /* Detailed forensic validation for recovery */
};

/**
 * Metadata operation results
 */
enum dm_remap_metadata_result {
    DM_REMAP_METADATA_OK = 0,
    DM_REMAP_METADATA_CORRUPT,
    DM_REMAP_METADATA_VERSION_MISMATCH,
    DM_REMAP_METADATA_DEVICE_MISMATCH,
    DM_REMAP_METADATA_CRC_MISMATCH,
    DM_REMAP_METADATA_INCOMPLETE,
    DM_REMAP_METADATA_CONFLICT,
    DM_REMAP_METADATA_RECOVERABLE
};

/* ========================================================================
 * FUNCTION DECLARATIONS (to be implemented)
 * ======================================================================== */

/* Metadata creation and initialization */
int dm_remap_v4_create_metadata(struct dm_remap_v4_metadata *metadata,
                                struct dm_dev *main_dev,
                                struct dm_dev **spare_devs,
                                int spare_count,
                                const char *target_params);

int dm_remap_create_target_configuration(struct dm_remap_target_configuration *config,
                                        const char *target_params,
                                        u64 target_size,
                                        const u32 *sysfs_settings,
                                        u32 settings_count);

int dm_remap_create_spare_device_info(struct dm_remap_spare_device_info *info,
                                     struct dm_dev **spare_devs,
                                     int spare_count);

int dm_remap_create_reassembly_instructions(struct dm_remap_reassembly_instructions *instructions,
                                           u8 safety_level);

int dm_remap_create_metadata_integrity(struct dm_remap_metadata_integrity *integrity,
                                      u32 total_metadata_size);

/* Metadata validation */
enum dm_remap_metadata_result dm_remap_v4_validate_metadata(
                                const struct dm_remap_v4_metadata *metadata,
                                enum dm_remap_validation_level level);

/* CRC calculation helpers */
u32 dm_remap_calculate_device_fingerprint_crc(const struct dm_remap_device_fingerprint *fp);
u32 dm_remap_calculate_target_config_crc(const struct dm_remap_target_configuration *config);
u32 dm_remap_calculate_spare_info_crc(const struct dm_remap_spare_device_info *info);
u32 dm_remap_calculate_metadata_crc(const struct dm_remap_v4_metadata *metadata);

/* Version control */
void dm_remap_increment_version_counter(struct dm_remap_v4_metadata *metadata);
int dm_remap_compare_metadata_versions(const struct dm_remap_v4_metadata *meta1,
                                      const struct dm_remap_v4_metadata *meta2);

/* Validation functions */
int dm_remap_calculate_section_crcs(const struct dm_remap_v4_metadata *metadata,
                                   u32 *section_crcs);
int dm_remap_validate_metadata_magic(const struct dm_remap_v4_metadata *metadata);
int dm_remap_validate_metadata_crc(const struct dm_remap_v4_metadata *metadata,
                                  u8 *section_errors);

/* Device fingerprinting */
int dm_remap_create_device_fingerprint(struct dm_remap_device_fingerprint *fp,
                                      struct dm_dev *dev);
int dm_remap_match_device_fingerprint(const struct dm_remap_device_fingerprint *fp,
                                     struct dm_dev *dev);

/* Spare device validation */
int dm_remap_validate_spare_device_size(struct dm_dev *spare_dev);
int dm_remap_check_metadata_storage_requirements(struct dm_dev *spare_dev);

/**
 * dm_remap_validate_spare_device_size - Validate spare device meets minimum size requirements
 * @spare_dev: The spare device to validate
 * 
 * Returns: 0 if device meets requirements, negative error code otherwise
 * 
 * This function ensures the spare device is large enough to store:
 * - All 5 metadata copies at required sector offsets
 * - Adequate space for actual sector remapping operations
 * - Safety margins for metadata growth and system overhead
 */
static inline bool dm_remap_spare_device_adequate_size(u64 spare_sectors)
{
    return spare_sectors >= DM_REMAP_MIN_SPARE_SIZE_SECTORS;
}

#endif /* DM_REMAP_V4_METADATA_H */