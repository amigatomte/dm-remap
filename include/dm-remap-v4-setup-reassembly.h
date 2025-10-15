/*
 * dm-remap v4.0 Automatic Setup Reassembly System
 * Header file for configuration metadata storage and device discovery
 * 
 * This system enables automatic device discovery and setup reconstruction
 * by storing comprehensive configuration metadata across multiple spare devices
 * with redundant storage, integrity protection, and conflict resolution.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#ifndef DM_REMAP_V4_SETUP_REASSEMBLY_H
#define DM_REMAP_V4_SETUP_REASSEMBLY_H

#include <linux/types.h>
#include <linux/uuid.h>

/*
 * Automatic Setup Reassembly Constants
 */

/* Magic numbers and signatures */
#define DM_REMAP_V4_REASSEMBLY_MAGIC        0xAB5EAB1E
#define DM_REMAP_V4_CONFIG_SIGNATURE        0xC0FDEC0D
#define DM_REMAP_V4_DEVICE_FINGERPRINT_MAGIC 0xFD9E4B7C

/* Metadata storage locations (sector offsets on spare devices) */
#define DM_REMAP_V4_METADATA_COPY_SECTORS   5
#define DM_REMAP_V4_METADATA_SECTOR_0       0     /* Primary copy */
#define DM_REMAP_V4_METADATA_SECTOR_1       1024  /* Secondary copy */
#define DM_REMAP_V4_METADATA_SECTOR_2       2048  /* Tertiary copy */
#define DM_REMAP_V4_METADATA_SECTOR_3       4096  /* Quaternary copy */
#define DM_REMAP_V4_METADATA_SECTOR_4       8192  /* Backup copy */

/* Configuration limits */
#define DM_REMAP_V4_MAX_DEVICE_PATH         256
#define DM_REMAP_V4_MAX_TARGET_PARAMS       512
#define DM_REMAP_V4_MAX_SPARE_DEVICES       16
#define DM_REMAP_V4_MAX_SYSFS_SETTINGS      32
#define DM_REMAP_V4_MAX_POLICY_RULES        64
#define DM_REMAP_V4_DEVICE_FINGERPRINT_SIZE 64
#define DM_REMAP_V4_SETUP_DESCRIPTION_SIZE  128

/* Discovery and validation */
#define DM_REMAP_V4_MIN_VALID_COPIES        1    /* Minimum copies needed for reassembly */
#define DM_REMAP_V4_PREFERRED_VALID_COPIES  3    /* Preferred copies for reliability */
#define DM_REMAP_V4_VERSION_TOLERANCE       100  /* Max version difference to consider valid */
#define DM_REMAP_V4_MIN_CONFIDENCE_THRESHOLD 70  /* Minimum confidence % for auto-reassembly */

/*
 * Device Fingerprint Structure
 * Unique identification for main and spare devices
 */
struct dm_remap_v4_device_fingerprint {
    uint32_t magic;                    /* DM_REMAP_V4_DEVICE_FINGERPRINT_MAGIC */
    uuid_t device_uuid;                /* Device UUID */
    char device_path[DM_REMAP_V4_MAX_DEVICE_PATH];  /* Device path (/dev/...) */
    uint64_t device_size;              /* Device size in sectors */
    uint64_t device_capacity;          /* Total capacity in bytes */
    uint32_t sector_size;              /* Sector size (usually 512) */
    uint32_t device_type;              /* Device type flags */
    uint64_t creation_timestamp;       /* When fingerprint was created */
    uint64_t last_seen_timestamp;      /* Last time device was detected */
    char device_serial[32];            /* Device serial number (if available) */
    char device_model[64];             /* Device model string */
    uint32_t fingerprint_crc32;       /* CRC32 checksum of fingerprint */
};

/*
 * Spare Device Relationship Structure
 * Describes relationship between main device and spare devices
 */
struct dm_remap_v4_spare_relationship {
    struct dm_remap_v4_device_fingerprint spare_fingerprint;  /* Spare device info */
    uint32_t spare_priority;           /* Priority level (0=highest) */
    uint32_t spare_status;             /* Status flags */
    uint64_t assigned_timestamp;       /* When spare was assigned */
    uint64_t capacity_used;            /* Sectors used on this spare */
    uint64_t capacity_available;       /* Available sectors on spare */
    uint32_t metadata_copies_stored;   /* Number of metadata copies on this spare */
    uint32_t relationship_flags;       /* Additional flags */
    uint32_t spare_crc32;              /* CRC32 checksum of relationship */
};

