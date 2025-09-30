/*
 * dm-remap-core.h - Core data structures and definitions for dm-remap
 * 
 * This file defines the fundamental data structures used by the dm-remap
 * device mapper target. It provides the foundation for bad sector remapping
 * functionality.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_CORE_H
#define DM_REMAP_CORE_H

#include <linux/types.h>          /* For sector_t, basic types */
#include <linux/spinlock.h>       /* For spinlock_t */
#include <linux/device-mapper.h>  /* For struct dm_dev, dm_target */
#include <linux/kobject.h>        /* For struct kobject (sysfs support) */

/*
 * v2.0 REMAP REASONS - Why a sector was remapped
 */
#define DMR_REMAP_MANUAL     0  /* Manually remapped via message interface */
#define DMR_REMAP_WRITE_ERR  1  /* Automatic remap due to write error */
#define DMR_REMAP_READ_ERR   2  /* Automatic remap due to read error */
#define DMR_REMAP_PROACTIVE  3  /* Proactive remap during health scan */

/*
 * v2.0 HEALTH STATUS - Current assessment of sector health
 */
#define DMR_HEALTH_UNKNOWN   0  /* Health status not yet determined */
#define DMR_HEALTH_GOOD      1  /* Sector appears healthy */
#define DMR_HEALTH_SUSPECT   2  /* Some errors but still usable */
#define DMR_HEALTH_BAD       3  /* Sector is unreliable, should be remapped */
#define DMR_HEALTH_REMAPPED  4  /* Sector has been remapped */

/*
 * REMAP_ENTRY - Represents a single bad sector remapping (v2.0 Enhanced)
 * 
 * This structure stores the mapping between a bad sector on the main device
 * and its replacement sector on the spare device. v2.0 adds intelligence
 * tracking for automatic bad sector detection and health monitoring.
 */
struct remap_entry {
    sector_t main_lba;    /* Original bad sector number on main device */
                         /* Set to (sector_t)-1 if this entry is unused */
    
    sector_t spare_lba;   /* Replacement sector number on spare device */
                         /* Always valid - pre-calculated during target creation */
    
    /* v2.0 Intelligence & Health Tracking */
    u32 error_count;      /* Number of I/O errors detected on this sector */
    u32 access_count;     /* Total number of I/O operations to this sector */
    u64 last_error_time;  /* Timestamp of last error (jiffies) */
    u8 remap_reason;      /* Why this sector was remapped (DMR_REMAP_*) */
    u8 health_status;     /* Current health assessment (DMR_HEALTH_*) */
    u16 reserved;         /* Reserved for future expansion */
};

/*
 * REMAP_IO_CTX - Per-I/O context for tracking operations (v2.0 Enhanced)
 * 
 * This structure is embedded in each bio's per-target data area.
 * v2.0 adds comprehensive error handling and retry logic tracking.
 */
struct remap_io_ctx {
    sector_t original_lba;      /* Original logical block address */
    sector_t current_lba;       /* Current target (may be remapped) */
    struct dm_target *ti;       /* Target pointer for error handling */
    
    /* I/O Operation Tracking */
    unsigned int operation;     /* REQ_OP_READ, REQ_OP_WRITE, etc. */
    u32 retry_count;           /* Number of retry attempts made */
    u64 start_time;            /* When this I/O started (jiffies) */
    
    /* v2.0 Error Handling State */
    bool was_write;            /* True if this was a write operation */
    bool is_retry;             /* True if this is a retry attempt */
    bool try_spare_on_error;   /* Try spare device if main device fails */
    bool auto_remap_candidate; /* This sector is candidate for auto-remap */
    
    /* Error Recovery */
    int last_error;            /* Last error code encountered */
    u8 error_flags;            /* Bit flags for error types encountered */
};

/*
 * REMAP_C - Main target context structure
 * 
 * This is the central data structure that contains all state for a single
 * dm-remap target instance. Each target (created via dmsetup) gets its own
 * instance of this structure.
 */
struct remap_c {
    /* Device references - managed by device mapper framework */
    struct dm_dev *main_dev;     /* Primary block device (where bad sectors occur) */
    struct dm_dev *spare_dev;    /* Spare block device (where remapped sectors go) */
    
    /* Spare area configuration */
    sector_t spare_start;        /* First sector number in spare area */
    sector_t spare_len;          /* Number of sectors available in spare area */
    sector_t spare_used;         /* Number of spare sectors currently used */
    sector_t main_start;         /* Starting sector on main device (usually 0) */
    
    /* Remapping table - dynamically allocated array */
    struct remap_entry *table;   /* Array of remap entries */
                                /* Size is spare_len (one entry per spare sector) */
    
