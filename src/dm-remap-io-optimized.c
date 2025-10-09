/*
 * dm-remap-io-optimized.c - Phase 3.2B Optimized I/O Processing
 * 
 * This file implements the optimized I/O processing pipeline for Phase 3.2B,
 * incorporating all performance enhancements and hot path optimizations.
 * 
 * KEY PHASE 3.2B OPTIMIZATIONS:
 * - Fast path with cache-optimized data structures
 * - Per-CPU performance counters (lock-free)
 * - Red-black tree O(log n) lookups
 * - Memory prefetching and spatial locality
 * - Sequential access pattern detection
 * - Reduced lock contention
 * 
 * PERFORMANCE TARGETS:
 * - <100ns average I/O latency (25% improvement from 135ns)
 * - >3.5 GB/s sequential throughput (20% improvement from 2.9 GB/s)
 * - 50% reduction in CPU cycles per I/O operation
 * - Better cache hit rates through optimized data layout
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/device-mapper.h>
#include <linux/prefetch.h>
#include <linux/cache.h>
#include <linux/compiler.h>
#include <linux/percpu.h>

#include "dm-remap-core.h"
#include "dm-remap-performance-optimization.h"
#include "dm-remap-io-optimized.h"

/* Phase 3.2B: Global optimized context */
static struct dmr_optimized_context *global_opt_ctx = NULL;

/* Phase 3.2B: Performance optimization statistics */
static atomic64_t opt_fast_path_hits = ATOMIC64_INIT(0);
static atomic64_t opt_slow_path_hits = ATOMIC64_INIT(0);
static atomic64_t opt_total_lookups = ATOMIC64_INIT(0);

/**
 * dmr_io_optimized_process - Phase 3.2B optimized I/O processing
 * @ti: Target instance
 * @bio: Bio to process
 * 
 * This is the main optimized I/O processing function that incorporates
 * all Phase 3.2B performance enhancements.
 * 
 * Returns: DM_MAPIO_* result code
 */
int dmr_io_optimized_process(struct dm_target *ti, struct bio *bio)
{
    struct remap_c *rc = ti->private;
    struct dmr_optimized_remap_entry *opt_entry;
    sector_t sector = bio->bi_iter.bi_sector;
    struct dm_dev *target_dev = rc->main_dev;
    sector_t target_sector = rc->main_start + sector;
    bool found_remap = false;
    ktime_t start_time, end_time;
    u64 latency_ns;
    bool is_sequential;
    
    /* Phase 3.2B: Start high-precision timing */
    start_time = ktime_get();
    
    /* Phase 3.2B: Check for sequential access patterns */
    is_sequential = global_opt_ctx ? 
        dmr_perf_opt_is_sequential(global_opt_ctx, sector) : false;
    
    /* Phase 3.2B: Increment total lookups for all I/O operations */
    atomic64_inc(&opt_total_lookups);
    
    /* Phase 3.2B: Fast path optimization for common cases */
    if (likely(global_opt_ctx && 
               (global_opt_ctx->optimization_flags & DMR_OPT_FAST_PATH_ENABLED))) {
        
        /* Prefetch optimization for sequential access */
        if (is_sequential) {
            dmr_perf_opt_prefetch_remap_data(global_opt_ctx, sector + 8);
        }
        
        /* Fast lookup using optimized red-black tree */
        opt_entry = dmr_perf_opt_lookup_fast(global_opt_ctx, sector);
        if (opt_entry) {
            /* Fast path hit - remap found */
            target_dev = rc->spare_dev;
            target_sector = opt_entry->spare_lba;
            found_remap = true;
            
            atomic64_inc(&opt_fast_path_hits);
            
            DMR_DEBUG(2, "Phase 3.2B FAST PATH HIT: sector %llu -> spare %llu",
                      (unsigned long long)sector, 
                      (unsigned long long)target_sector);
        } else {
            /* Fast path miss - use passthrough (still counts as fast path usage) */
            /* Note: We don't increment opt_fast_path_hits, but this is still fast path */
            
            DMR_DEBUG(3, "Phase 3.2B FAST PATH MISS: sector %llu (passthrough)",
                      (unsigned long long)sector);
        }
    } else {
        /* Fallback to legacy slow path if optimization disabled */
        int i;
        
        spin_lock(&rc->lock);
        for (i = 0; i < rc->spare_used; i++) {
            if (sector == rc->table[i].main_lba && 
                rc->table[i].main_lba != (sector_t)-1) {
                target_dev = rc->spare_dev;
                target_sector = rc->table[i].spare_lba;
                found_remap = true;
                break;
            }
        }
        spin_unlock(&rc->lock);
        
        /* Only increment slow path counter when actually using slow path */
        atomic64_inc(&opt_slow_path_hits);
        
        DMR_DEBUG(2, "Phase 3.2B SLOW PATH: sector %llu %s",
                  (unsigned long long)sector,
                  found_remap ? "(remapped)" : "(passthrough)");
    }
    
    /* Handle special operations (unchanged from original) */
    if (bio_op(bio) == REQ_OP_FLUSH || bio_op(bio) == REQ_OP_DISCARD || 
        bio_op(bio) == REQ_OP_WRITE_ZEROES) {
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        
        /* Quick performance update for special ops */
        end_time = ktime_get();
        latency_ns = ktime_to_ns(ktime_sub(end_time, start_time));
        dmr_perf_update_stats(1, (unsigned int)latency_ns, 
                             bio->bi_iter.bi_size, 0, 0);
        
        return DM_MAPIO_REMAPPED;
    }
    
    /* Set target device and sector */
    bio_set_dev(bio, target_dev->bdev);
    bio->bi_iter.bi_sector = target_sector;
    
    /* Phase 3.2B: Calculate optimized performance metrics */
    end_time = ktime_get();
    latency_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    /* Phase 3.2B: Update both legacy and optimized performance tracking */
    dmr_perf_update_stats(1, (unsigned int)latency_ns, bio->bi_iter.bi_size, 
                         found_remap ? 1 : 0, found_remap ? 0 : 1);
    
    /* Phase 3.2B: Update optimized per-CPU statistics */
    if (global_opt_ctx) {
        dmr_perf_opt_update_percpu_stats(global_opt_ctx, 1, latency_ns,
                                        bio->bi_iter.bi_size,
                                        found_remap ? 1 : 0,
                                        found_remap ? 0 : 1);
    }
    
    DMR_DEBUG(3, "Phase 3.2B optimized I/O: latency=%lluns, size=%u, %s -> %s",
              latency_ns, bio->bi_iter.bi_size,
              found_remap ? "REMAP" : "PASSTHROUGH",
              target_dev->name);
    
    return DM_MAPIO_REMAPPED;
}

