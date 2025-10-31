/*
 * dm-remap-bio-context-optimized.c - Memory Pool Optimized Bio Context Management
 * 
 * Week 9 Optimization: High-performance bio context allocation and management
 * Replaces kmalloc/kfree patterns in I/O paths with memory pool allocation
 * to reduce allocation overhead and memory fragmentation.
 * 
 * Key optimizations:
 * - Memory pool allocation for bio contexts
 * - Pre-allocated context pools to avoid allocation in I/O path
 * - Cache-aligned structures for better performance
 * - Reduced lock contention with per-CPU pools (future enhancement)
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/atomic.h>

#include "dm-remap-core.h"
#include "dm-remap-memory-pool.h"
#include "dm-remap-io.h"

/* Module metadata */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Optimized Bio Context Management for dm-remap v4.0");
MODULE_LICENSE("GPL");

/* Bio context optimization statistics */
struct dmr_bio_context_stats {
    atomic64_t fast_allocs;         /* Fast pool allocations */
    atomic64_t slow_allocs;         /* Fallback kmalloc allocations */
    atomic64_t pool_hits;           /* Successful pool allocations */
    atomic64_t pool_misses;         /* Pool allocation failures */
    atomic64_t total_contexts;      /* Total contexts created */
    atomic64_t active_contexts;     /* Currently active contexts */
    atomic64_t peak_contexts;       /* Peak concurrent contexts */
};

/* Global bio context statistics */
static struct dmr_bio_context_stats bio_stats;

/**
 * dmr_bio_context_alloc_optimized - Allocate bio context from memory pool
 * @rc: Remap context
 * @gfp_flags: GFP allocation flags
 * 
 * Allocates a bio context structure using the optimized memory pool system.
 * Falls back to kmalloc if pool allocation fails.
 * 
 * Returns: Allocated bio context or NULL on failure
 */
struct dmr_bio_context *dmr_bio_context_alloc_optimized(struct remap_c *rc, 
                                                        gfp_t gfp_flags)
{
    struct dmr_bio_context *ctx;
    u64 active, peak;

    if (!rc || !rc->pool_manager) {
        DMR_DEBUG("Bio context alloc: invalid remap context or no pool manager");
        return NULL;
    }

    /* Try to allocate from memory pool first */
    ctx = dmr_alloc_bio_context(rc);
    if (ctx) {
        atomic64_inc(&bio_stats.fast_allocs);
        atomic64_inc(&bio_stats.pool_hits);
        DMR_DEBUG("Bio context allocated from pool: %p", ctx);
    } else {
        /* Fallback to direct allocation */
        ctx = kmalloc(sizeof(*ctx), gfp_flags);
        if (ctx) {
            atomic64_inc(&bio_stats.slow_allocs);
            atomic64_inc(&bio_stats.pool_misses);
            DMR_DEBUG("Bio context allocated via kmalloc fallback: %p", ctx);
        } else {
            DMR_DEBUG("Bio context allocation failed completely");
            return NULL;
        }
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(*ctx));
    ctx->rc = rc;
    ctx->start_time = jiffies;

    /* Update statistics */
    atomic64_inc(&bio_stats.total_contexts);
    active = atomic64_inc_return(&bio_stats.active_contexts);
    
    /* Update peak if necessary */
    peak = atomic64_read(&bio_stats.peak_contexts);
    while (active > peak) {
        if (atomic64_cmpxchg(&bio_stats.peak_contexts, peak, active) == peak)
            break;
        peak = atomic64_read(&bio_stats.peak_contexts);
    }

    DMR_DEBUG("Bio context allocated: %p, active: %lld", ctx, active);
    return ctx;
}

/**
 * dmr_bio_context_free_optimized - Free bio context to memory pool
 * @ctx: Bio context to free
 * 
 * Returns a bio context structure to the optimized memory pool system.
 * Handles both pool-allocated and kmalloc-allocated contexts.
 */
void dmr_bio_context_free_optimized(struct dmr_bio_context *ctx)
{
    struct remap_c *rc;

    if (!ctx) {
        return;
    }

    rc = ctx->rc;
    DMR_DEBUG("Freeing bio context: %p", ctx);

    if (rc && rc->pool_manager) {
        /* Try to return to pool - the pool system will handle
         * whether this was originally pool-allocated or not */
        dmr_free_bio_context(rc, ctx);
        DMR_DEBUG("Bio context returned to pool: %p", ctx);
    } else {
        /* Direct free for fallback allocations or missing pool */
        kfree(ctx);
        DMR_DEBUG("Bio context freed via kfree: %p", ctx);
    }

    /* Update statistics */
    atomic64_dec(&bio_stats.active_contexts);
}