/*
 * DM-Target Parameters Structure
 * Complete dm-remap target configuration
 */
struct dm_remap_v4_target_config {
    uint32_t config_magic;             /* DM_REMAP_V4_CONFIG_SIGNATURE */
    char target_params[DM_REMAP_V4_MAX_TARGET_PARAMS];  /* Target parameters string */
    uint64_t main_device_start;        /* Start sector on main device */
    uint64_t target_device_size;       /* Size of target device in sectors */
    uint32_t remap_policy;             /* Remapping policy flags */
    uint32_t performance_mode;         /* Performance optimization mode */
    uint32_t redundancy_level;         /* Redundancy level (future use) */
    uint32_t auto_recovery_enabled;    /* Auto-recovery settings */
    uint64_t created_timestamp;        /* When configuration was created */
    uint64_t modified_timestamp;       /* Last modification timestamp */
    uint32_t config_version;           /* Configuration version number */
    uint32_t target_crc32;             /* CRC32 checksum of target config */
};

/*
 * Sysfs Configuration Structure
 * Stores sysfs settings and policies
 */
struct dm_remap_v4_sysfs_setting {
    char setting_name[64];             /* Setting name (e.g., "scan_interval") */
    char setting_value[128];           /* Setting value */
    uint32_t setting_type;             /* Data type (int, string, bool, etc.) */
    uint32_t setting_flags;            /* Flags (read-only, admin-only, etc.) */
    uint32_t setting_crc32;            /* CRC32 checksum of setting */
};

struct dm_remap_v4_sysfs_config {
    uint32_t num_settings;             /* Number of stored settings */
    struct dm_remap_v4_sysfs_setting settings[DM_REMAP_V4_MAX_SYSFS_SETTINGS];
    uint64_t config_timestamp;         /* When sysfs config was saved */
    uint32_t sysfs_config_crc32;       /* CRC32 checksum of sysfs config */
};

/*
 * Policy Rules Structure
 * Automation policies and rules
 */
struct dm_remap_v4_policy_rule {
    char rule_name[64];                /* Policy rule name */
    char rule_condition[128];          /* Condition string */
    char rule_action[128];             /* Action string */
    uint32_t rule_priority;            /* Rule priority (0=highest) */
    uint32_t rule_enabled;             /* Whether rule is enabled */
    uint64_t rule_created;             /* When rule was created */
    uint32_t rule_crc32;               /* CRC32 checksum of rule */
};

struct dm_remap_v4_policy_config {
    uint32_t num_rules;                /* Number of policy rules */
    struct dm_remap_v4_policy_rule rules[DM_REMAP_V4_MAX_POLICY_RULES];
    uint64_t policy_timestamp;         /* When policies were last updated */
    uint32_t policy_config_crc32;      /* CRC32 checksum of policy config */
};

/*
 * Complete Setup Configuration Metadata
 * This is the main structure stored redundantly across spare devices
 */
struct dm_remap_v4_setup_metadata {
    /* Header information */
    uint32_t magic;                    /* DM_REMAP_V4_REASSEMBLY_MAGIC */
    uint32_t metadata_version;         /* Metadata format version */
    uint64_t version_counter;          /* Monotonic counter for conflict resolution */
    uint64_t created_timestamp;        /* When setup was created */
    uint64_t modified_timestamp;       /* Last modification time */
    char setup_description[DM_REMAP_V4_SETUP_DESCRIPTION_SIZE];  /* Human-readable description */
    
    /* Device identification */
    struct dm_remap_v4_device_fingerprint main_device;        /* Main device fingerprint */
    uint32_t num_spare_devices;       /* Number of spare devices */
    struct dm_remap_v4_spare_relationship spare_devices[DM_REMAP_V4_MAX_SPARE_DEVICES];
    
    /* Target configuration */
    struct dm_remap_v4_target_config target_config;           /* DM target parameters */
    
