/*
 * dm-remap-io-optimized.h - Phase 3.2B Optimized I/O Processing Interface
 * 
 * This header defines the interface for the Phase 3.2B optimized I/O
 * processing system, including performance structures and function declarations.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_IO_OPTIMIZED_H
#define DM_REMAP_IO_OPTIMIZED_H

#include <linux/types.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>

/* Forward declarations */
struct remap_c;

/**
 * struct dmr_io_optimization_stats - Phase 3.2B I/O optimization statistics
 * 
 * This structure contains comprehensive statistics about the performance
 * optimization system's effectiveness.
 */
struct dmr_io_optimization_stats {
    /* Fast/slow path statistics */
    u64 fast_path_hits;        /* Fast path lookup hits */
    u64 slow_path_hits;        /* Slow path lookup hits */
    u64 total_lookups;         /* Total remap lookups */
    u64 fast_path_hit_rate;    /* Fast path hit rate (%) */
    
    /* Per-CPU aggregated statistics */
    u64 percpu_total_ios;      /* Total I/O operations */
    u64 percpu_total_latency_ns; /* Total latency */
    u64 percpu_total_bytes;    /* Total bytes processed */
    u64 percpu_cache_hits;     /* Cache hits */
    u64 percpu_cache_misses;   /* Cache misses */
    u64 percpu_remap_lookups;  /* Remap lookups */
    u64 avg_latency_ns;        /* Average latency */
    
    /* Configuration and capacity */
    u32 optimization_flags;    /* Active optimization flags */
    u32 remap_entries;         /* Current remap entries */
    u32 max_entries;           /* Maximum remap entries */
};

/* Function declarations */

/* Core I/O processing */
int dmr_io_optimized_process(struct dm_target *ti, struct bio *bio);

/* Remap management */
int dmr_io_optimized_add_remap(struct remap_c *rc, sector_t main_lba, sector_t spare_lba);
int dmr_io_optimized_remove_remap(struct remap_c *rc, sector_t main_lba);

/* Statistics and monitoring */
void dmr_io_optimized_get_stats(struct dmr_io_optimization_stats *stats);

/* Initialization and cleanup */
int dmr_io_optimized_init(size_t max_entries);
void dmr_io_optimized_cleanup(void);

/* Runtime optimization control */
void dmr_io_optimized_optimize_layout(void);
void dmr_io_optimized_set_flags(u32 flags);
u32 dmr_io_optimized_get_flags(void);

#endif /* DM_REMAP_IO_OPTIMIZED_H */