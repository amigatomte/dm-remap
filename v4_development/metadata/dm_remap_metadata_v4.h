/* dm-remap v4.0 Enhanced Metadata Format Header
 * 
 * This header defines the v4.0 metadata structure with redundant storage,
 * integrity protection, and automatic setup reassembly capabilities.
 */

#ifndef DM_REMAP_METADATA_V4_H
#define DM_REMAP_METADATA_V4_H

#include <linux/types.h>
#include <linux/crc32.h>

/* Metadata format constants */
#define DM_REMAP_METADATA_V4_MAGIC      0x444D5234  /* "DMR4" */
#define DM_REMAP_METADATA_V4_VERSION    1
#define DM_REMAP_METADATA_COPIES        5
#define DM_REMAP_METADATA_SIZE          4096        /* 4KB per copy */
#define DM_REMAP_METADATA_SECTORS       8           /* 4KB = 8 sectors */

/* Footer magic for validation */
#define DM_REMAP_METADATA_FOOTER_MAGIC  0x34524D44  /* "DMR4" reversed */

/* Metadata copy sector locations */
static const sector_t metadata_copy_sectors[DM_REMAP_METADATA_COPIES] = {
    0,      /* Primary: Immediate access, v3.0 compatible */
    1024,   /* Secondary: Early spare area (512KB offset) */
    2048,   /* Tertiary: Mid-range (1MB offset) */
    4096,   /* Quaternary: Higher range (2MB offset) */
    8192    /* Quinary: Extended range (4MB offset) */
};

/* Device fingerprint for identity verification */
struct device_fingerprint {
    u8 device_uuid[16];         /* Binary UUID */
    char serial_number[32];     /* Device serial number */
    char model_name[64];        /* Device model */
    u64 size_sectors;           /* Device size in sectors */
    u32 logical_block_size;     /* Logical block size */
    u32 physical_block_size;    /* Physical block size */
    u8 sha256_hash[32];         /* SHA-256 of above fields */
} __packed;

/* Health scanning configuration and statistics */
struct health_scanning_data {
    u64 last_scan_time;         /* Last scan timestamp (nanoseconds) */
    u32 scan_interval;          /* Scan interval in seconds */
    u32 health_score;           /* Overall health score (0-100) */
    u32 sector_scan_progress;   /* Current scan progress (0-100) */
    u32 scan_flags;             /* Scanning configuration flags */
    
    /* Health statistics */
    struct {
        u32 scans_completed;
        u32 errors_detected;
        u32 predictive_remaps;
        u32 scan_overhead_ms;
        u32 false_positives;
        u32 true_positives;
    } stats;
    
    u32 health_checksum;        /* CRC32 of health data */
} __packed;

/* Setup configuration for automatic reassembly */
struct setup_configuration {
    char main_device_uuid[37];      /* Main device UUID string */
    char main_device_path[256];     /* Original device path */
    u64 main_device_size;           /* Main device size in sectors */
    
    /* dm-remap target parameters */
    u32 target_params_len;          /* Length of stored parameters */
    char target_params[512];        /* Complete target line parameters */
    
    /* sysfs configuration snapshot */
    char sysfs_config[1024];        /* Key sysfs settings */
    
    /* Configuration metadata */
    u32 config_version;             /* Configuration format version */
    u64 creation_time;              /* Initial setup timestamp */
    u64 last_update_time;           /* Last configuration update */
    
    /* Device relationship */
    struct device_fingerprint main_device_fp;
    struct device_fingerprint spare_device_fp;
    
    u32 config_checksum;            /* CRC32 of configuration data */
} __packed;

/* v4.0 Enhanced Metadata Structure */
struct dm_remap_metadata_v4 {
    /* Header with integrity protection */
    struct {
        u32 magic;                  /* Magic number: DM_REMAP_METADATA_V4_MAGIC */
        u32 version;                /* Metadata format version */
        u64 sequence_number;        /* Monotonic counter for conflict resolution */
        u32 total_size;             /* Total metadata size in bytes */
        u32 header_checksum;        /* CRC32 of this header */
        u32 data_checksum;          /* CRC32 of data sections */
        u32 copy_index;             /* Which copy this is (0-4) */
        u64 timestamp;              /* Creation/update timestamp (nanoseconds) */
        u8  reserved[32];           /* Reserved for future extensions */
    } header;
    