/**
 * dmr_bio_context_setup_tracking - Set up bio tracking with optimized context
 * @bio: Bio to track
 * @rc: Remap context
 * @original_lba: Original logical block address
 * 
 * Sets up bio tracking using an optimized bio context allocation.
 * This replaces the original bio tracking setup with memory pool optimization.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_bio_context_setup_tracking(struct bio *bio, struct remap_c *rc, 
                                  sector_t original_lba)
{
    struct dmr_bio_context *ctx;

    if (!bio || !rc) {
        return -EINVAL;
    }

    /* Allocate optimized bio context */
    ctx = dmr_bio_context_alloc_optimized(rc, GFP_NOIO);
    if (!ctx) {
        DMR_DEBUG("Failed to allocate bio context for sector %llu",
                  (unsigned long long)original_lba);
        return -ENOMEM;
    }

    /* Set up tracking information */
    ctx->original_lba = original_lba;
    ctx->retry_count = 0;
    
    /* Store original bio information */
    ctx->original_bi_end_io = bio->bi_end_io;
    ctx->original_bi_private = bio->bi_private;
    
    /* Set up our tracking */
    bio->bi_private = ctx;
    /* bio->bi_end_io will be set by caller */

    DMR_DEBUG("Bio tracking setup for sector %llu, ctx: %p",
              (unsigned long long)original_lba, ctx);

    return 0;
}

/**
 * dmr_bio_context_cleanup_tracking - Clean up bio tracking context
 * @bio: Bio being completed
 * 
 * Cleans up bio tracking and frees the associated context.
 * This should be called from bio completion handlers.
 */
void dmr_bio_context_cleanup_tracking(struct bio *bio)
{
    struct dmr_bio_context *ctx;

    if (!bio || !bio->bi_private) {
        return;
    }

    ctx = (struct dmr_bio_context *)bio->bi_private;
    
    /* Restore original bio information before cleanup */
    bio->bi_private = ctx->original_bi_private;
    bio->bi_end_io = ctx->original_bi_end_io;

    DMR_DEBUG("Cleaning up bio tracking for sector %llu, ctx: %p",
              (unsigned long long)ctx->original_lba, ctx);

    /* Free the context */
    dmr_bio_context_free_optimized(ctx);
}

/**
 * dmr_bio_context_get_stats - Get bio context allocation statistics
 * @rc: Remap context
 * @stats_buf: Buffer to write statistics to
 * @buf_size: Size of statistics buffer
 * 
 * Returns detailed statistics about bio context allocation patterns.
 */
void dmr_bio_context_get_stats(struct remap_c *rc, char *stats_buf, size_t buf_size)
{
    if (!stats_buf || buf_size == 0) {
        return;
    }

    snprintf(stats_buf, buf_size,
             "Bio Context Statistics:\n"
             "  Fast pool allocs: %lld\n"
             "  Slow kmalloc allocs: %lld\n"
             "  Pool hits: %lld\n"
             "  Pool misses: %lld\n"
             "  Total contexts: %lld\n"
             "  Active contexts: %lld\n"
             "  Peak contexts: %lld\n"
             "  Pool efficiency: %lld%%\n",
             atomic64_read(&bio_stats.fast_allocs),
             atomic64_read(&bio_stats.slow_allocs),
             atomic64_read(&bio_stats.pool_hits),
             atomic64_read(&bio_stats.pool_misses),
             atomic64_read(&bio_stats.total_contexts),
             atomic64_read(&bio_stats.active_contexts),
             atomic64_read(&bio_stats.peak_contexts),
             atomic64_read(&bio_stats.total_contexts) > 0 ?
                (atomic64_read(&bio_stats.pool_hits) * 100) / 
                atomic64_read(&bio_stats.total_contexts) : 0);
}

/**
 * dmr_bio_context_init_stats - Initialize bio context statistics
 * 
 * Initializes the global bio context statistics counters.
 * Called during module initialization.
 */
void dmr_bio_context_init_stats(void)
{
    atomic64_set(&bio_stats.fast_allocs, 0);
    atomic64_set(&bio_stats.slow_allocs, 0);
    atomic64_set(&bio_stats.pool_hits, 0);
    atomic64_set(&bio_stats.pool_misses, 0);
    atomic64_set(&bio_stats.total_contexts, 0);
    atomic64_set(&bio_stats.active_contexts, 0);
    atomic64_set(&bio_stats.peak_contexts, 0);

    DMR_DEBUG("Bio context statistics initialized");
}

/**
 * dmr_bio_context_emergency_mode - Handle memory pressure
 * @rc: Remap context
 * @enable: Enable/disable emergency mode
 * 
 * Adjusts bio context allocation behavior during memory pressure.
 * In emergency mode, more aggressive pool management is used.
 */
void dmr_bio_context_emergency_mode(struct remap_c *rc, bool enable)
{
    if (!rc || !rc->pool_manager) {
        return;
    }

    /* Enable emergency mode in pool manager */
    dmr_pool_emergency_mode(rc, enable);

    DMR_DEBUG("Bio context emergency mode: %s", enable ? "ENABLED" : "DISABLED");
}

/**
 * Module initialization and cleanup for optimized bio context management
 */
static int __init dmr_bio_context_optimized_init(void)
{
    dmr_bio_context_init_stats();
    
    printk(KERN_INFO "dm-remap: Optimized bio context management initialized\n");
    return 0;
}

static void __exit dmr_bio_context_optimized_exit(void)
{
    u64 active = atomic64_read(&bio_stats.active_contexts);
    
    if (active > 0) {
        printk(KERN_WARNING "dm-remap: %lld bio contexts still active at module exit\n", active);
    }
    
    printk(KERN_INFO "dm-remap: Optimized bio context management cleanup complete\n");
}

module_init(dmr_bio_context_optimized_init);
module_exit(dmr_bio_context_optimized_exit);