    /* System configuration */
    struct dm_remap_v4_sysfs_config sysfs_config;             /* Sysfs settings */
    struct dm_remap_v4_policy_config policy_config;           /* Automation policies */
    
    /* Metadata management */
    uint32_t metadata_copies_count;   /* Number of copies of this metadata */
    uint64_t metadata_copy_locations[DM_REMAP_V4_METADATA_COPY_SECTORS];  /* Where copies are stored */
    uint32_t corruption_detected;     /* Corruption detection flags */
    uint32_t repair_needed;           /* Whether repair is needed */
    
    /* Integrity protection */
    uint32_t header_crc32;            /* CRC32 of header section */
    uint32_t devices_crc32;           /* CRC32 of device section */
    uint32_t config_crc32;            /* CRC32 of configuration section */
    uint32_t overall_crc32;           /* CRC32 of entire metadata (excluding this field) */
};

/*
 * Discovery Results Structure
 * Results from scanning system for dm-remap setups
 */
struct dm_remap_v4_discovery_result {
    struct list_head list;            /* Linked list node for tracking */
    struct dm_remap_v4_setup_metadata metadata;  /* Discovered metadata */
    char device_path[DM_REMAP_V4_MAX_DEVICE_PATH];  /* Device being scanned */
    char spare_device_path[DM_REMAP_V4_MAX_DEVICE_PATH];  /* Where it was found */
    uint64_t discovery_timestamp;     /* When it was discovered */
    uint32_t copies_found;            /* Number of copies found */
    uint32_t copies_valid;            /* Number of valid copies */
    uint32_t corruption_level;        /* Level of corruption detected */
    uint32_t confidence_score;        /* Confidence percentage (0-100) */
    uint32_t discovery_flags;         /* Discovery status flags */
    bool has_metadata;                /* Whether valid metadata was found */
};

/*
 * Metadata Read Result Structure
 * Results from reading and validating metadata from a device
 */
struct dm_remap_v4_metadata_read_result {
    char device_path[DM_REMAP_V4_MAX_DEVICE_PATH];  /* Device that was read */
    uint64_t read_timestamp;          /* When the read occurred */
    uint32_t copies_found;            /* Number of copies found */
    uint32_t copies_valid;            /* Number of valid copies */
    uint32_t corruption_level;        /* Level of corruption detected */
    uint32_t confidence_score;        /* Confidence percentage (0-100) */
};

/* Maximum devices per setup group during discovery */
#define DM_REMAP_V4_MAX_DEVICES_PER_GROUP   16

/*
 * Setup Group Structure
 * Groups discovered results that belong to the same setup
 */
struct dm_remap_v4_setup_group {
    uint32_t group_id;                /* Unique group identifier */
    char setup_description[DM_REMAP_V4_SETUP_DESCRIPTION_SIZE];  /* Setup description */
    uuid_t main_device_uuid;          /* Main device UUID */
    uint64_t discovery_timestamp;     /* When group was created */
    uint32_t group_confidence;        /* Overall group confidence (0-100) */
    struct dm_remap_v4_setup_metadata best_metadata;  /* Best metadata found */
    struct dm_remap_v4_discovery_result devices[DM_REMAP_V4_MAX_DEVICES_PER_GROUP];  /* Devices in group */
    uint32_t num_devices;             /* Number of devices in group */
};

/*
 * Reconstruction Step Structure
 * Individual step in the setup reconstruction process
 */
struct dm_remap_v4_reconstruction_step {
    char description[128];            /* Step description */
    uint32_t step_type;               /* Step type (1=verify, 2=create, 3=configure) */
    uint32_t status;                  /* Step status */
    uint32_t reserved;                /* Reserved for future use */
};

/*
 * Reconstruction Plan Structure
 * Complete plan for reconstructing a dm-remap setup
 */