    /* v3.0 compatibility section (preserved exactly) */
    struct dm_remap_metadata_v3 legacy;
    
    /* v4.0 Enhanced sections */
    struct setup_configuration setup_config;
    struct health_scanning_data health_data;
    
    /* Padding to ensure exact 4KB size */
    u8 padding[METADATA_PADDING_SIZE];
    
    /* Footer validation */
    struct {
        u32 footer_magic;           /* Footer magic: DM_REMAP_METADATA_FOOTER_MAGIC */
        u32 overall_checksum;       /* CRC32 of entire metadata */
    } footer;
} __packed;

/* Calculate padding size to reach exactly 4KB */
#define METADATA_PADDING_SIZE (DM_REMAP_METADATA_SIZE - \
                              sizeof(struct { \
                                  struct { \
                                      u32 magic; u32 version; u64 sequence_number; \
                                      u32 total_size; u32 header_checksum; u32 data_checksum; \
                                      u32 copy_index; u64 timestamp; u8 reserved[32]; \
                                  } header; \
                                  struct dm_remap_metadata_v3 legacy; \
                                  struct setup_configuration setup_config; \
                                  struct health_scanning_data health_data; \
                                  struct { u32 footer_magic; u32 overall_checksum; } footer; \
                              }))

/* Metadata validation results */
enum metadata_validation_result {
    METADATA_VALID = 0,
    METADATA_HEADER_CORRUPT = 1,
    METADATA_CONFIG_CORRUPT = 2,
    METADATA_HEALTH_CORRUPT = 3,
    METADATA_OVERALL_CORRUPT = 4,
    METADATA_MAGIC_INVALID = 5,
    METADATA_VERSION_UNSUPPORTED = 6,
    METADATA_SIZE_INVALID = 7
};

/* Conflict resolution results */
enum conflict_resolution_result {
    CONFLICT_RESOLVED_SUCCESS = 0,
    CONFLICT_ALL_COPIES_CORRUPT = 1,
    CONFLICT_VERSION_MISMATCH = 2,
    CONFLICT_DEVICE_MISMATCH = 3,
    CONFLICT_NO_VALID_COPIES = 4
};

/* Function prototypes for metadata operations */

/* Checksum calculation functions */
u32 calculate_header_checksum(const struct dm_remap_metadata_v4 *meta);
u32 calculate_config_checksum(const struct setup_configuration *config);
u32 calculate_health_checksum(const struct health_scanning_data *health);
u32 calculate_overall_checksum(const struct dm_remap_metadata_v4 *meta);

/* Metadata validation */
enum metadata_validation_result validate_metadata_copy(
    const struct dm_remap_metadata_v4 *meta);

/* Device fingerprinting */
int generate_device_fingerprint(struct block_device *bdev,
                               struct device_fingerprint *fp);
bool compare_device_fingerprints(const struct device_fingerprint *fp1,
                               const struct device_fingerprint *fp2);

/* Metadata I/O operations */
int write_redundant_metadata_v4(struct block_device *spare_bdev,
                               struct dm_remap_metadata_v4 *meta);
int read_redundant_metadata_v4(struct block_device *spare_bdev,
                              struct dm_remap_metadata_v4 **meta_out);

/* Conflict resolution */
enum conflict_resolution_result resolve_metadata_conflicts(
    struct dm_remap_metadata_v4 copies[DM_REMAP_METADATA_COPIES],
    struct dm_remap_metadata_v4 **selected_copy);

/* Metadata repair */
int repair_corrupted_metadata_copies(struct block_device *spare_bdev,
                                   const struct dm_remap_metadata_v4 *valid_meta);

/* Version upgrade */
int upgrade_metadata_v3_to_v4(struct block_device *spare_bdev,
                             const struct dm_remap_metadata_v3 *old_meta);

/* Sequence number management */
u64 get_next_sequence_number(void);
bool is_newer_metadata(u64 seq1, u64 seq2);

/* Utility functions */
bool validate_metadata_sector_placement(void);
unsigned int calculate_optimal_metadata_write_order(void);

#endif /* DM_REMAP_METADATA_V4_H */