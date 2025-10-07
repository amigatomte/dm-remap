/*
 * dm-remap-hotpath-optimization.h - I/O Hotpath Performance Optimization
 * 
 * Week 9-10 Advanced Optimization: High-performance I/O path optimization
 * Implements cache-aligned structures, fast-path detection, and streamlined
 * I/O processing to minimize latency in the critical I/O path.
 * 
 * Key optimizations:
 * - Cache-aligned data structures for better CPU cache utilization
 * - Fast-path detection to bypass unnecessary processing
 * - Streamlined I/O processing with minimal function calls
 * - Prefetching and branch prediction hints
 * - Lock-free operations where possible
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_HOTPATH_OPTIMIZATION_H
#define DM_REMAP_HOTPATH_OPTIMIZATION_H

#include <linux/types.h>
#include <linux/bio.h>
#include <linux/cache.h>
#include <linux/compiler.h>

/* Forward declarations */
struct remap_c;
struct dmr_bio_context;

/*
 * HOTPATH OPTIMIZATION CONSTANTS
 */
#define DMR_HOTPATH_CACHE_SIZE      64      /* CPU cache line size */
#define DMR_HOTPATH_PREFETCH_LINES  2       /* Number of cache lines to prefetch */
#define DMR_HOTPATH_BATCH_SIZE      16      /* I/O batch processing size */

/*
 * Fast-path eligibility flags
 */
#define DMR_FASTPATH_READ           0x01    /* Read operation eligible */
#define DMR_FASTPATH_WRITE          0x02    /* Write operation eligible */
#define DMR_FASTPATH_NO_REMAP       0x04    /* No remapping required */
#define DMR_FASTPATH_HEALTHY        0x08    /* Sector known healthy */
#define DMR_FASTPATH_CACHED         0x10    /* Sector info cached */

/*
 * Cache-aligned I/O context for hotpath operations
 * Aligned to cache boundaries for optimal performance
 */
struct dmr_hotpath_context {
    /* Hot cache line - frequently accessed data */
    sector_t sector;                    /* Current sector */
    u32 flags;                          /* Fast-path flags */
    u32 bio_size;                       /* Bio size in sectors */
    
    /* Performance counters - aligned */
    atomic64_t fast_reads ____cacheline_aligned;
    atomic64_t fast_writes;
    atomic64_t slow_path_fallbacks;
    atomic64_t cache_hits;
    
    /* Batch processing context */
    struct bio *batch_bios[DMR_HOTPATH_BATCH_SIZE];
    int batch_count;
    spinlock_t batch_lock;
    
    /* Prefetch targets */
    void *prefetch_targets[DMR_HOTPATH_PREFETCH_LINES];
} ____cacheline_aligned;

/*
 * Optimized remap entry with cache alignment
 * Designed for optimal cache line utilization
 */
struct dmr_hotpath_remap_entry {
    sector_t main_lba;                  /* Main sector */
    sector_t spare_lba;                 /* Spare sector */
    u32 access_count;                   /* Access frequency */
    u32 health_status;                  /* Cached health status */
} ____cacheline_aligned;

/*
 * Performance optimization statistics
 */
/* Statistics structure for external use (sysfs) */
struct dmr_hotpath_stats {
    u64 total_ios;               /* Total I/O operations */
    u64 fastpath_ios;            /* Fast-path I/Os */
    u64 cache_line_hits;         /* Cache line hits */
    u64 prefetch_hits;           /* Prefetch hits */
    u64 batch_processed;        /* Batch processed I/Os */
    u64 branch_mispredicts;      /* Branch misprediction estimates */
};

/* Hotpath optimization functions */
int dmr_hotpath_init(struct remap_c *rc);
/**
 * dmr_hotpath_get_stats - Get current hotpath statistics
 * @rc: Remap context
 * @stats: Statistics structure to fill
 */
void dmr_hotpath_get_stats(struct remap_c *rc, struct dmr_hotpath_stats *stats);