struct dm_remap_v4_reconstruction_plan {
    uint32_t group_id;                /* Setup group ID */
    uint64_t plan_timestamp;          /* When plan was created */
    uint32_t confidence_score;        /* Overall confidence (0-100) */
    char setup_name[DM_REMAP_V4_SETUP_DESCRIPTION_SIZE];  /* Setup name */
    char target_name[32];             /* DM target name */
    char target_params[DM_REMAP_V4_MAX_TARGET_PARAMS];  /* Target parameters */
    char main_device_path[DM_REMAP_V4_MAX_DEVICE_PATH];  /* Main device */
    char spare_device_paths[DM_REMAP_V4_MAX_SPARE_DEVICES][DM_REMAP_V4_MAX_DEVICE_PATH];  /* Spare devices */
    uint32_t num_spare_devices;       /* Number of spare devices */
    struct dm_remap_v4_sysfs_setting sysfs_settings[DM_REMAP_V4_MAX_SYSFS_SETTINGS];  /* Sysfs settings */
    uint32_t num_sysfs_settings;      /* Number of sysfs settings */
    char dmsetup_create_command[512]; /* dmsetup command to run */
    struct dm_remap_v4_reconstruction_step steps[16];  /* Reconstruction steps */
    uint32_t num_steps;               /* Number of steps */
};

/*
 * Setup Reassembly Context
 * Runtime context for discovery and reassembly operations
 */
struct dm_remap_v4_reassembly_context {
    uint32_t magic;                    /* Context magic number */
    uint32_t num_discoveries;          /* Number of setups discovered */
    struct dm_remap_v4_discovery_result discoveries[16];  /* Discovered setups */
    uint32_t scan_progress;            /* Scan progress percentage */
    uint64_t scan_start_time;          /* When scan started */
    uint64_t scan_duration;            /* How long scan took */
    uint32_t devices_scanned;          /* Number of devices scanned */
    uint32_t errors_encountered;       /* Number of errors during scan */
    char last_error[256];              /* Last error message */
    uint32_t context_crc32;            /* CRC32 checksum of context */
};

/*
 * Discovery Statistics Structure
 * Runtime statistics for device discovery and setup scanning
 */
struct dm_remap_v4_discovery_stats {
    uint64_t last_scan_timestamp;     /* Last scan time */
    uint32_t total_devices_scanned;   /* Total devices scanned */
    uint32_t setups_discovered;       /* Total setups discovered */
    uint64_t system_uptime;           /* System uptime */
    uint32_t setups_in_memory;        /* Setups currently in memory */
    uint32_t high_confidence_setups;  /* Setups with high confidence */
};

/*
 * Function Prototypes for Setup Reassembly API
 */

/* Metadata Creation and Storage */
int dm_remap_v4_create_setup_metadata(
    struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_device_fingerprint *main_device,
    const struct dm_remap_v4_target_config *target_config
);

int dm_remap_v4_store_metadata_redundantly(
    const struct dm_remap_v4_setup_metadata *metadata,
    const struct dm_remap_v4_spare_relationship *spare_devices,
    uint32_t num_spares
);

int dm_remap_v4_read_metadata_validated(
    const char *device_path,
    struct dm_remap_v4_setup_metadata *metadata,
    struct dm_remap_v4_metadata_read_result *read_result
);

int dm_remap_v4_update_metadata_version(
    struct dm_remap_v4_setup_metadata *metadata
);

/* Device Fingerprinting */
int dm_remap_v4_create_device_fingerprint(
    struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path
);

int dm_remap_v4_verify_device_fingerprint(
    const struct dm_remap_v4_device_fingerprint *fingerprint,
    const char *device_path
);

int dm_remap_v4_compare_device_fingerprints(
    const struct dm_remap_v4_device_fingerprint *fp1,
    const struct dm_remap_v4_device_fingerprint *fp2
);

/* Discovery and Scanning */
int dm_remap_v4_scan_for_setups(
    struct dm_remap_v4_reassembly_context *context
);

int dm_remap_v4_discover_spare_devices(
    struct dm_remap_v4_reassembly_context *context,
    const char *device_pattern
);

int dm_remap_v4_validate_discovered_metadata(
    struct dm_remap_v4_discovery_result *result
);

/* Metadata Validation and Repair */
int dm_remap_v4_validate_metadata_copies(
    const struct dm_remap_v4_setup_metadata *metadata_copies,
    uint32_t num_copies,
    struct dm_remap_v4_setup_metadata *validated_metadata
);

int dm_remap_v4_resolve_metadata_conflicts(
    const struct dm_remap_v4_setup_metadata *metadata_copies,
    uint32_t num_copies,
    struct dm_remap_v4_setup_metadata *resolved_metadata
);

