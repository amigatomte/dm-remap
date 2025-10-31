/*
 * dm-remap-hotpath-optimization.c - I/O Hotpath Performance Implementation
 * 
 * Week 9-10 Advanced Optimization: High-performance I/O path implementation
 * Provides cache-optimized, fast-path I/O processing to minimize latency
 * in the critical path of the dm-remap kernel module.
 * 
 * Key features:
 * - Cache-aligned data structures for optimal CPU cache utilization
 * - Fast-path detection with branch prediction optimization
 * - Batch processing for improved throughput
 * - Prefetching strategies for predictive performance
 * - Lock-free operations where possible
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/prefetch.h>
#include <linux/cache.h>
#include <linux/compiler.h>
#include <linux/atomic.h>

#include "dm-remap-core.h"
#include "dm-remap-hotpath-optimization.h"
#include "dm-remap-memory-pool.h"

/* Module metadata */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Hotpath I/O Optimization for dm-remap v4.0");
MODULE_LICENSE("GPL");

/*
 * Cache-aligned hotpath data structure
 * This structure is designed for optimal cache line utilization
 */
/* Internal atomic statistics structure */
struct dmr_hotpath_atomic_stats {
    atomic64_t total_ios;               /* Total I/O operations */
    atomic64_t fastpath_ios;            /* Fast-path I/Os */
    atomic64_t cache_line_hits;         /* Cache line hits */
    atomic64_t prefetch_hits;           /* Prefetch hits */
    atomic64_t batch_processed;        /* Batch processed I/Os */
    atomic64_t branch_mispredicts;      /* Branch misprediction estimates */
} ____cacheline_aligned;

struct dmr_hotpath_manager {
    struct dmr_hotpath_context context ____cacheline_aligned;
    struct dmr_hotpath_atomic_stats stats ____cacheline_aligned;
    
    /* Optimization tuning parameters */
    u32 prefetch_distance;              /* Sectors to prefetch ahead */
    u32 batch_timeout_ms;               /* Batch processing timeout */
    bool adaptive_batching;             /* Enable adaptive batch sizing */
    
    /* Cache optimization state */
    sector_t last_accessed_sector;      /* Last accessed sector for locality */
    u32 sequential_count;               /* Sequential access counter */
    
    /* Performance monitoring */
    unsigned long last_stats_time;     /* Last statistics update time */
} ____cacheline_aligned;

/**
 * dmr_hotpath_init - Initialize hotpath optimization system
 * @rc: Remap context
 * 
 * Sets up the hotpath optimization infrastructure including cache-aligned
 * data structures and performance monitoring.
 */
/*
 * Test version of dmr_hotpath_init() - Basic validation
 * DISABLED - using the full version below
 */

/*
int dmr_hotpath_init(struct remap_c *rc)
{
    if (!rc) return -EINVAL;
    
    return 0;
}
*/

/* DUPLICATE STRUCT DEFINITION REMOVED - using the one above */

/**
 * dmr_hotpath_init - Initialize hotpath optimization system
 * @rc: Remap context
 * 
 * Sets up the hotpath optimization infrastructure including cache-aligned
 * data structures and performance monitoring.
 */
