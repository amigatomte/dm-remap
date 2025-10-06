/* dm-remap v4.0 Background Health Scanning System
 * 
 * Non-intrusive background health scanning with intelligent I/O scheduling
 * and predictive sector health assessment.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/ktime.h>
#include <linux/atomic.h>

#include "dm_remap_metadata_v4.h"

/* Health scanning configuration */
#define HEALTH_SCAN_IO_THRESHOLD        30      /* Max I/O load % for scanning */
#define HEALTH_SCAN_RETRY_DELAY         HZ      /* Retry delay when I/O busy */
#define HEALTH_SCAN_DEFAULT_INTERVAL    (24 * 3600)  /* 24 hours */

#define MIN_SCAN_BATCH_SECTORS          8       /* 4KB minimum batch */
#define MAX_SCAN_BATCH_SECTORS          256     /* 128KB maximum batch */
#define DEFAULT_SCAN_BATCH_SECTORS      64      /* 32KB default batch */

/* Health scanning work queue */
static struct workqueue_struct *dm_remap_health_wq;

/* Health scanning work item structure */
struct dm_remap_health_work {
    struct work_struct work;
    struct delayed_work delayed_work;
    struct dm_remap_context *dmrc;
    sector_t scan_start;
    sector_t scan_end;
    unsigned int scan_batch_size;
    bool is_retry;
};

/* I/O load tracking structure */
struct dm_remap_io_stats {
    atomic64_t read_ios;
    atomic64_t write_ios;
    atomic64_t total_sectors;
    unsigned long last_io_timestamp;
    unsigned int current_load_percentage;
    spinlock_t stats_lock;
};

/* Health scanning context (part of dm_remap_context) */
struct health_scanning_context {
    struct health_scanning_data config;
    struct dm_remap_io_stats io_stats;
    sector_t next_scan_sector;
    sector_t device_size;
    unsigned int scan_progress_percentage;
    bool scanning_enabled;
    bool scanning_active;
    struct mutex scan_mutex;
};

/* ============================================================================
 * I/O LOAD MONITORING
 * ============================================================================ */

/**
 * init_io_stats - Initialize I/O statistics tracking
 * @stats: I/O statistics structure to initialize
 */
static void init_io_stats(struct dm_remap_io_stats *stats)
{
    atomic64_set(&stats->read_ios, 0);
    atomic64_set(&stats->write_ios, 0);
    atomic64_set(&stats->total_sectors, 0);
    stats->last_io_timestamp = jiffies;
    stats->current_load_percentage = 0;
    spin_lock_init(&stats->stats_lock);
}

/**
 * update_io_load - Update I/O load statistics
 * @dmrc: Device mapper context
 * @bio: I/O bio being processed
 */
void update_io_load(struct dm_remap_context *dmrc, struct bio *bio)
{
    struct dm_remap_io_stats *stats = &dmrc->health_ctx.io_stats;
    unsigned long current_time = jiffies;
    unsigned long time_diff;
    u64 total_ios;
    
    /* Update I/O counters */
    if (bio_data_dir(bio) == READ) {
        atomic64_inc(&stats->read_ios);
    } else {
        atomic64_inc(&stats->write_ios);
    }
    
    atomic64_add(bio_sectors(bio), &stats->total_sectors);
    
    /* Calculate load percentage (simplified algorithm) */
    spin_lock(&stats->stats_lock);
    
    time_diff = current_time - stats->last_io_timestamp;
    if (time_diff > 0) {
        total_ios = atomic64_read(&stats->read_ios) + 
                   atomic64_read(&stats->write_ios);
        
        /* Simple load calculation: IOs per second over threshold */
        stats->current_load_percentage = min(100u, 
            (unsigned int)((total_ios * HZ) / (time_diff * 10)));
    }
    
    stats->last_io_timestamp = current_time;
    spin_unlock(&stats->stats_lock);
}

