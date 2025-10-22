/**
 * dm-remap-v4.h - Pure v4.0 Header Definitions
 * 
 * Copyright (C) 2025 dm-remap Development Team
 * 
 * Clean slate v4.0 architecture with:
 * - No backward compatibility overhead
 * - Optimized memory layout  
 * - Enterprise features built-in
 * 
 * This header defines the pure v4.0 data structures and APIs.
 */

#ifndef DM_REMAP_V4_H
#define DM_REMAP_V4_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

/* v4.0 Constants */
#define DM_REMAP_METADATA_V4_MAGIC      0x444D5234  /* "DMR4" */
#define DM_REMAP_METADATA_V4_VERSION    4
#define DM_REMAP_V4_MAX_REMAPS          2048
#define DM_REMAP_V4_REDUNDANT_COPIES    5
#define DM_REMAP_V4_COPY_SECTORS        {0, 1024, 2048, 4096, 8192}

/* Health scoring constants */
#define DM_REMAP_HEALTH_PERFECT         100
#define DM_REMAP_HEALTH_GOOD            80
#define DM_REMAP_HEALTH_WARNING         60
#define DM_REMAP_HEALTH_CRITICAL        40
#define DM_REMAP_HEALTH_FAILING         20

/* Debug logging macro */
#define DMR_DEBUG(level, fmt, args...) \
    do { \
        if (dm_remap_debug >= level) \
            printk(KERN_INFO "dm-remap-v4: " fmt "\n", ##args); \
    } while (0)

/* External debug level variable */
extern int dm_remap_debug;

/**
 * Pure v4.0 Metadata Structure - No Legacy Baggage
 * 
 * Total size: ~24KB (vs 32KB with v3.0 compatibility)
 * Optimized layout for cache performance and minimal I/O
 */
struct dm_remap_metadata_v4 {
    /* Streamlined header with single checksum */
    struct {
        uint32_t magic;                 /* 0x444D5234 (DMR4) */
        uint32_t version;               /* Always 4 (no version detection needed) */
        uint64_t sequence_number;       /* For conflict resolution */
        uint64_t timestamp;             /* Creation/update time */
        uint32_t metadata_checksum;     /* Single CRC32 for entire structure */
        uint32_t copy_index;            /* Which redundant copy (0-4) */
        uint32_t structure_size;        /* Total metadata size */
        uint32_t reserved;              /* Future header expansion */
    } header __attribute__((packed));
    
    /* Device identification and configuration */
    struct {
        char main_device_uuid[37];      /* Main device UUID */
        char spare_device_uuid[37];     /* Spare device UUID */
        uint64_t main_device_sectors;   /* Main device size */
        uint64_t spare_device_sectors;  /* Spare device size */
        uint32_t sector_size;           /* Device sector size */
        uint32_t remap_capacity;        /* Max remaps supported */
        uint8_t device_fingerprint[32]; /* SHA-256 device fingerprint */
        char device_model[64];          /* Device model for validation */
    } device_config __attribute__((packed));
    
    /* Health monitoring and scanning */
    struct {
        uint64_t last_full_scan;        /* Last complete scan timestamp */
        uint64_t next_scheduled_scan;   /* Next scan time */
        uint32_t health_score;          /* Overall health (0-100) */
        uint32_t scan_progress_percent; /* Current scan progress */
        uint32_t total_errors_found;    /* Lifetime error count */
        uint32_t predictive_remaps;     /* Proactive remaps created */
        uint32_t scan_interval_hours;   /* Configured scan interval */
        uint32_t scan_flags;            /* Scanning configuration */
        
        /* Scan statistics */
        struct {
            uint32_t sectors_scanned;
            uint32_t errors_detected;
            uint32_t slow_sectors_found;
            uint32_t scan_interruptions;
        } scan_stats;
    } health_data __attribute__((packed));
    
    /* Simplified remap table (direct mapping, no indirection) */
    struct {
        uint32_t active_remaps;         /* Current number of remaps */
        uint32_t max_remaps;            /* Maximum capacity */
        uint32_t next_spare_sector;     /* Next available spare sector */
        uint32_t remap_flags;           /* Remap behavior flags */
        
        /* Direct remap entries (no complex indexing) */
        struct {
            uint64_t original_sector;   /* Failing sector */
            uint64_t spare_sector;      /* Replacement sector */
            uint64_t remap_timestamp;   /* When remap was created */
            uint32_t access_count;      /* Usage counter */
            uint32_t error_count;       /* Errors on original sector */
            uint16_t remap_reason;      /* Why remap was created */
            uint16_t flags;             /* Remap-specific flags */
        } remaps[DM_REMAP_V4_MAX_REMAPS];
    } remap_data __attribute__((packed));
    
    /* Future expansion without breaking compatibility */
    struct {
        uint32_t expansion_version;     /* For future v4.x features */
        uint32_t expansion_size;        /* Bytes used in expansion area */
        uint8_t expansion_data[2048];   /* Reserved for v4.1, v4.2, etc. */
    } future_expansion __attribute__((packed));
} __attribute__((packed));

/**
 * Background Health Scanner Structure
 */
struct dm_remap_background_scanner {
    /* Configuration */
    uint32_t scan_interval_seconds;     /* Full scan interval (default: 86400 = 24h) */
    uint32_t scan_chunk_sectors;        /* Sectors per scan iteration (default: 1024) */
    uint32_t max_io_latency_ms;         /* Max allowed I/O latency during scan */
    uint32_t scan_priority;             /* Scan priority (0=lowest, 10=highest) */
    bool enabled;                       /* Scanner enabled/disabled */
    
    /* Runtime state */
    struct workqueue_struct *scan_wq;   /* Dedicated work queue */
    struct delayed_work scan_work;      /* Delayed work structure */
    struct dm_remap_device_v4 *target;  /* Current scan target */
    
    /* Progress tracking */
    uint64_t current_sector;            /* Current scan position */
    uint64_t total_sectors;             /* Total device sectors */
    uint64_t last_scan_start;           /* Last full scan start time */
    uint64_t last_scan_complete;        /* Last full scan completion time */
    uint32_t scan_progress_percent;     /* Current scan progress (0-100) */
    
    /* Performance monitoring */
    struct {
        atomic64_t sectors_scanned;
        atomic64_t scan_time_ms;
        atomic64_t io_errors_detected;
        atomic64_t predictive_remaps;
        atomic64_t scan_interruptions;
    } stats;
    
    /* Health analysis */
    struct {
        uint32_t overall_health_score;  /* 0-100 health score */
        uint32_t error_sectors_found;   /* Number of problematic sectors */
        uint32_t slow_sectors_found;    /* Number of slow-responding sectors */
        uint64_t last_health_update;    /* Last health score calculation */
    } health;
    
    /* Synchronization */
    struct mutex scan_mutex;            /* Protects scanner state */
    atomic_t scan_active;               /* Atomic scan activity flag */
};

/**
 * Device Fingerprint Structure for Identification
 */
struct dm_remap_device_fingerprint {
    /* Hardware identification */
    struct {
        char device_uuid[37];           /* Device UUID (GPT/filesystem) */
        char serial_number[64];         /* Hardware serial number */
        char model_name[64];            /* Device model identifier */
        uint64_t device_size_sectors;   /* Device size for validation */
        uint32_t sector_size;           /* Logical sector size */
    } hardware;
    
    /* Filesystem identification */
    struct {
        char fs_uuid[37];               /* Filesystem UUID */
        char fs_type[16];               /* Filesystem type (ext4, xfs, etc.) */
        uint64_t fs_size;               /* Filesystem size */
        uint32_t partition_table_crc;   /* Partition table signature */
    } filesystem;
    
    /* Content identification */
    struct {
        uint8_t sector_hash[32];        /* SHA-256 of specific sectors */
        uint64_t creation_timestamp;    /* Fingerprint creation time */
        uint32_t fingerprint_version;   /* Fingerprint format version */
        uint8_t dm_remap_signature[16]; /* dm-remap metadata signature */
    } content;
    
    /* Overall fingerprint */
    uint8_t composite_hash[32];         /* SHA-256 of all above data */
};

/**
 * Main dm-remap v4.0 Device Structure
 */
struct dm_remap_device_v4 {
    /* Device references */
    struct block_device *main_dev;      /* Main storage device */
    struct block_device *spare_dev;     /* Spare device for remapping */
    
    /* Device identification */
    struct dm_remap_device_fingerprint fingerprint;
    
    /* Metadata management */
    struct dm_remap_metadata_v4 metadata;
    struct mutex metadata_mutex;
    bool metadata_dirty;
    
    /* Background health scanning */
    struct dm_remap_background_scanner scanner;
    
    /* Performance monitoring */
    struct {
        atomic64_t read_count;
        atomic64_t write_count;
        atomic64_t remap_count;
        atomic64_t error_count;
        atomic64_t total_latency_ns;
    } stats;
    
    /* Device state */
    atomic_t device_active;
    struct list_head device_list;
};

/* Core v4.0 Metadata Functions */
int dm_remap_read_metadata_v4(struct block_device *bdev, 
                              struct dm_remap_metadata_v4 *metadata);

int dm_remap_write_metadata_v4(struct block_device *bdev,
                               struct dm_remap_metadata_v4 *metadata);

int dm_remap_repair_metadata_v4(struct block_device *bdev);

void dm_remap_init_metadata_v4(struct dm_remap_metadata_v4 *metadata,
                               const char *main_device_uuid,
                               const char *spare_device_uuid,
                               uint64_t main_device_sectors,
                               uint64_t spare_device_sectors);

/* Background Health Scanner Functions */
int dm_remap_scanner_init(struct dm_remap_background_scanner *scanner,
                         struct dm_remap_device_v4 *device);

int dm_remap_scanner_start(struct dm_remap_background_scanner *scanner);

void dm_remap_scanner_stop(struct dm_remap_background_scanner *scanner);

void dm_remap_scanner_cleanup(struct dm_remap_background_scanner *scanner);

/* Device Discovery Functions */
int dm_remap_generate_fingerprint(struct block_device *bdev,
                                 struct dm_remap_device_fingerprint *fingerprint);

int dm_remap_validate_fingerprint(struct block_device *bdev,
                                 const struct dm_remap_device_fingerprint *fingerprint);

int dm_remap_discover_devices_v4(void);

/* Device Management Functions */
struct dm_remap_device_v4 *dm_remap_create_device_v4(struct block_device *main_dev,
                                                     struct block_device *spare_dev);

void dm_remap_destroy_device_v4(struct dm_remap_device_v4 *device);

int dm_remap_add_remap_v4(struct dm_remap_device_v4 *device,
                         uint64_t original_sector, uint64_t spare_sector,
                         uint16_t reason);

/* Module Initialization Functions */
int dm_remap_metadata_v4_init(void);
void dm_remap_metadata_v4_cleanup(void);

int dm_remap_health_v4_init(void);
void dm_remap_health_v4_cleanup(void);

int dm_remap_discovery_v4_init(void);
void dm_remap_discovery_v4_cleanup(void);

/* ========================================================================
 * v4.1 Async Metadata I/O API
 * ======================================================================== */

/**
 * struct dm_remap_async_metadata_context - Async metadata write context
 * 
 * Tracks state for an in-flight async metadata write operation.
 * Used for cancellation and completion signaling.
 */
struct dm_remap_async_metadata_context {
	atomic_t copies_pending;        /* Number of bio copies still in-flight */
	atomic_t write_cancelled;       /* Set to 1 if write should be aborted */
	atomic_t error_occurred;        /* Set to non-zero error code if any copy fails */
	struct completion all_copies_done; /* Signaled when all 5 copies complete or cancel */
	
	/* Timeout handling */
	unsigned long timeout_jiffies;  /* When operation times out */
	bool timeout_expired;           /* Timeout expiration flag */
	
	/* For cleanup */
	struct bio *bios[5];            /* Bio pointers for cleanup */
	struct page *pages[5];          /* Page pointers for cleanup */
};

/**
 * dm_remap_write_metadata_v4_async - Write metadata asynchronously
 * @bdev: Block device to write metadata to (spare device)
 * @metadata: Metadata structure to write
 * @context: Async context for tracking completion/cancellation
 * 
 * Submits metadata writes to all 5 redundant locations without blocking.
 * Caller must wait on context->all_copies_done or call cancel function.
 * 
 * Returns: 0 on successful submission, negative error if submission fails
 */
int dm_remap_write_metadata_v4_async(struct block_device *bdev,
                                     struct dm_remap_metadata_v4 *metadata,
                                     struct dm_remap_async_metadata_context *context);

/**
 * dm_remap_cancel_metadata_write - Cancel in-flight async metadata write
 * @context: Async context for the write to cancel
 * 
 * Sets cancellation flag and waits for all in-flight bios to complete.
 * Safe to call from presuspend hook.
 * 
 * Returns: 0 on successful cancellation, -ETIMEDOUT if timeout occurs
 */
int dm_remap_cancel_metadata_write(struct dm_remap_async_metadata_context *context);

/**
 * dm_remap_wait_metadata_write - Wait for async metadata write completion
 * @context: Async context to wait on
 * @timeout_ms: Maximum time to wait in milliseconds (0 = infinite)
 * 
 * Waits for all copies to complete successfully or any error to occur.
 * 
 * Returns: 0 on success, negative error code on failure or timeout
 */
int dm_remap_wait_metadata_write(struct dm_remap_async_metadata_context *context,
                                 unsigned int timeout_ms);

/**
 * dm_remap_init_async_context - Initialize async metadata context
 * @context: Context to initialize
 * 
 * Must be called before using context with async write functions.
 */
void dm_remap_init_async_context(struct dm_remap_async_metadata_context *context);

/**
 * dm_remap_cleanup_async_context - Clean up async metadata context
 * @context: Context to clean up
 * 
 * Frees any remaining resources. Safe to call even if write was cancelled.
 */
void dm_remap_cleanup_async_context(struct dm_remap_async_metadata_context *context);

#endif /* DM_REMAP_V4_H */