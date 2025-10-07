/*
 * dm-remap-performance.h - Performance optimization for dm-remap v4.0
 */

#ifndef DM_REMAP_PERFORMANCE_H
#define DM_REMAP_PERFORMANCE_H

#include <linux/types.h>
#include <linux/bio.h>

/* Forward declarations */
struct remap_c;

/* Performance optimization flags */
#define DMR_PERF_FAST_PATH          0x01
#define DMR_PERF_MINIMAL_TRACKING   0x02
#define DMR_PERF_BULK_OPERATIONS    0x04
#define DMR_PERF_CACHE_OPTIMIZED    0x08
#define DMR_PERF_LOW_LATENCY        0x10

/* Performance counters */
struct dmr_perf_counters {
    u64 fast_path_hits;
    u64 slow_path_hits;
    u64 bio_contexts_allocated;
    u64 bio_contexts_reused;
    u64 cache_hits;
    u64 bulk_operations;
} ____cacheline_aligned;

/* Function prototypes */
bool dmr_is_fast_path_eligible(struct bio *bio, struct remap_c *rc);
int dmr_process_fast_path(struct bio *bio, struct remap_c *rc);
void dmr_optimize_bio_tracking(struct bio *bio, struct remap_c *rc);
void dmr_perf_update_counters(struct remap_c *rc, u32 event_type);
u64 dmr_perf_get_counter(struct remap_c *rc, u32 counter_type);
void dmr_optimize_memory_layout(struct remap_c *rc);
void dmr_prefetch_remap_table(struct remap_c *rc, sector_t lba);
int dmr_init_allocation_cache(struct remap_c *rc);
void dmr_cleanup_allocation_cache(struct remap_c *rc);
sector_t dmr_allocate_spare_sector_optimized(struct remap_c *rc);
void dmr_get_performance_stats(struct remap_c *rc, char *stats, size_t max_len);
int dmr_perf_init(struct remap_c *rc);
void dmr_perf_cleanup(struct remap_c *rc);

#endif /* DM_REMAP_PERFORMANCE_H */