/**
 * is_io_load_acceptable_for_scanning - Check if I/O load allows scanning
 * @dmrc: Device mapper context
 * Returns: true if scanning is safe, false if I/O load too high
 */
static bool is_io_load_acceptable_for_scanning(struct dm_remap_context *dmrc)
{
    return dmrc->health_ctx.io_stats.current_load_percentage < 
           HEALTH_SCAN_IO_THRESHOLD;
}

/* ============================================================================
 * HEALTH SCORING ALGORITHMS
 * ============================================================================ */

/**
 * assess_sector_health - Assess health of a single sector
 * @bdev: Block device
 * @sector: Sector to assess
 * Returns: Health score (0-100, higher is better)
 */
static unsigned int assess_sector_health(struct block_device *bdev, 
                                       sector_t sector)
{
    struct bio *bio;
    struct page *page;
    int ret;
    ktime_t start_time, end_time;
    s64 read_latency_ns;
    unsigned int health_score = 100;  /* Start with perfect health */
    
    /* Allocate page for read test */
    page = alloc_page(GFP_NOIO);
    if (!page) {
        return 50;  /* Return average health if can't test */
    }
    
    /* Create bio for health check read */
    bio = bio_alloc(GFP_NOIO, 1);
    if (!bio) {
        __free_page(page);
        return 50;
    }
    
    bio_set_dev(bio, bdev);
    bio->bi_iter.bi_sector = sector;
    bio_set_op_attrs(bio, REQ_OP_READ, 0);
    bio_add_page(bio, page, 512, 0);  /* Read single sector */
    
    /* Measure read latency */
    start_time = ktime_get();
    ret = submit_bio_wait(bio);
    end_time = ktime_get();
    
    read_latency_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    /* Assess health based on results */
    if (ret != 0) {
        /* Read error indicates bad sector */
        health_score = 0;
    } else {
        /* Assess based on read latency */
        if (read_latency_ns > 100000000) {      /* > 100ms */
            health_score = 20;  /* Very slow - poor health */
        } else if (read_latency_ns > 50000000) { /* > 50ms */
            health_score = 40;  /* Slow - degraded health */
        } else if (read_latency_ns > 10000000) { /* > 10ms */
            health_score = 70;  /* Moderate - acceptable health */
        }
        /* else: Fast read - keep score at 100 */
    }
    
    bio_put(bio);
    __free_page(page);
    
    return health_score;
}

/**
 * calculate_optimal_scan_batch_size - Determine optimal scan batch size
 * @dmrc: Device mapper context
 * Returns: Optimal batch size in sectors
 */
static unsigned int calculate_optimal_scan_batch_size(
    struct dm_remap_context *dmrc)
{
    unsigned int load = dmrc->health_ctx.io_stats.current_load_percentage;
    unsigned int batch_size;
    
    if (load < 10) {
        batch_size = MAX_SCAN_BATCH_SECTORS;      /* Low load - larger batches */
    } else if (load < 20) {
        batch_size = DEFAULT_SCAN_BATCH_SECTORS;  /* Medium load - default */
    } else {
        batch_size = MIN_SCAN_BATCH_SECTORS;      /* High load - smaller batches */
    }
    
    return batch_size;
}

/* ============================================================================
 * HEALTH SCANNING WORK FUNCTIONS
 * ============================================================================ */

/**
 * dm_remap_health_scan_work - Main health scanning work function
 * @work: Work structure
 */
