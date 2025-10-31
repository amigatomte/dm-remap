/*
 * dm-remap-performance-optimization.h - Phase 3.2B Performance Tuning & Optimization
 * 
 * This file implements advanced performance optimizations based on profiler data
 * analysis and hot path optimization techniques.
 * 
 * KEY PHASE 3.2B FEATURES:
 * - CPU cache-optimized data structures
 * - Per-CPU performance counters (lock-free)
 * - Optimized memory allocation patterns
 * - Hot path micro-optimizations
 * - Profile-guided optimization hints
 * 
 * OPTIMIZATION TARGETS:
 * - Target: <100ns average I/O latency (from current 135ns)
 * - Target: >3.5 GB/s sequential throughput (from current 2.9 GB/s)
 * - Target: 50% reduction in CPU cycles per I/O
 * - Target: Better cache hit rates through data locality
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_PERFORMANCE_OPTIMIZATION_H
#define DM_REMAP_PERFORMANCE_OPTIMIZATION_H

#include <linux/percpu.h>
#include <linux/cache.h>
#include <linux/prefetch.h>
#include <linux/compiler.h>
#include <linux/atomic.h>
#include <linux/rbtree.h>

/* Phase 3.2B: Performance optimization constants */
#define DMR_PERF_OPT_CACHE_LINE_SIZE    64
#define DMR_PERF_OPT_PREFETCH_DISTANCE  4
#define DMR_PERF_OPT_BATCH_SIZE         8

/*
 * Phase 3.2B: Per-CPU Performance Counters
 * 
 * Using per-CPU counters eliminates atomic contention and improves
 * cache locality for performance tracking.
 */
struct dmr_percpu_stats {
    u64 total_ios;              /* Total I/O operations */
    u64 total_latency_ns;       /* Cumulative latency */
    u64 total_bytes;            /* Total bytes processed */
    u64 cache_hits;             /* Cache hits */
    u64 cache_misses;           /* Cache misses */
    u64 fast_path_hits;         /* Fast path utilization */
    u64 remap_lookups;          /* Remap table lookups */
    u64 lock_contentions;       /* Lock contention events */
} ____cacheline_aligned;

/*
 * Phase 3.2B: Cache-Optimized Remap Entry
 * 
 * Optimized for CPU cache line efficiency and memory prefetching.
 * Aligned to cache boundaries for optimal access patterns.
 */
struct dmr_optimized_remap_entry {
    sector_t main_lba;          /* Main device LBA */
    sector_t spare_lba;         /* Spare device LBA */
    u32 access_count;           /* Access frequency for LRU */
    u32 flags;                  /* Entry flags */
    ktime_t last_access;        /* Last access time */
    u8 padding[24];             /* Pad to cache line boundary */
} ____cacheline_aligned;

/*
 * Phase 3.2B: Red-Black Tree Node for Fast Lookups
 * 
 * Replace linear search with O(log n) red-black tree lookups
 * for better performance with large remap tables.
 */
struct dmr_rbtree_node {
    struct rb_node rb;          /* Red-black tree node */
    sector_t sector;            /* Search key */
    struct dmr_optimized_remap_entry *entry; /* Associated entry */
} ____cacheline_aligned;

/*
 * Phase 3.2B: Optimized Target Context
 * 
 * Cache-aligned and optimized data layout for better performance.
 */
struct dmr_optimized_context {
    /* Hot path data - first cache line */
    struct rb_root remap_tree ____cacheline_aligned;
    spinlock_t fast_lock;       /* Lightweight spinlock for fast path */
    u32 entry_count;            /* Number of entries */
    u32 optimization_flags;     /* Runtime optimization flags */
    
    /* Per-CPU statistics */
    struct dmr_percpu_stats __percpu *stats;
    
    /* Cold path data - separate cache lines */
    struct dmr_optimized_remap_entry *entries ____cacheline_aligned;
    size_t max_entries;
    rwlock_t slow_lock;         /* Reader-writer lock for slow path */
    
    /* Prefetch optimization data */
    sector_t last_sector;       /* Last accessed sector for locality */
    u32 sequential_count;       /* Sequential access counter */
} ____cacheline_aligned;

/* Phase 3.2B: Optimization flags */
#define DMR_OPT_FAST_PATH_ENABLED       (1 << 0)
#define DMR_OPT_PREFETCH_ENABLED        (1 << 1)
#define DMR_OPT_PERCPU_STATS_ENABLED    (1 << 2)
#define DMR_OPT_RBTREE_ENABLED          (1 << 3)
#define DMR_OPT_SEQUENTIAL_DETECTION    (1 << 4)