/**
 * dmr_hotpath_reset_stats - Reset hotpath statistics
 * @rc: Remap context
 */
void dmr_hotpath_reset_stats(struct remap_c *rc);

/**
 * dmr_hotpath_cleanup - Clean up hotpath optimization system
 * @rc: Remap context
 */
void dmr_hotpath_cleanup(struct remap_c *rc);

/* Fast-path detection and processing */
bool dmr_is_fastpath_eligible(struct bio *bio, struct remap_c *rc);
int dmr_process_fastpath_io(struct bio *bio, struct remap_c *rc);

/* Cache optimization functions */
void dmr_hotpath_prefetch_remap_data(struct remap_c *rc, sector_t sector);
void dmr_hotpath_update_access_pattern(struct remap_c *rc, sector_t sector);

/* Batch processing optimization */
int dmr_hotpath_batch_add(struct remap_c *rc, struct bio *bio);
void dmr_hotpath_batch_process(struct remap_c *rc);

/* Performance monitoring functions are declared above */

/*
 * OPTIMIZATION MACROS
 */

/* Branch prediction hints for hotpath */
#define dmr_likely_fastpath(x)      likely(x)
#define dmr_unlikely_slowpath(x)    unlikely(x)

/* Cache prefetch macro */
#define dmr_prefetch_remap_entry(entry) \
    do { \
        prefetch(entry); \
        prefetch((char *)(entry) + L1_CACHE_BYTES); \
    } while (0)

/* Cache-aligned allocation macro */
#define dmr_alloc_cache_aligned(size) \
    kmalloc((size), GFP_KERNEL | __GFP_ZERO | __GFP_COMP)

/* Fast sector range check */
static inline bool dmr_is_sector_in_range(sector_t sector, sector_t start, sector_t len)
{
    return dmr_likely_fastpath(sector >= start && sector < (start + len));
}

/* Fast health check */
static inline bool dmr_is_sector_healthy(struct remap_c *rc, sector_t sector)
{
    /* Fast path: assume healthy unless proven otherwise */
    return dmr_likely_fastpath(true);
}

/* Cache-friendly remap table lookup */
static inline struct dmr_hotpath_remap_entry *
dmr_hotpath_lookup_remap(struct remap_c *rc, sector_t sector)
{
    /* This would be implemented with optimized hash table or array lookup */
    return NULL; /* Placeholder */
}

/*
 * Hotpath I/O processing pipeline stages
 */
enum dmr_hotpath_stage {
    DMR_HOTPATH_STAGE_VALIDATE = 0,    /* Input validation */
    DMR_HOTPATH_STAGE_LOOKUP,          /* Remap table lookup */
    DMR_HOTPATH_STAGE_HEALTH_CHECK,    /* Health verification */
    DMR_HOTPATH_STAGE_DISPATCH,        /* I/O dispatch */
    DMR_HOTPATH_STAGE_COMPLETE,        /* Completion handling */
    DMR_HOTPATH_STAGE_MAX
};

/* Performance-critical inline functions */
static inline void dmr_hotpath_update_stats(struct dmr_hotpath_context *ctx, 
                                           enum dmr_hotpath_stage stage)
{
    switch (stage) {
    case DMR_HOTPATH_STAGE_VALIDATE:
        atomic64_inc(&ctx->fast_reads);
        break;
    case DMR_HOTPATH_STAGE_LOOKUP:
        atomic64_inc(&ctx->cache_hits);  
        break;
    default:
        break;
    }
}

/* Batch processing inline helpers */
static inline bool dmr_hotpath_batch_full(struct dmr_hotpath_context *ctx)
{
    return ctx->batch_count >= DMR_HOTPATH_BATCH_SIZE;
}

static inline void dmr_hotpath_batch_reset(struct dmr_hotpath_context *ctx)
{
    ctx->batch_count = 0;
}

#endif /* DM_REMAP_HOTPATH_OPTIMIZATION_H */