static void dm_remap_health_scan_work(struct work_struct *work)
{
    struct dm_remap_health_work *health_work = 
        container_of(work, struct dm_remap_health_work, work);
    struct dm_remap_context *dmrc = health_work->dmrc;
    struct health_scanning_context *health_ctx = &dmrc->health_ctx;
    sector_t current_sector;
    unsigned int sector_health;
    unsigned int total_health = 0;
    unsigned int sectors_scanned = 0;
    ktime_t scan_start_time, sector_start_time, sector_end_time;
    s64 scan_overhead_ns = 0;
    
    /* Check if scanning should proceed */
    if (!health_ctx->scanning_enabled || 
        !is_io_load_acceptable_for_scanning(dmrc)) {
        
        /* Reschedule for later */
        if (!health_work->is_retry) {
            health_work->is_retry = true;
            INIT_DELAYED_WORK(&health_work->delayed_work, 
                            dm_remap_health_scan_work);
            queue_delayed_work(dm_remap_health_wq, 
                             &health_work->delayed_work,
                             HEALTH_SCAN_RETRY_DELAY);
        }
        return;
    }
    
    mutex_lock(&health_ctx->scan_mutex);
    health_ctx->scanning_active = true;
    
    scan_start_time = ktime_get();
    
    /* Scan sectors in the specified range */
    for (current_sector = health_work->scan_start;
         current_sector < health_work->scan_end && 
         current_sector < health_ctx->device_size;
         current_sector++) {
        
        /* Check if we should yield to regular I/O */
        if (!is_io_load_acceptable_for_scanning(dmrc)) {
            break;  /* Stop scanning if I/O load increased */
        }
        
        sector_start_time = ktime_get();
        sector_health = assess_sector_health(dmrc->spare_bdev, current_sector);
        sector_end_time = ktime_get();
        
        scan_overhead_ns += ktime_to_ns(ktime_sub(sector_end_time, 
                                                 sector_start_time));
        
        total_health += sector_health;
        sectors_scanned++;
        
        /* Take action if unhealthy sector detected */
        if (sector_health < 50) {
            printk(KERN_WARNING "dm-remap: Unhealthy sector detected at %llu, "
                   "health score: %u\n", current_sector, sector_health);
            
            /* TODO: Trigger predictive remapping */
        }
        
        /* Yield CPU periodically */
        if (sectors_scanned % 64 == 0) {
            cond_resched();
        }
    }
    
    /* Update health statistics */
    if (sectors_scanned > 0) {
        health_ctx->config.stats.scans_completed++;
        health_ctx->config.health_score = total_health / sectors_scanned;
        health_ctx->config.stats.scan_overhead_ms = 
            (u32)(scan_overhead_ns / 1000000);  /* Convert to milliseconds */
        
        /* Update scan progress */
        health_ctx->next_scan_sector = current_sector;
        health_ctx->scan_progress_percentage = 
            (unsigned int)((current_sector * 100) / health_ctx->device_size);
        
        health_ctx->config.last_scan_time = ktime_get_ns();
        health_ctx->config.sector_scan_progress = 
            health_ctx->scan_progress_percentage;
    }
    
    health_ctx->scanning_active = false;
    mutex_unlock(&health_ctx->scan_mutex);
    
    /* Schedule next scan batch if not complete */
    if (current_sector < health_ctx->device_size) {
        schedule_health_scan(dmrc);
    }
    
    kfree(health_work);
}

/**
 * schedule_health_scan - Schedule next health scanning batch
 * @dmrc: Device mapper context
 */
void schedule_health_scan(struct dm_remap_context *dmrc)
{
    struct health_scanning_context *health_ctx = &dmrc->health_ctx;
    struct dm_remap_health_work *health_work;
    
    if (!health_ctx->scanning_enabled) {
        return;
    }
    
    /* Check if I/O load allows scanning */
    if (!is_io_load_acceptable_for_scanning(dmrc)) {
        /* Schedule retry */
        health_work = kmalloc(sizeof(*health_work), GFP_NOIO);
        if (!health_work) {
            return;
        }
        
        health_work->dmrc = dmrc;
        health_work->is_retry = true;
        INIT_DELAYED_WORK(&health_work->delayed_work, 
                         dm_remap_health_scan_work);
        queue_delayed_work(dm_remap_health_wq, &health_work->delayed_work,
                          HEALTH_SCAN_RETRY_DELAY);
        return;
    }
    
    /* Create new health scanning work */
    health_work = kmalloc(sizeof(*health_work), GFP_NOIO);
    if (!health_work) {
        return;
    }
    
    health_work->dmrc = dmrc;
    health_work->scan_start = health_ctx->next_scan_sector;
    health_work->scan_batch_size = calculate_optimal_scan_batch_size(dmrc);
    health_work->scan_end = min(health_work->scan_start + 
                               health_work->scan_batch_size,
                               health_ctx->device_size);
    health_work->is_retry = false;
    
    INIT_WORK(&health_work->work, dm_remap_health_scan_work);
    queue_work(dm_remap_health_wq, &health_work->work);
}