/**
 * dmr_io_optimized_add_remap - Add remap entry to optimized structures
 * @rc: Target context
 * @main_lba: Main device LBA
 * @spare_lba: Spare device LBA
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_io_optimized_add_remap(struct remap_c *rc, sector_t main_lba, sector_t spare_lba)
{
    int ret = 0;
    
    /* Add to legacy table first */
    spin_lock(&rc->lock);
    if (rc->spare_used < rc->spare_len) {
        rc->table[rc->spare_used].main_lba = main_lba;
        rc->table[rc->spare_used].spare_lba = spare_lba;
        rc->spare_used++;
        spin_unlock(&rc->lock);
        
        /* Add to optimized structures */
        if (global_opt_ctx) {
            ret = dmr_perf_opt_add_remap(global_opt_ctx, main_lba, spare_lba);
            if (ret) {
                DMR_DEBUG(1, "Failed to add to optimized table: %d", ret);
                /* Continue with legacy table only */
                ret = 0;
            }
        }
        
        DMR_DEBUG(1, "Phase 3.2B added remap: %llu -> %llu",
                  (unsigned long long)main_lba, (unsigned long long)spare_lba);
    } else {
        spin_unlock(&rc->lock);
        ret = -ENOSPC;
    }
    
    return ret;
}

/**
 * dmr_io_optimized_remove_remap - Remove remap entry from optimized structures
 * @rc: Target context
 * @main_lba: Main device LBA to remove
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_io_optimized_remove_remap(struct remap_c *rc, sector_t main_lba)
{
    int i, ret = -ENOENT;
    
    /* Remove from legacy table */
    spin_lock(&rc->lock);
    for (i = 0; i < rc->spare_used; i++) {
        if (rc->table[i].main_lba == main_lba) {
            /* Move last entry to this position */
            rc->table[i] = rc->table[rc->spare_used - 1];
            rc->spare_used--;
            ret = 0;
            break;
        }
    }
    spin_unlock(&rc->lock);
    
    /* Remove from optimized structures */
    if (ret == 0 && global_opt_ctx) {
        dmr_perf_opt_remove_remap(global_opt_ctx, main_lba);
    }
    
    if (ret == 0) {
        DMR_DEBUG(1, "Phase 3.2B removed remap: %llu",
                  (unsigned long long)main_lba);
    }
    
    return ret;
}

