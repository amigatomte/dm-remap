/*
 * dm-remap-v4-repair.c - Automatic metadata repair for dm-remap v4.2
 *
 * This module implements automatic background repair of corrupted metadata copies.
 * When corruption is detected during metadata reads, repairs are automatically
 * scheduled. Periodic scrubbing can also be enabled to proactively check all
 * metadata copies.
 *
 * Features:
 * - Automatic repair scheduling on corruption detection
 * - Asynchronous repair execution (no I/O path blocking)
 * - Periodic metadata scrubbing (configurable interval)
 * - Repair statistics and health monitoring
 * - Sysfs interface for control and status
 *
 * Part of v4.2: Automatic metadata rebuild and boot-time activation
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/blkdev.h>
#include "dm-remap-v4-compat.h"
#include "dm-remap-v4.h"

/* Forward declaration */
static void dm_remap_repair_work(struct work_struct *work);
static void dm_remap_periodic_scrub_work(struct work_struct *work);

/*
 * Initialize repair context for a device
 *
 * @ctx: Repair context to initialize
 * @spare_bdev: Spare block device with metadata
 * @repair_wq: Workqueue for repair operations
 *
 * Sets up work structures, initializes atomic flags and statistics,
 * and sets default configuration values.
 */
void dm_remap_init_repair_context(struct dm_remap_repair_context *ctx,
                                  struct block_device *spare_bdev,
                                  struct workqueue_struct *repair_wq)
{
    if (!ctx) {
        DMR_ERROR("init_repair_context: NULL context");
        return;
    }
    
    /* Initialize work structures */
    INIT_WORK(&ctx->repair_work, dm_remap_repair_work);
    INIT_DELAYED_WORK(&ctx->periodic_scrub_work, dm_remap_periodic_scrub_work);
    
    /* Initialize atomic state flags */
    atomic_set(&ctx->repair_in_progress, 0);
    atomic_set(&ctx->repairs_pending, 0);
    atomic_set(&ctx->scrub_enabled, 0);
    
    /* Initialize statistics */
    atomic64_set(&ctx->last_repair_time, 0);
    atomic64_set(&ctx->repairs_completed, 0);
    atomic64_set(&ctx->scrubs_completed, 0);
    atomic64_set(&ctx->corruption_detected, 0);
    
    /* Set default configuration */
    ctx->scrub_interval_seconds = 3600; /* 1 hour default */
    ctx->repair_retry_limit = 3;
    
    /* Store references */
    ctx->spare_bdev = spare_bdev;
    ctx->repair_wq = repair_wq;
    
    DMR_INFO("Repair context initialized (scrub interval: %u sec)",
             ctx->scrub_interval_seconds);
}
EXPORT_SYMBOL(dm_remap_init_repair_context);

/*
 * Cleanup repair context
 *
 * @ctx: Repair context to cleanup
 *
 * Cancels any pending work items and waits for in-flight repairs to complete.
 * Should be called during device destruction.
 */
void dm_remap_cleanup_repair_context(struct dm_remap_repair_context *ctx)
{
    if (!ctx) {
        DMR_ERROR("cleanup_repair_context: NULL context");
        return;
    }
    
    DMR_INFO("Cleaning up repair context (completed: %llu, scrubs: %llu)",
             atomic64_read(&ctx->repairs_completed),
             atomic64_read(&ctx->scrubs_completed));
    
    /* Disable scrubbing */
    atomic_set(&ctx->scrub_enabled, 0);
    
    /* Cancel delayed work */
    cancel_delayed_work_sync(&ctx->periodic_scrub_work);
    
    /* Cancel any pending repair work */
    cancel_work_sync(&ctx->repair_work);
    
    /* Wait for any in-flight repairs (should be quick as work is cancelled) */
    while (atomic_read(&ctx->repair_in_progress)) {
        msleep(10);
    }
    
    DMR_INFO("Repair context cleaned up");
}
EXPORT_SYMBOL(dm_remap_cleanup_repair_context);

/*
 * Schedule a metadata repair operation
 *
 * @ctx: Repair context
 *
 * Schedules asynchronous metadata repair if not already in progress.
 * Can be called from I/O path when corruption is detected.
 * Returns immediately without blocking.
 */
void dm_remap_schedule_metadata_repair(struct dm_remap_repair_context *ctx)
{
    if (!ctx) {
        DMR_ERROR("schedule_repair: NULL context");
        return;
    }
    
    /* Increment corruption counter */
    atomic64_inc(&ctx->corruption_detected);
    /* TODO: Update global sysfs stats when sysfs is refactored for v4 */
    
    /* Check if repair already in progress */
    if (atomic_read(&ctx->repair_in_progress)) {
        DMR_INFO("Repair already in progress, marking as pending");
        atomic_set(&ctx->repairs_pending, 1);
        return;
    }
    
    /* Schedule repair work on dedicated repair workqueue */
    if (ctx->repair_wq) {
        DMR_INFO("Scheduling metadata repair (corruption detected: %llu)",
                 atomic64_read(&ctx->corruption_detected));
        
        queue_work(ctx->repair_wq, &ctx->repair_work);
    } else {
        DMR_ERROR("Cannot schedule repair: workqueue NULL");
    }
}
EXPORT_SYMBOL(dm_remap_schedule_metadata_repair);

