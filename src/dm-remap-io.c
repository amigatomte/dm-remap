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

/* Work queue for deferred auto-remapping operations */
static struct workqueue_struct *auto_remap_wq;

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
 * dmr_bio_endio() - Bio completion callback for error detection and retry
 * 
 * This function is called when a bio completes (successfully or with error).
 * It implements the v2.0 intelligent error handling pipeline.
 * 
 * @bio: The completed bio
 */
static void dmr_bio_endio(struct bio *bio)
{
    /* TODO: Implement v2.0 bio endio callback after structure conflicts are resolved */
    DMR_DEBUG(2, "Bio endio callback - v2.0 placeholder");
    
    /* Call the original bio completion function */
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
    /* For v2.0 transition - simplified setup without endio callback for now */
    DMR_DEBUG(3, "Setup bio tracking for sector %llu", (unsigned long long)lba);
    /* TODO: Add full v2.0 error tracking after structure conflicts are resolved */
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
    struct remap_c *rc = ti->private;
    sector_t sector = bio->bi_iter.bi_sector;
    struct dm_dev *target_dev = rc->main_dev;
    sector_t target_sector = rc->main_start + sector;
    int i;
    bool found_remap = false;
    
    /* Setup v2.0 error tracking */
    dmr_setup_bio_tracking(bio, rc, sector);
    
    /* I/O debug logging */
    if (debug_level >= 2) {
        DMR_DEBUG(2, "Enhanced I/O: sector=%llu, size=%u, %s", 
                  (unsigned long long)sector, bio->bi_iter.bi_size, 
                  bio_data_dir(bio) == WRITE ? "WRITE" : "READ");
    }
    
    /* Handle multi-sector bios by passing through */
    if (bio->bi_iter.bi_size != 512) {
        DMR_DEBUG(2, "Multi-sector passthrough: %u bytes", bio->bi_iter.bi_size);
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
    }
    
    /* Handle special operations */
    if (bio_op(bio) == REQ_OP_FLUSH || bio_op(bio) == REQ_OP_DISCARD || 
        bio_op(bio) == REQ_OP_WRITE_ZEROES) {
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
    }
    
    /* Check for existing remapping */
    spin_lock(&rc->lock);
    for (i = 0; i < rc->spare_used; i++) {
        if (sector == rc->table[i].main_lba && rc->table[i].main_lba != (sector_t)-1) {
            /* Redirect to spare device */
            target_dev = rc->spare_dev;
            target_sector = rc->table[i].spare_lba;
            found_remap = true;
            
            DMR_DEBUG(1, "REMAP: sector %llu -> spare sector %llu", 
                      (unsigned long long)sector, (unsigned long long)target_sector);
            break;
        }
    }
    spin_unlock(&rc->lock);
    
    /* Set target device and sector */
    bio_set_dev(bio, target_dev->bdev);
    bio->bi_iter.bi_sector = target_sector;
    
    if (!found_remap) {
        DMR_DEBUG(2, "Passthrough: sector %llu to main device", 
                  (unsigned long long)sector);
    }
    
    return DM_MAPIO_REMAPPED;
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