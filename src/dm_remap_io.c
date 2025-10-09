/*
 * dm-remap-io.c - Enhanced I/O processing for dm-remap v2.0
 * 
 * This file implements the intelligent I/O processing pipeline that
 * detects errors, performs retries, and triggers automatic remapping.
 * 
 * KEY v2.0 FEATURES:
 * - Bio endio callbacks for error detection
 * - Retry logic with exponential backoff  
 * - Automatic bad sector remapping
 * - Health monitoring and statistics
 * - Deferred work for non-atomic operations
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>         /* Core kernel module support */
#include <linux/bio.h>           /* Bio structures and functions */
#include <linux/blkdev.h>        /* Block device interfaces */
#include <linux/device-mapper.h> /* Device mapper framework */
#include <linux/delay.h>         /* msleep for retry delays */
#include <linux/workqueue.h>     /* Work queue support */
#include <linux/slab.h>          /* Memory allocation */

#include "dm-remap-core.h"       /* Core data structures */
#include "dm-remap-error.h"      /* Error handling interfaces */
#include "dm-remap-io.h"         /* I/O processing interfaces */
#include "dm-remap-performance.h" /* Performance optimization functions */
#include "dm-remap-hotpath-optimization.h" /* Week 9-10 Hotpath optimization */
#include "dm-remap-io-optimized.h" /* Phase 3.2B: Optimized I/O processing */

/*
 * Work structure for deferred auto-remapping operations
 * 
 * Auto-remapping cannot be done in bio endio context (atomic context),
 * so we defer it to a work queue for safe execution.
 */
struct auto_remap_work {
    struct work_struct work;     /* Kernel work structure */
    struct remap_c *rc;          /* Target context */
    sector_t lba;                /* Sector to remap */
    int error_code;              /* Original error that triggered remap */
};

/* Bio context structure is now defined in dm-remap-io.h */

/* Work queue for deferred auto-remapping operations */
/*
 * Auto-remap work queue for background operations
 */
static struct workqueue_struct *auto_remap_wq;

/* Forward declarations */
static void dmr_schedule_auto_remap(struct remap_c *rc, sector_t lba, int error_code);



/*
 * dmr_auto_remap_worker() - Work queue handler for automatic remapping
 * 
 * This function runs in process context and can safely perform
 * operations that might block or allocate memory.
 * 
 * @work: Work structure containing remapping parameters
 */
static void dmr_auto_remap_worker(struct work_struct *work)
{
    struct auto_remap_work *arw = container_of(work, struct auto_remap_work, work);
    struct remap_c *rc = arw->rc;
    sector_t lba = arw->lba;
    int ret;
    
    DMR_DEBUG(1, "Auto-remap worker processing sector %llu", 
              (unsigned long long)lba);
    
    /* Check if this sector should be auto-remapped */
    if (dmr_should_auto_remap(rc, lba)) {
        ret = dmr_perform_auto_remap(rc, lba);
        if (ret == 0) {
            DMR_DEBUG(0, "Successfully auto-remapped sector %llu", 
                      (unsigned long long)lba);
        } else {
            DMR_DEBUG(0, "Failed to auto-remap sector %llu: %d", 
                      (unsigned long long)lba, ret);
        }
    }
    
    /* Clean up work structure */
    kfree(arw);
}

/*
 * dmr_schedule_auto_remap() - Schedule automatic remapping for a sector
 * 
 * This function schedules deferred auto-remapping work for a sector
 * that has experienced errors.
 * 
 * @rc: Target context
 * @lba: Logical block address to potentially remap
 * @error_code: Error code that triggered this request
 */
/*
 * dmr_schedule_auto_remap() - Schedule automatic remapping work
 * 
 * This function schedules background work to perform automatic remapping
 * of a sector that has experienced too many errors.
 * 
 * @rc: Target context
 * @lba: Logical block address to remap
 * @error_code: The error that triggered this remap
 */
static void dmr_schedule_auto_remap(struct remap_c *rc, sector_t lba, int error_code)
{
    struct auto_remap_work *arw;
    
    /* Don't schedule work if auto-remap is disabled */
    if (!rc->auto_remap_enabled)
        return;
    
    /* Allocate work structure */
    arw = kzalloc(sizeof(*arw), GFP_ATOMIC);  /* Must use GFP_ATOMIC in endio */
    if (!arw) {
        DMR_DEBUG(0, "Failed to allocate auto-remap work for sector %llu", 
                  (unsigned long long)lba);
        return;
    }
    
    /* Initialize work structure */
    INIT_WORK(&arw->work, dmr_auto_remap_worker);
    arw->rc = rc;
    arw->lba = lba;
    arw->error_code = error_code;
    
    /* Queue the work */
    queue_work(auto_remap_wq, &arw->work);
    
    DMR_DEBUG(2, "Scheduled auto-remap work for sector %llu", 
              (unsigned long long)lba);
}

/*
 * dmr_bio_endio() - Intelligent bio completion callback for v2.0 error handling
 * 
 * This is the heart of the v2.0 intelligent error detection system.
 * It analyzes I/O completion status, updates health statistics,
 * and triggers automatic remapping when necessary.
 * 
 * @bio: The completed bio
 */  