/*
 * Repair work function - executes asynchronously
 *
 * @work: Work structure (embedded in repair context)
 *
 * Calls the existing dm_remap_repair_metadata_v4() function to rebuild
 * corrupted metadata copies. Updates statistics and handles retries.
 */
static void dm_remap_repair_work(struct work_struct *work)
{
    struct dm_remap_repair_context *ctx;
    int ret;
    uint32_t retry_count = 0;
    
    ctx = container_of(work, struct dm_remap_repair_context, repair_work);
    if (!ctx || !ctx->spare_bdev) {
        DMR_ERROR("repair_work: Invalid context or spare device");
        return;
    }
    
    /* Set repair in progress flag */
    if (atomic_cmpxchg(&ctx->repair_in_progress, 0, 1) != 0) {
        DMR_ERROR("repair_work: Concurrent repair detected");
        return;
    }
    
    DMR_INFO("Starting metadata repair");
    
    /* Execute repair with retry logic */
    do {
        ret = dm_remap_repair_metadata_v4(ctx->spare_bdev);
        
        if (ret == 0) {
            /* Success */
            DMR_INFO("Metadata repair completed successfully");
            atomic64_inc(&ctx->repairs_completed);
            atomic64_set(&ctx->last_repair_time, ktime_get_real_seconds());
            /* TODO: Update global sysfs stats when sysfs is refactored for v4 */
            break;
        }
        
        retry_count++;
        DMR_WARN("Metadata repair attempt %u/%u failed: %d",
                 retry_count, ctx->repair_retry_limit, ret);
        
        if (retry_count < ctx->repair_retry_limit) {
            /* Wait before retry */
            msleep(1000 * retry_count); /* Exponential backoff */
        }
        
    } while (retry_count < ctx->repair_retry_limit);
    
    if (ret != 0) {
        DMR_ERROR("Metadata repair failed after %u attempts", retry_count);
    }
    
    /* Clear repair in progress flag */
    atomic_set(&ctx->repair_in_progress, 0);
    
    /* Check if another repair was requested while this one was running */
    if (atomic_cmpxchg(&ctx->repairs_pending, 1, 0) == 1) {
        DMR_INFO("Pending repair detected, scheduling another repair");
        queue_work(ctx->repair_wq, &ctx->repair_work);
    }
}

/*
 * Periodic scrub work function - executes asynchronously
 *
 * @work: Delayed work structure (embedded in repair context)
 *
 * Periodically reads and verifies all metadata copies, scheduling
 * repairs if corruption is detected. Reschedules itself if enabled.
 */
static void dm_remap_periodic_scrub_work(struct work_struct *work)
{
    struct dm_remap_repair_context *ctx;
    struct delayed_work *dwork;
    struct dm_remap_metadata_v4 *metadata;
    int ret;
    
    dwork = container_of(work, struct delayed_work, work);
    ctx = container_of(dwork, struct dm_remap_repair_context, periodic_scrub_work);
    
    if (!ctx || !ctx->spare_bdev) {
        DMR_ERROR("scrub_work: Invalid context or spare device");
        return;
    }
    
    /* Check if scrubbing is still enabled */
    if (!atomic_read(&ctx->scrub_enabled)) {
        DMR_INFO("Periodic scrubbing disabled, stopping");
        return;
    }
    
    /* Allocate metadata on heap to avoid stack overflow */
    metadata = kmalloc(sizeof(struct dm_remap_metadata_v4), GFP_KERNEL);
    if (!metadata) {
        DMR_ERROR("Failed to allocate memory for periodic scrub");
        /* Reschedule anyway - memory might be available next time */
        goto reschedule;
    }
    
    DMR_INFO("Starting periodic metadata scrub");
    
    /* Read metadata - this will detect corruption if present */
    ret = dm_remap_read_metadata_v4(ctx->spare_bdev, metadata);
    
    if (ret != 0) {
        DMR_WARN("Periodic scrub detected corruption: %d", ret);
        /* Schedule repair */
        dm_remap_schedule_metadata_repair(ctx);
    } else {
        DMR_INFO("Periodic scrub: metadata healthy");
    }
    
    kfree(metadata);
    
    atomic64_inc(&ctx->scrubs_completed);
    /* TODO: Update global sysfs stats when sysfs is refactored for v4 */
    
reschedule:
    /* Reschedule next scrub if still enabled */
    if (atomic_read(&ctx->scrub_enabled)) {
        unsigned long delay = msecs_to_jiffies(ctx->scrub_interval_seconds * 1000);
        queue_delayed_work(ctx->repair_wq, &ctx->periodic_scrub_work, delay);
        DMR_INFO("Next scrub scheduled in %u seconds", ctx->scrub_interval_seconds);
    }
}

MODULE_DESCRIPTION("Automatic metadata repair for dm-remap v4.2");
MODULE_AUTHOR("Your Name");
MODULE_LICENSE("GPL");
