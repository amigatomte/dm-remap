/*
 * dm-remap-io.h - Enhanced I/O processing interface for dm-remap v2.0
 * 
 * This header defines the interface for the intelligent I/O processing
 * system in dm-remap v2.0, including error handling and auto-remapping.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_IO_H
#define DM_REMAP_IO_H

#include <linux/bio.h>            /* For struct bio */
#include <linux/device-mapper.h>  /* For struct dm_target */
#include <linux/blkdev.h>         /* For sector_t */

/* Forward declarations */
struct remap_c;

/*
 * Bio context for v2.0 intelligent error handling
 * 
 * This structure tracks individual I/O operations for error detection,
 * retry logic, and automatic remapping decisions.
 */
struct dmr_bio_context {
    struct remap_c *rc;           /* Target context */
    sector_t original_lba;        /* Original logical block address */
    u32 retry_count;              /* Number of retries attempted */
    unsigned long start_time;     /* I/O start time (jiffies) */
    bio_end_io_t *original_bi_end_io;  /* Original completion callback */
    void *original_bi_private;    /* Original private data */
};

/*
 * Function prototypes for v2.0 I/O processing
 */

/* Main I/O mapping function - called by device mapper framework */
int remap_map(struct dm_target *ti, struct bio *bio);

/* Enhanced I/O mapping with v2.0 intelligence */
int dmr_enhanced_map(struct dm_target *ti, struct bio *bio);

/* Bio tracking setup for error detection */
void dmr_setup_bio_tracking(struct bio *bio, struct remap_c *rc, sector_t lba);

/* I/O subsystem initialization and cleanup */
int dmr_io_init(void);
void dmr_io_exit(void);

/* 
 * External variables that need to be accessible 
 * (defined in main dm-remap.c file)
 */
extern int debug_level;

/*
 * Debug macro for I/O module (uses external debug_level)
 */
#ifndef DMR_DEBUG
#define DMR_DEBUG(level, fmt, args...) \
    do { \
        if (debug_level >= level) \
            printk(KERN_INFO "dm-remap: " fmt "\n", ##args); \
    } while (0)
#endif

#endif /* DM_REMAP_IO_H */