    /* v2.0 Intelligence & Statistics */
    u32 write_errors;           /* Total write errors detected */
    u32 read_errors;            /* Total read errors detected */
    u32 auto_remaps;            /* Number of automatic remappings */
    u32 manual_remaps;          /* Number of manual remappings */
    u32 scan_progress;          /* Health scan progress (percentage) */
    u64 last_scan_time;         /* Last health scan timestamp */
    
    /* v2.0 Health Assessment */
    u8 overall_health;          /* Overall device health (DMR_HEALTH_*) */
    bool auto_remap_enabled;    /* Enable automatic remapping on errors */
    bool background_scan;       /* Enable background health scanning */
    u8 error_threshold;         /* Error count threshold for auto-remap */
    
    /* v2.0 Sysfs Interface */
    struct kobject kobj;        /* Kernel object for sysfs representation */
    
    /* Concurrency control */
    spinlock_t lock;            /* Protects table and statistics */
                               /* Must be held when reading/writing remap table */
};

/*
 * Debug and statistics counters
 * These are global counters for monitoring system-wide behavior
 */
extern unsigned long dmr_clone_shallow_count;  /* Number of shallow bio clones */
extern unsigned long dmr_clone_deep_count;     /* Number of deep bio clones */

/*
 * v2.0 CONFIGURATION CONSTANTS
 */
#define DMR_DEFAULT_ERROR_THRESHOLD  3    /* Auto-remap after 3 errors */
#define DMR_ERROR_TIME_WINDOW        300   /* Consider errors in 5min window (seconds) */
#define DMR_MAX_RETRIES              3    /* Maximum retry attempts before giving up */
#define DMR_HEALTH_SCAN_INTERVAL     3600  /* Background scan every hour (seconds) */

/*
 * v2.0 OVERALL DEVICE HEALTH STATES
 */
#define DMR_DEVICE_HEALTH_EXCELLENT  0  /* No errors detected */
#define DMR_DEVICE_HEALTH_GOOD       1  /* Few errors, well within limits */
#define DMR_DEVICE_HEALTH_FAIR       2  /* Some sectors showing problems */
#define DMR_DEVICE_HEALTH_POOR       3  /* Many bad sectors, consider replacement */
#define DMR_DEVICE_HEALTH_CRITICAL   4  /* Spare area nearly full */

/*
 * v2.0 HELPER MACROS
 */
#define DMR_IS_REMAPPED_ENTRY(entry) ((entry)->main_lba != (sector_t)-1)
#define DMR_ENTRY_AGE_SECONDS(entry) ((jiffies - (entry)->last_error_time) / HZ)
#define DMR_SHOULD_AUTO_REMAP(rc, entry) \
    ((rc)->auto_remap_enabled && \
     (entry)->error_count >= (rc)->error_threshold && \
     (entry)->health_status != DMR_HEALTH_REMAPPED)

/*
 * Module parameters - can be set at module load time or changed at runtime
 */
extern int debug_level;         /* 0=quiet, 1=info, 2=debug */
extern int max_remaps;          /* Maximum remaps per target */
extern int auto_remap_enabled;  /* v2.0: Enable automatic remapping */
extern int error_threshold;     /* v2.0: Error count threshold for auto-remap */

/*
 * Debug logging macro
 * 
 * Usage: DMR_DEBUG(1, "Remapping sector %llu", sector_number);
 * 
 * This macro only generates code if debug_level is high enough,
 * making it safe to use in performance-critical paths.
 */
#define DMR_DEBUG(level, fmt, args...) \
    do { \
        if (debug_level >= (level)) \
            printk(KERN_INFO "dm-remap: " fmt "\n", ##args); \
    } while (0)

/*
 * Error logging macro - always outputs
 */
#define DMR_ERROR(fmt, args...) \
    printk(KERN_ERR "dm-remap: ERROR: " fmt "\n", ##args)

/*
 * Helper macro for per-bio data access
 * 
 * The device mapper framework provides a small per-bio data area.
 * This macro safely casts it to our context structure type.
 */
#define dmr_per_bio_data(bio, type) \
    ((type *)dm_per_bio_data(bio, sizeof(type)))

/*
 * Module identification strings
 */
#define DM_MSG_PREFIX "dm_remap"
#define DMR_VERSION "1.1"

/*
 * Return values for dm_target.map function
 * These tell the device mapper framework how we handled the bio
 */
/* DM_MAPIO_SUBMITTED - we submitted the bio ourselves (via bio cloning) */
/* DM_MAPIO_REMAPPED - we modified the bio and want DM to submit it */
/* DM_MAPIO_REQUEUE - temporary failure, please retry this bio */

#endif /* DM_REMAP_CORE_H */