int dm_remap_v4_repair_corrupted_metadata(
    struct dm_remap_v4_setup_metadata *corrupted_metadata,
    const struct dm_remap_v4_setup_metadata *valid_metadata
);

/* Setup Reconstruction */
int dm_remap_v4_reconstruct_target_setup(
    const struct dm_remap_v4_setup_metadata *metadata,
    char *dm_command_buffer,
    size_t buffer_size
);

int dm_remap_v4_restore_sysfs_configuration(
    const struct dm_remap_v4_sysfs_config *sysfs_config
);

int dm_remap_v4_restore_policy_configuration(
    const struct dm_remap_v4_policy_config *policy_config
);

/* Safety and Validation */
int dm_remap_v4_validate_setup_compatibility(
    const struct dm_remap_v4_setup_metadata *metadata
);

int dm_remap_v4_check_setup_conflicts(
    const struct dm_remap_v4_setup_metadata *metadata
);

int dm_remap_v4_verify_main_device_availability(
    const struct dm_remap_v4_device_fingerprint *main_device
);

/* Utility Functions */
uint32_t dm_remap_v4_calculate_metadata_crc32(
    const struct dm_remap_v4_setup_metadata *metadata
);

int dm_remap_v4_verify_metadata_integrity(
    const struct dm_remap_v4_setup_metadata *metadata
);

uint32_t dm_remap_v4_calculate_confidence_score(
    const struct dm_remap_v4_discovery_result *result
);

/* Debug and Information */
void dm_remap_v4_print_setup_metadata(
    const struct dm_remap_v4_setup_metadata *metadata
);

void dm_remap_v4_print_discovery_results(
    const struct dm_remap_v4_reassembly_context *context
);

const char* dm_remap_v4_reassembly_error_to_string(int error_code);

/*
 * Error Codes for Setup Reassembly
 */
#define DM_REMAP_V4_REASSEMBLY_SUCCESS              0
#define DM_REMAP_V4_REASSEMBLY_ERROR_INVALID_PARAMS -1
#define DM_REMAP_V4_REASSEMBLY_ERROR_NO_METADATA    -2
#define DM_REMAP_V4_REASSEMBLY_ERROR_CORRUPTED      -3
#define DM_REMAP_V4_REASSEMBLY_ERROR_VERSION_CONFLICT -4
#define DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISSING -5
#define DM_REMAP_V4_REASSEMBLY_ERROR_SETUP_CONFLICT -6
#define DM_REMAP_V4_REASSEMBLY_ERROR_INSUFFICIENT_COPIES -7
#define DM_REMAP_V4_REASSEMBLY_ERROR_CRC_MISMATCH   -8
#define DM_REMAP_V4_REASSEMBLY_ERROR_DEVICE_MISMATCH -9
#define DM_REMAP_V4_REASSEMBLY_ERROR_PERMISSION_DENIED -10
#define DM_REMAP_V4_REASSEMBLY_ERROR_LOW_CONFIDENCE -11

/*
 * Status Flags
 */
#define DM_REMAP_V4_METADATA_FLAG_VALID             0x00000001
#define DM_REMAP_V4_METADATA_FLAG_CORRUPTED         0x00000002
#define DM_REMAP_V4_METADATA_FLAG_REPAIRED          0x00000004
#define DM_REMAP_V4_METADATA_FLAG_CONFLICT          0x00000008
#define DM_REMAP_V4_METADATA_FLAG_OUTDATED          0x00000010
#define DM_REMAP_V4_METADATA_FLAG_BACKUP_COPY       0x00000020

#define DM_REMAP_V4_DISCOVERY_FLAG_COMPLETE         0x00000001
#define DM_REMAP_V4_DISCOVERY_FLAG_PARTIAL          0x00000002
#define DM_REMAP_V4_DISCOVERY_FLAG_CORRUPTED        0x00000004
#define DM_REMAP_V4_DISCOVERY_FLAG_CONFLICT         0x00000008
#define DM_REMAP_V4_DISCOVERY_FLAG_REPAIRABLE       0x00000010

#endif /* DM_REMAP_V4_SETUP_REASSEMBLY_H */