/* ============================================================================
 * INITIALIZATION AND CLEANUP
 * ============================================================================ */

/**
 * init_health_scanning_context - Initialize health scanning context
 * @dmrc: Device mapper context
 * @device_size: Size of device in sectors
 * Returns: 0 on success, negative error code on failure
 */
int init_health_scanning_context(struct dm_remap_context *dmrc, 
                                sector_t device_size)
{
    struct health_scanning_context *health_ctx = &dmrc->health_ctx;
    
    /* Initialize health configuration */
    health_ctx->config.scan_interval = HEALTH_SCAN_DEFAULT_INTERVAL;
    health_ctx->config.health_score = 100;  /* Start optimistic */
    health_ctx->config.sector_scan_progress = 0;
    health_ctx->config.scan_flags = 0;
    memset(&health_ctx->config.stats, 0, sizeof(health_ctx->config.stats));
    
    /* Initialize scanning state */
    health_ctx->next_scan_sector = 0;
    health_ctx->device_size = device_size;
    health_ctx->scan_progress_percentage = 0;
    health_ctx->scanning_enabled = true;
    health_ctx->scanning_active = false;
    
    /* Initialize I/O statistics */
    init_io_stats(&health_ctx->io_stats);
    
    /* Initialize mutex */
    mutex_init(&health_ctx->scan_mutex);
    
    return 0;
}

/**
 * cleanup_health_scanning_context - Cleanup health scanning context
 * @dmrc: Device mapper context
 */
void cleanup_health_scanning_context(struct dm_remap_context *dmrc)
{
    struct health_scanning_context *health_ctx = &dmrc->health_ctx;
    
    /* Disable scanning */
    health_ctx->scanning_enabled = false;
    
    /* Wait for active scans to complete */
    mutex_lock(&health_ctx->scan_mutex);
    mutex_unlock(&health_ctx->scan_mutex);
    
    /* Flush any pending work */
    flush_workqueue(dm_remap_health_wq);
}

/**
 * dm_remap_health_init - Initialize health scanning subsystem
 * Returns: 0 on success, negative error code on failure
 */
int dm_remap_health_init(void)
{
    dm_remap_health_wq = alloc_workqueue("dm-remap-health",
        WQ_UNBOUND | WQ_FREEZABLE | WQ_MEM_RECLAIM, 0);
    
    if (!dm_remap_health_wq) {
        printk(KERN_ERR "dm-remap: Failed to create health scanning workqueue\n");
        return -ENOMEM;
    }
    
    printk(KERN_INFO "dm-remap: Health scanning subsystem initialized\n");
    return 0;
}

/**
 * dm_remap_health_exit - Cleanup health scanning subsystem
 */
void dm_remap_health_exit(void)
{
    if (dm_remap_health_wq) {
        destroy_workqueue(dm_remap_health_wq);
        dm_remap_health_wq = NULL;
    }
    
    printk(KERN_INFO "dm-remap: Health scanning subsystem cleaned up\n");
}

/* Export functions */
EXPORT_SYMBOL(update_io_load);
EXPORT_SYMBOL(schedule_health_scan);
EXPORT_SYMBOL(init_health_scanning_context);
EXPORT_SYMBOL(cleanup_health_scanning_context);
EXPORT_SYMBOL(dm_remap_health_init);
EXPORT_SYMBOL(dm_remap_health_exit);