/* Phase 3.2B: Function declarations */

/* Initialization and cleanup */
int dmr_perf_opt_init(struct dmr_optimized_context *ctx, size_t max_entries);
void dmr_perf_opt_cleanup(struct dmr_optimized_context *ctx);

/* Optimized remap operations */
struct dmr_optimized_remap_entry *dmr_perf_opt_lookup_fast(
    struct dmr_optimized_context *ctx, sector_t sector);
int dmr_perf_opt_add_remap(struct dmr_optimized_context *ctx,
    sector_t main_lba, sector_t spare_lba);
int dmr_perf_opt_remove_remap(struct dmr_optimized_context *ctx, sector_t main_lba);

/* Performance tracking */
void dmr_perf_opt_update_stats(struct dmr_optimized_context *ctx,
    u64 ios, u64 latency_ns, u64 bytes, u64 hits, u64 misses);
void dmr_perf_opt_get_aggregated_stats(struct dmr_optimized_context *ctx,
    struct dmr_percpu_stats *result);

/* Hot path optimizations */
static inline void dmr_perf_opt_prefetch_remap_data(
    struct dmr_optimized_context *ctx, sector_t sector);
static inline bool dmr_perf_opt_is_sequential(
    struct dmr_optimized_context *ctx, sector_t sector);

/* Memory optimization helpers */
void dmr_perf_opt_optimize_memory_layout(struct dmr_optimized_context *ctx);
void dmr_perf_opt_compact_remap_table(struct dmr_optimized_context *ctx);

/*
 * Phase 3.2B: Inline Hot Path Functions
 * 
 * These functions are inlined for maximum performance in the I/O path.
 */

/**
 * dmr_perf_opt_prefetch_remap_data - Prefetch remap table data
 * @ctx: Optimized context
 * @sector: Target sector for prefetching
 * 
 * Prefetches likely-to-be-accessed remap table entries to improve
 * cache hit rates.
 */
static inline void dmr_perf_opt_prefetch_remap_data(
    struct dmr_optimized_context *ctx, sector_t sector)
{
    if (likely(ctx->optimization_flags & DMR_OPT_PREFETCH_ENABLED)) {
        /* Prefetch the rbtree root and likely nodes */
        prefetch(&ctx->remap_tree);
        
        /* Prefetch adjacent entries for spatial locality */
        if (ctx->entries) {
            int idx = sector % ctx->max_entries;
            prefetch(&ctx->entries[idx]);
            if (idx + 1 < ctx->max_entries)
                prefetch(&ctx->entries[idx + 1]);
        }
    }
}

/**
 * dmr_perf_opt_is_sequential - Detect sequential access patterns
 * @ctx: Optimized context
 * @sector: Current sector
 * 
 * Returns: true if this appears to be sequential access
 */
static inline bool dmr_perf_opt_is_sequential(
    struct dmr_optimized_context *ctx, sector_t sector)
{
    if (unlikely(!(ctx->optimization_flags & DMR_OPT_SEQUENTIAL_DETECTION)))
        return false;
    
    bool sequential = (sector == ctx->last_sector + 1);
    ctx->last_sector = sector;
    
    if (sequential) {
        ctx->sequential_count++;
    } else {
        ctx->sequential_count = 0;
    }
    
    return ctx->sequential_count >= DMR_PERF_OPT_PREFETCH_DISTANCE;
}

/**
 * dmr_perf_opt_update_percpu_stats - Update per-CPU statistics
 * @ctx: Optimized context
 * @ios: Number of I/Os
 * @latency_ns: Latency in nanoseconds
 * @bytes: Bytes processed
 * @hits: Cache hits
 * @misses: Cache misses
 * 
 * Updates per-CPU statistics without atomic operations for better performance.
 */
static inline void dmr_perf_opt_update_percpu_stats(
    struct dmr_optimized_context *ctx,
    u64 ios, u64 latency_ns, u64 bytes, u64 hits, u64 misses)
{
    struct dmr_percpu_stats *stats;
    
    if (unlikely(!(ctx->optimization_flags & DMR_OPT_PERCPU_STATS_ENABLED)))
        return;
    
    stats = this_cpu_ptr(ctx->stats);
    stats->total_ios += ios;
    stats->total_latency_ns += latency_ns;
    stats->total_bytes += bytes;
    stats->cache_hits += hits;
    stats->cache_misses += misses;
}

#endif /* DM_REMAP_PERFORMANCE_OPTIMIZATION_H */