int dmr_hotpath_init(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: About to allocate manager\n");
    
    /* THIS IS THE SUSPECT LINE - cache-aligned allocation */
    manager = dmr_alloc_cache_aligned(sizeof(*manager));
    
    printk(KERN_INFO "dmr_hotpath_init: Allocation completed\n");
    
    if (!manager) {
        printk(KERN_ERR "dmr_hotpath_init: Failed to allocate hotpath manager\n");
        return -ENOMEM;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: Setting manager and returning\n");
    rc->hotpath_manager = manager;
    
    printk(KERN_INFO "dmr_hotpath_init: TEST 1 completed successfully\n");
    return 0;
}

/**
 * dmr_is_fastpath_eligible - Determine if I/O can use fast path
 * @bio: Bio to evaluate
 * @rc: Remap context
 * 
 * Performs lightweight checks to determine if an I/O operation can
 * bypass expensive processing and use the optimized fast path.
 */
bool dmr_is_fastpath_eligible(struct bio *bio, struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    sector_t sector;
    u32 flags = 0;

    if (dmr_unlikely_slowpath(!bio || !rc || !rc->hotpath_manager)) {
        return false;
    }

    manager = rc->hotpath_manager;
    sector = bio->bi_iter.bi_sector;

    /* Fast path eligibility checks with branch prediction hints */
    
    /* Check 1: Basic bio validation */
    if (dmr_unlikely_slowpath(bio_sectors(bio) == 0)) {
        return false;
    }

    /* Check 2: Sector range validation */
    if (dmr_unlikely_slowpath(!dmr_is_sector_in_range(sector, 0, rc->main_sectors))) {
        return false;
    }

    /* Check 3: Operation type flags */
    if (bio_data_dir(bio) == READ) {
        flags |= DMR_FASTPATH_READ;
    } else {
        flags |= DMR_FASTPATH_WRITE;
    }

    /* Check 4: Health status (assume healthy for fast path) */
    if (dmr_likely_fastpath(dmr_is_sector_healthy(rc, sector))) {
        flags |= DMR_FASTPATH_HEALTHY;
    }

    /* Check 5: Sequential access pattern detection */
    if (dmr_likely_fastpath(sector == manager->last_accessed_sector + 1)) {
        manager->sequential_count++;
        flags |= DMR_FASTPATH_CACHED;
    } else {
        manager->sequential_count = 0;
    }

    manager->last_accessed_sector = sector;

    /* Fast path is eligible if we have basic flags */
    bool eligible = (flags & (DMR_FASTPATH_READ | DMR_FASTPATH_WRITE)) &&
                   (flags & DMR_FASTPATH_HEALTHY);

    if (eligible) {
        atomic64_inc(&manager->stats.fastpath_ios);
        
        /* Update access pattern for prefetching */
        dmr_hotpath_update_access_pattern(rc, sector);
    } else {
        atomic64_inc(&manager->context.slow_path_fallbacks);
    }

    return eligible;
}

/**
 * dmr_process_fastpath_io - Process I/O using optimized fast path
 * @bio: Bio to process
 * @rc: Remap context
 * 
 * Handles I/O processing using the optimized fast path with minimal
 * overhead and maximum performance.
 */
int dmr_process_fastpath_io(struct bio *bio, struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    sector_t sector;

    if (dmr_unlikely_slowpath(!bio || !rc || !rc->hotpath_manager)) {
        return -EINVAL;
    }

    manager = rc->hotpath_manager;
    sector = bio->bi_iter.bi_sector;

    /* Update performance statistics */
    atomic64_inc(&manager->stats.total_ios);
    
    if (bio_data_dir(bio) == READ) {
        atomic64_inc(&manager->context.fast_reads);
    } else {
        atomic64_inc(&manager->context.fast_writes);
    }

    /* Prefetch remap data for potential future use */
    dmr_hotpath_prefetch_remap_data(rc, sector);

    /* Fast path: direct mapping without remapping overhead */
    bio_set_dev(bio, rc->main_dev->bdev);
    bio->bi_iter.bi_sector = sector + rc->main_start;

    DMR_DEBUG(2, "Fast path I/O: sector %llu, size %u sectors", 
              (unsigned long long)sector, bio_sectors(bio));

    /* Submit bio directly */
    submit_bio_noacct(bio);

    return 0;
}

/**
 * dmr_hotpath_prefetch_remap_data - Prefetch remap table data
 * @rc: Remap context
 * @sector: Current sector being accessed
 * 
 * Implements intelligent prefetching of remap table data based on
 * access patterns to reduce cache misses.
 */
void dmr_hotpath_prefetch_remap_data(struct remap_c *rc, sector_t sector)
{
    struct dmr_hotpath_manager *manager;
    int i;

    if (dmr_unlikely_slowpath(!rc || !rc->hotpath_manager)) {
        return;
    }

    manager = rc->hotpath_manager;

    /* Prefetch remap table entries ahead of current access */
    for (i = 1; i <= manager->prefetch_distance; i++) {
        sector_t prefetch_sector = sector + i;
        
        /* Check if prefetch sector is valid */
        if (dmr_likely_fastpath(prefetch_sector < rc->main_sectors)) {
            /* Calculate remap table index */
            int table_index = prefetch_sector % rc->spare_len;
            
            if (dmr_likely_fastpath(table_index < rc->spare_len)) {
                /* Prefetch remap table entry */
                prefetch(&rc->table[table_index]);
                
                /* Store prefetch target for statistics */
                if (i <= DMR_HOTPATH_PREFETCH_LINES) {
                    manager->context.prefetch_targets[i-1] = &rc->table[table_index];
                }
            }
        }
    }

    /* Update prefetch statistics */
    atomic64_inc(&manager->stats.prefetch_hits);
}

/**
 * dmr_hotpath_update_access_pattern - Update access pattern tracking
 * @rc: Remap context
 * @sector: Sector being accessed
 * 
 * Tracks access patterns to optimize prefetching and caching strategies.
 */
void dmr_hotpath_update_access_pattern(struct remap_c *rc, sector_t sector)
{
    struct dmr_hotpath_manager *manager;

    if (dmr_unlikely_slowpath(!rc || !rc->hotpath_manager)) {
        return;
    }

    manager = rc->hotpath_manager;

    /* Detect sequential access patterns */
    if (sector == manager->last_accessed_sector + 1) {
        manager->sequential_count++;
        
        /* Increase prefetch distance for sequential access */
        if (manager->sequential_count > 4 && manager->prefetch_distance < 16) {
            manager->prefetch_distance++;
        }
    } else {
        /* Reset for random access patterns */
        manager->sequential_count = 0;
        manager->prefetch_distance = 8;  /* Reset to default */
    }

    /* Update cache hit statistics based on locality */
    if (abs((long)(sector - manager->last_accessed_sector)) <= 8) {
        atomic64_inc(&manager->context.cache_hits);
        atomic64_inc(&manager->stats.cache_line_hits);
    }
}

/**
 * dmr_hotpath_batch_add - Add bio to batch processing queue
 * @rc: Remap context
 * @bio: Bio to add to batch
 * 
 * Adds a bio to the batch processing queue for improved throughput.
 */
int dmr_hotpath_batch_add(struct remap_c *rc, struct bio *bio)
{
    struct dmr_hotpath_manager *manager;
    unsigned long flags;
    int ret = 0;

    if (dmr_unlikely_slowpath(!rc || !rc->hotpath_manager || !bio)) {
        return -EINVAL;
    }

    manager = rc->hotpath_manager;

    spin_lock_irqsave(&manager->context.batch_lock, flags);

    if (dmr_likely_fastpath(!dmr_hotpath_batch_full(&manager->context))) {
        manager->context.batch_bios[manager->context.batch_count] = bio;
        manager->context.batch_count++;
        ret = 0;
    } else {
        /* Batch is full, trigger immediate processing */
        ret = -EAGAIN;
    }

    spin_unlock_irqrestore(&manager->context.batch_lock, flags);

    /* Process batch if full or if adaptive batching suggests it */
    if (ret == -EAGAIN || 
        (manager->adaptive_batching && manager->context.batch_count >= 8)) {
        dmr_hotpath_batch_process(rc);
        ret = 0;  /* Successfully handled */
    }

    return ret;
}

/**
 * dmr_hotpath_batch_process - Process batched I/O operations
 * @rc: Remap context
 * 
 * Processes accumulated batch of I/O operations for improved throughput.
 */
void dmr_hotpath_batch_process(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    struct bio *batch_bios[DMR_HOTPATH_BATCH_SIZE];
    int batch_count;
    unsigned long flags;
    int i;

    if (dmr_unlikely_slowpath(!rc || !rc->hotpath_manager)) {
        return;
    }

    manager = rc->hotpath_manager;

    /* Atomically extract batch */
    spin_lock_irqsave(&manager->context.batch_lock, flags);
    batch_count = manager->context.batch_count;
    memcpy(batch_bios, manager->context.batch_bios, 
           batch_count * sizeof(struct bio *));
    dmr_hotpath_batch_reset(&manager->context);
    spin_unlock_irqrestore(&manager->context.batch_lock, flags);

    if (batch_count == 0) {
        return;
    }

    DMR_DEBUG(2, "Processing batch of %d I/O operations", batch_count);

    /* Process batch with optimized loop */
    for (i = 0; i < batch_count; i++) {
        struct bio *bio = batch_bios[i];
        
        if (dmr_likely_fastpath(bio != NULL)) {
            /* Process each bio in the batch */
            dmr_process_fastpath_io(bio, rc);
        }
    }

    /* Update batch processing statistics */
    atomic64_add(batch_count, &manager->stats.batch_processed);
}

/**
 * dmr_hotpath_get_stats - Get hotpath performance statistics
 * @rc: Remap context
 * @stats: Output statistics structure
 * 
 * Retrieves current performance statistics from the hotpath optimization.
 */
void dmr_hotpath_get_stats(struct remap_c *rc, struct dmr_hotpath_stats *stats)
{
    struct dmr_hotpath_manager *manager;

    if (dmr_unlikely_slowpath(!rc || !rc->hotpath_manager || !stats)) {
        return;
    }

    manager = rc->hotpath_manager;

    /* Copy statistics atomically */
    stats->total_ios = (u64)atomic64_read(&manager->stats.total_ios);
    stats->fastpath_ios = (u64)atomic64_read(&manager->stats.fastpath_ios);
    stats->cache_line_hits = (u64)atomic64_read(&manager->stats.cache_line_hits);
    stats->prefetch_hits = (u64)atomic64_read(&manager->stats.prefetch_hits);
    stats->batch_processed = (u64)atomic64_read(&manager->stats.batch_processed);
    stats->branch_mispredicts = (u64)atomic64_read(&manager->stats.branch_mispredicts);
    
    /* Calculate derived statistics */
    if (stats->total_ios > 0) {
        u64 fastpath_percent = (stats->fastpath_ios * 100) / stats->total_ios;
        DMR_DEBUG(2, "Hotpath efficiency: %llu%% (%llu/%llu fast path)",
                  fastpath_percent, stats->fastpath_ios, stats->total_ios);
    }
}

/**
 * dmr_hotpath_reset_stats - Reset performance statistics
 * @rc: Remap context
 * 
 * Resets all performance counters to zero.
 */
void dmr_hotpath_reset_stats(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;

    if (dmr_unlikely_slowpath(!rc || !rc->hotpath_manager)) {
        return;
    }

    manager = rc->hotpath_manager;

    /* Reset all statistics atomically */
    atomic64_set(&manager->stats.total_ios, 0);
    atomic64_set(&manager->stats.fastpath_ios, 0);
    atomic64_set(&manager->stats.cache_line_hits, 0);
    atomic64_set(&manager->stats.prefetch_hits, 0);
    atomic64_set(&manager->stats.batch_processed, 0);
    atomic64_set(&manager->stats.branch_mispredicts, 0);

    /* Reset context counters */
    atomic64_set(&manager->context.fast_reads, 0);
    atomic64_set(&manager->context.fast_writes, 0);
    atomic64_set(&manager->context.slow_path_fallbacks, 0);
    atomic64_set(&manager->context.cache_hits, 0);

    manager->last_stats_time = jiffies;

    DMR_DEBUG(1, "Hotpath performance statistics reset");
}



/**
 * dmr_hotpath_cleanup - Clean up hotpath optimization system
 * @rc: Remap context
 * 
 * Cleans up hotpath optimization resources and prints final statistics.
 */
void dmr_hotpath_cleanup(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    struct dmr_hotpath_stats final_stats;

    if (!rc || !rc->hotpath_manager) {
        return;
    }

    manager = rc->hotpath_manager;

    DMR_DEBUG(1, "Cleaning up hotpath optimization system");

    /* Get final statistics */
    dmr_hotpath_get_stats(rc, &final_stats);

    /* Log final performance metrics */
    DMR_DEBUG(1, "Hotpath final stats - Total: %llu, Fast: %llu, Cache hits: %llu, "
              "Prefetch: %llu, Batch: %llu",
              final_stats.total_ios, final_stats.fastpath_ios,
              final_stats.cache_line_hits, final_stats.prefetch_hits,
              final_stats.batch_processed);

    /* Process any remaining batched I/Os */
    if (manager->context.batch_count > 0) {
        DMR_DEBUG(1, "Processing remaining %d batched I/Os", 
                  manager->context.batch_count);
        dmr_hotpath_batch_process(rc);
    }

    /* Free the manager */
    kfree(manager);
    rc->hotpath_manager = NULL;

    DMR_DEBUG(1, "Hotpath optimization cleanup complete");
}