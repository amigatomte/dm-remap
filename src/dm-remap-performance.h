/*
 * dm-remap-performance.h - Performance optimization for dm-remap v2.0
 * 
 * This header defines performance optimization features including:
 * - Hot path optimization and fast paths for common operations
 * - Latency reduction techniques and minimal overhead tracking
 * - CPU cache optimization and memory layout improvements
 * - Interrupt context optimization for bio endio callbacks
 * - Bulk operation support for high-throughput scenarios
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_PERFORMANCE_H
#define DM_REMAP_PERFORMANCE_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/cache.h>

/*
 * PERFORMANCE OPTIMIZATION FLAGS
 */
#define DMR_PERF_FAST_PATH          0x01  /* Use optimized fast path */
#define DMR_PERF_MINIMAL_TRACKING   0x02  /* Reduce tracking overhead */
#define DMR_PERF_BULK_OPERATIONS    0x04  /* Enable bulk processing */
#define DMR_PERF_CACHE_OPTIMIZED    0x08  /* CPU cache optimizations */
#define DMR_PERF_LOW_LATENCY        0x10  /* Low latency mode */

/*
 * PERFORMANCE COUNTERS (Per-CPU for scalability)
 */
struct dmr_perf_counters {
    u64 fast_path_hits;          /* Fast path usage count */
    u64 slow_path_hits;          /* Slow path usage count */
    u64 bio_contexts_allocated;  /* Bio contexts allocated */
    u64 bio_contexts_reused;     /* Bio contexts reused */
    u64 cache_hits;              /* Cache optimization hits */
    u64 bulk_operations;         /* Bulk operations performed */
} ____cacheline_aligned;

/*
 * PERFORMANCE CONFIGURATION
 */
struct dmr_perf_config {
    u32 optimization_flags;      /* Performance optimization flags */
    u32 fast_path_threshold;     /* Threshold for fast path eligibility */
    u32 bulk_batch_size;         /* Batch size for bulk operations */
    u32 cache_line_size;         /* CPU cache line size */
    u32 max_tracking_contexts;   /* Maximum tracking contexts */
    bool enable_percpu_counters; /* Enable per-CPU performance counters */
};

/*
 * OPTIMIZED BIO CONTEXT (Cache-line aligned for performance)
 */
struct dmr_bio_context_fast {
    struct remap_c *rc;          /* Target context */
    sector_t lba;                /* Logical block address */
    u32 flags;                   /* Fast path flags */
    bio_end_io_t *orig_endio;    /* Original endio callback */
} ____cacheline_aligned;

/*
 * PERFORMANCE OPTIMIZATION FUNCTIONS
 */

/* Fast path optimization */
bool dmr_is_fast_path_eligible(struct bio *bio, struct remap_c *rc);
int dmr_process_fast_path(struct bio *bio, struct remap_c *rc);
void dmr_optimize_bio_tracking(struct bio *bio, struct remap_c *rc);

/* Performance monitoring */
void dmr_perf_update_counters(struct remap_c *rc, u32 event_type);
u64 dmr_perf_get_counter(struct remap_c *rc, u32 counter_type);
void dmr_perf_reset_counters(struct remap_c *rc);

/* Memory optimization */
void dmr_optimize_memory_layout(struct remap_c *rc);
struct dmr_bio_context_fast *dmr_alloc_fast_context(struct remap_c *rc);
void dmr_free_fast_context(struct dmr_bio_context_fast *ctx);

/* Bulk operations */
int dmr_process_bulk_ios(struct bio **bios, int count, struct remap_c *rc);
bool dmr_can_batch_ios(struct bio *bio1, struct bio *bio2);

/* Cache optimization */
void dmr_prefetch_remap_table(struct remap_c *rc, sector_t lba);
void dmr_optimize_data_structures(struct remap_c *rc);

/* Performance initialization */
int dmr_perf_init(struct remap_c *rc);
void dmr_perf_cleanup(struct remap_c *rc);

#endif /* DM_REMAP_PERFORMANCE_H */