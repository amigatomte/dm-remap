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

/*
 * REMAP_ENTRY - Represents a single bad sector remapping
 * 
 * This structure stores the mapping between a bad sector on the main device
 * and its replacement sector on the spare device.
 */
struct remap_entry {
    sector_t main_lba;    /* Original bad sector number on main device */
                         /* Set to (sector_t)-1 if this entry is unused */
    
    sector_t spare_lba;   /* Replacement sector number on spare device */
                         /* Always valid - pre-calculated during target creation */
};

/*
 * REMAP_IO_CTX - Per-I/O context for tracking operations
 * 
 * This structure is embedded in each bio's per-target data area.
 * It tracks information needed for error handling and retry logic.
 */
struct remap_io_ctx {
    sector_t lba;            /* Logical block address being accessed */
    bool was_write;          /* True if this was a write operation */
    bool retry_to_spare;     /* True if we should retry failed ops to spare */
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
    
    /* Concurrency control */
    spinlock_t lock;            /* Protects table and spare_used counter */
                               /* Must be held when reading/writing remap table */
};

/*
 * Debug and statistics counters
 * These are global counters for monitoring system-wide behavior
 */
extern unsigned long dmr_clone_shallow_count;  /* Number of shallow bio clones */
extern unsigned long dmr_clone_deep_count;     /* Number of deep bio clones */

/*
 * Module parameters - can be set at module load time or changed at runtime
 */
extern int debug_level;    /* 0=quiet, 1=info, 2=debug */
extern int max_remaps;     /* Maximum remaps per target */

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