static void dmr_bio_endio(struct bio *bio)
{
    struct dmr_bio_context *ctx = bio->bi_private;
    struct remap_c *rc = ctx->rc;
    sector_t lba = ctx->original_lba;
    int error = bio->bi_status;
    bool is_write = bio_data_dir(bio) == WRITE;
    
    /* DEBUG: Confirm bio completion callback is being called */
    DMR_DEBUG(2, "dmr_bio_endio called: sector=%llu, error=%d, %s", 
              (unsigned long long)lba, error, is_write ? "WRITE" : "READ");
    
    /* Update health statistics for this sector */
    dmr_update_sector_health(rc, lba, (error != 0), error);
    
    /* Update global error counters */
    if (error) {
        if (is_write) {
            rc->write_errors++;
            global_write_errors++;  /* Global counter for testing */
        } else {
            rc->read_errors++;
            global_read_errors++;   /* Global counter for testing */
        }
            
        DMR_DEBUG(1, "I/O error %d on sector %llu (%s)", 
                  error, (unsigned long long)lba, is_write ? "write" : "read");
    }
    
    /* Check if auto-remapping should be triggered */
    if (error && rc->auto_remap_enabled && dmr_should_auto_remap(rc, lba)) {
        dmr_schedule_auto_remap(rc, lba, error);
    }
    
    /* Restore original bio completion info */
    bio->bi_end_io = ctx->original_bi_end_io;
    bio->bi_private = ctx->original_bi_private;
    
    /* Free our context */
    kfree(ctx);
    
    /* Call original completion handler */
    if (bio->bi_end_io)
        bio->bi_end_io(bio);
    else
        bio_endio(bio);
}



/*
 * dmr_setup_bio_tracking() - Setup bio for v2.0 error tracking
 * 
 * This function sets up a bio with the necessary context and callbacks
 * for v2.0 error detection and retry logic.
 * 
 * @bio: Bio to setup
 * @rc: Target context
 * @lba: Original logical block address
 */
void dmr_setup_bio_tracking(struct bio *bio, struct remap_c *rc, sector_t lba)
{
    struct dmr_bio_context *ctx;
    
    DMR_DEBUG(3, "Setup bio tracking for sector %llu", (unsigned long long)lba);
    
    /* Bio tracking enabled for both READ and WRITE operations for error detection */
    
    /* Track I/Os up to 64KB to handle kernel bio coalescing */
    if (bio->bi_iter.bi_size > 65536) {
        DMR_DEBUG(3, "Skipping tracking for very large bio (%u bytes)", bio->bi_iter.bi_size);
        return;
    }
    
    DMR_DEBUG(3, "Tracking bio: %u bytes starting at sector %llu", 
              bio->bi_iter.bi_size, (unsigned long long)lba);
    
    /* Allocate context for tracking this bio */
    ctx = kzalloc(sizeof(*ctx), GFP_NOIO);
    if (!ctx) {
        /* If we can't allocate context, continue without tracking */
        DMR_DEBUG(1, "Failed to allocate bio context for sector %llu", (unsigned long long)lba);
        return;
    }
    
    /* Initialize context */
    ctx->rc = rc;
    ctx->original_lba = lba;
    ctx->retry_count = 0;
    ctx->start_time = jiffies;
    
    /* Store original bio completion info */
    ctx->original_bi_end_io = bio->bi_end_io;
    ctx->original_bi_private = bio->bi_private;
    
    /* Set up our completion callback */
    bio->bi_end_io = dmr_bio_endio;
    bio->bi_private = ctx;
    
    DMR_DEBUG(3, "Bio tracking enabled for sector %llu", (unsigned long long)lba);
}

/*
 * remap_map() - Enhanced v2.0 I/O mapping with error handling
 * 
 * This function extends the basic remapping logic with v2.0 intelligence
 * features like health monitoring and automatic error detection setup.
 * 
 * @ti: Device mapper target
 * @bio: Bio to process
 * 
 * Returns: DM_MAPIO_* result code
 */
int remap_map(struct dm_target *ti, struct bio *bio)
{
    /* Phase 3.2B: Use optimized I/O processing */
    return dmr_io_optimized_process(ti, bio);
}

/*
 * dmr_io_init() - Initialize I/O processing subsystem
 * 
 * This function initializes the work queue and other resources
 * needed for v2.0 I/O processing.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_io_init(void)
{
    /* Create work queue for auto-remapping operations */
    auto_remap_wq = alloc_workqueue("dmr_auto_remap", WQ_MEM_RECLAIM, 0);
    if (!auto_remap_wq) {
        DMR_DEBUG(0, "Failed to create auto-remap work queue");
        return -ENOMEM;
    }
    
    DMR_DEBUG(1, "Initialized v2.0 I/O processing subsystem");
    return 0;
}

/*
 * dmr_io_exit() - Cleanup I/O processing subsystem
 * 
 * This function cleans up resources used by the I/O processing subsystem.
 */
void dmr_io_exit(void)
{
    if (auto_remap_wq) {
        flush_workqueue(auto_remap_wq);
        destroy_workqueue(auto_remap_wq);
        auto_remap_wq = NULL;
    }
    
    DMR_DEBUG(1, "Cleaned up v2.0 I/O processing subsystem");
}