/**
 * dmr_io_optimized_get_stats - Get comprehensive optimization statistics
 * @stats: Output statistics structure
 */
void dmr_io_optimized_get_stats(struct dmr_io_optimization_stats *stats)
{
    struct dmr_percpu_stats percpu_stats;
    
    if (!stats)
        return;
    
    memset(stats, 0, sizeof(*stats));
    
    /* Get optimization-specific statistics */
    stats->fast_path_hits = atomic64_read(&opt_fast_path_hits);
    stats->slow_path_hits = atomic64_read(&opt_slow_path_hits);
    stats->total_lookups = atomic64_read(&opt_total_lookups);
    
    /* Calculate hit rates */
    if (stats->total_lookups > 0) {
        stats->fast_path_hit_rate = (stats->fast_path_hits * 100) / stats->total_lookups;
    }
    
    /* Get per-CPU aggregated statistics */
    if (global_opt_ctx) {
        dmr_perf_opt_get_aggregated_stats(global_opt_ctx, &percpu_stats);
        stats->percpu_total_ios = percpu_stats.total_ios;
        stats->percpu_total_latency_ns = percpu_stats.total_latency_ns;
        stats->percpu_total_bytes = percpu_stats.total_bytes;
        stats->percpu_cache_hits = percpu_stats.cache_hits;
        stats->percpu_cache_misses = percpu_stats.cache_misses;
        stats->percpu_remap_lookups = percpu_stats.remap_lookups;
        stats->optimization_flags = global_opt_ctx->optimization_flags;
        stats->remap_entries = global_opt_ctx->entry_count;
        stats->max_entries = global_opt_ctx->max_entries;
        
        /* Calculate average latency */
        if (percpu_stats.total_ios > 0) {
            stats->avg_latency_ns = percpu_stats.total_latency_ns / percpu_stats.total_ios;
        }
    }
}

/**
 * dmr_io_optimized_init - Initialize Phase 3.2B optimized I/O processing
 * @max_entries: Maximum number of remap entries
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_io_optimized_init(size_t max_entries)
{
    int ret;
    
    /* Allocate optimized context */
    global_opt_ctx = kzalloc(sizeof(*global_opt_ctx), GFP_KERNEL);
    if (!global_opt_ctx) {
        DMR_DEBUG(0, "Failed to allocate Phase 3.2B optimized context");
        return -ENOMEM;
    }
    
    /* Initialize optimized context */
    ret = dmr_perf_opt_init(global_opt_ctx, max_entries);
    if (ret) {
        DMR_DEBUG(0, "Failed to initialize Phase 3.2B optimization: %d", ret);
        kfree(global_opt_ctx);
        global_opt_ctx = NULL;
        return ret;
    }
    
    /* Reset statistics */
    atomic64_set(&opt_fast_path_hits, 0);
    atomic64_set(&opt_slow_path_hits, 0);
    atomic64_set(&opt_total_lookups, 0);
    
    DMR_DEBUG(1, "Phase 3.2B optimized I/O processing initialized: max_entries=%zu",
              max_entries);
    
    return 0;
}

/**
 * dmr_io_optimized_cleanup - Cleanup Phase 3.2B optimized I/O processing
 */
void dmr_io_optimized_cleanup(void)
{
    if (global_opt_ctx) {
        dmr_perf_opt_cleanup(global_opt_ctx);
        kfree(global_opt_ctx);
        global_opt_ctx = NULL;
    }
    
    DMR_DEBUG(1, "Phase 3.2B optimized I/O processing cleaned up");
}

/**
 * dmr_io_optimized_optimize_layout - Trigger memory layout optimization
 */
void dmr_io_optimized_optimize_layout(void)
{
    if (global_opt_ctx) {
        dmr_perf_opt_optimize_memory_layout(global_opt_ctx);
        dmr_perf_opt_compact_remap_table(global_opt_ctx);
        
        DMR_DEBUG(1, "Phase 3.2B triggered memory layout optimization");
    }
}

/**
 * dmr_io_optimized_set_flags - Set optimization flags
 * @flags: Optimization flags to set
 */
void dmr_io_optimized_set_flags(u32 flags)
{
    if (global_opt_ctx) {
        global_opt_ctx->optimization_flags = flags;
        DMR_DEBUG(1, "Phase 3.2B optimization flags set: 0x%x", flags);
    }
}

/**
 * dmr_io_optimized_get_flags - Get current optimization flags
 * 
 * Returns: Current optimization flags
 */
u32 dmr_io_optimized_get_flags(void)
{
    return global_opt_ctx ? global_opt_ctx->optimization_flags : 0;
}