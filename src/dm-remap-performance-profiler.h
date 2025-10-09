/*
 * dm-remap-performance-profiler.h - Advanced Performance Profiling System
 * 
 * This module provides comprehensive performance profiling capabilities
 * for dm-remap v4.0, enabling detailed analysis of I/O paths, memory
 * usage, and optimization opportunities.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_PERFORMANCE_PROFILER_H
#define DM_REMAP_PERFORMANCE_PROFILER_H

#include <linux/types.h>
#include <linux/time64.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

/*
 * Performance Profiling Configuration
 */
#define DMR_PERF_MAX_SAMPLES        10000    /* Maximum samples per metric */
#define DMR_PERF_HOTPATH_SAMPLES    1000     /* Hot path sample buffer size */
#define DMR_PERF_HIST_BUCKETS       20       /* Histogram bucket count */

/*
 * Performance Metric Types
 */
enum dmr_perf_metric_type {
    DMR_PERF_IO_LATENCY = 0,        /* I/O operation latency */
    DMR_PERF_MEMORY_ALLOC,          /* Memory allocation performance */
    DMR_PERF_LOCK_CONTENTION,       /* Lock contention analysis */
    DMR_PERF_CACHE_PERFORMANCE,     /* Cache hit/miss rates */
    DMR_PERF_HOTPATH_TIMING,        /* Hot path execution timing */
    DMR_PERF_METRIC_COUNT           /* Total number of metric types */
};

/*
 * Performance Sample Structure
 */
struct dmr_perf_sample {
    u64 timestamp;          /* Sample timestamp (nanoseconds) */
    u64 value;             /* Measured value */
    u32 context;           /* Context information */
};

/*
 * Performance Histogram
 */
struct dmr_perf_histogram {
    u64 bucket_ranges[DMR_PERF_HIST_BUCKETS + 1];  /* Bucket boundaries */
    atomic64_t bucket_counts[DMR_PERF_HIST_BUCKETS]; /* Sample counts per bucket */
    u64 min_value;         /* Minimum observed value */
    u64 max_value;         /* Maximum observed value */
    atomic64_t total_samples; /* Total number of samples */
};

/*
 * Performance Statistics
 */
struct dmr_perf_stats {
    atomic64_t count;       /* Total sample count */
    atomic64_t sum;         /* Sum of all values */
    atomic64_t sum_squares; /* Sum of squares for variance calculation */
    u64 min;               /* Minimum value */
    u64 max;               /* Maximum value */
    
    /* Recent performance window */
    u64 window_start;      /* Window start timestamp */
    atomic64_t window_count; /* Samples in current window */
    atomic64_t window_sum;   /* Sum for current window */
};

/*
 * Hot Path Performance Tracker
 */
struct dmr_hotpath_profiler {
    struct dmr_perf_sample *samples;    /* Sample buffer */
    atomic_t sample_index;              /* Current sample index */
    spinlock_t lock;                    /* Synchronization */
    
    /* Fast path timing */
    u64 map_entry_time;                 /* Time when map function entered */
    u64 bio_submission_time;            /* Bio submission timestamp */
    u64 completion_time;                /* I/O completion timestamp */
    
    /* Performance counters */
    atomic64_t fast_path_count;         /* Fast path executions */
    atomic64_t slow_path_count;         /* Slow path executions */
    atomic64_t remap_path_count;        /* Remap path executions */
};

/*
 * Memory Performance Profiler
 */
struct dmr_memory_profiler {
    /* Pool performance tracking */
    atomic64_t pool_hits[3];            /* Pool cache hits by type */
    atomic64_t pool_misses[3];          /* Pool cache misses by type */
    atomic64_t pool_alloc_time[3];      /* Total allocation time by type */
    atomic64_t pool_free_time[3];       /* Total free time by type */
    
    /* Memory usage tracking */
    atomic64_t peak_memory_usage;       /* Peak memory usage */
    atomic64_t current_memory_usage;    /* Current memory usage */
    atomic64_t total_allocations;       /* Total allocations performed */
    atomic64_t total_frees;             /* Total frees performed */
    
    /* Fragmentation analysis */
    atomic64_t fragmentation_events;    /* Fragmentation occurrences */
    u64 last_gc_timestamp;             /* Last garbage collection time */
};

/*
 * Lock Performance Profiler
 */
struct dmr_lock_profiler {
    /* Spinlock contention tracking */
    atomic64_t lock_acquisitions;       /* Total lock acquisitions */
    atomic64_t lock_contentions;        /* Lock contention events */
    atomic64_t lock_hold_time;          /* Total time locks held */
    atomic64_t max_hold_time;           /* Maximum single hold time */
    
    /* Per-CPU lock statistics */
    atomic64_t per_cpu_acquisitions[NR_CPUS]; /* Per-CPU acquisition counts */
    u64 per_cpu_contention_time[NR_CPUS];     /* Per-CPU contention time */
};

/*
 * Main Performance Profiler Context
 */
struct dmr_performance_profiler {
    /* Enable/disable profiling */
    bool profiling_enabled;
    bool detailed_profiling;
    
    /* Core profilers */
    struct dmr_hotpath_profiler *hotpath;
    struct dmr_memory_profiler *memory;
    struct dmr_lock_profiler *locks;
    
    /* Performance statistics by metric type */
    struct dmr_perf_stats stats[DMR_PERF_METRIC_COUNT];
    struct dmr_perf_histogram histograms[DMR_PERF_METRIC_COUNT];
    
    /* Profiling control */
    u64 profiling_start_time;
    atomic64_t total_samples;
    
    /* Export interface */
    struct kobject kobj;                /* Sysfs interface */
};

/*
 * Performance Profiling Macros
 */
#define DMR_PERF_START_TIMING(profiler, start_var) \
    do { \
        if (profiler && profiler->profiling_enabled) \
            start_var = ktime_get_ns(); \
    } while (0)

#define DMR_PERF_END_TIMING(profiler, start_var, metric_type) \
    do { \
        if (profiler && profiler->profiling_enabled && start_var) { \
            u64 duration = ktime_get_ns() - start_var; \
            dmr_perf_record_sample(profiler, metric_type, duration, 0); \
        } \
    } while (0)

#define DMR_PERF_RECORD_EVENT(profiler, metric_type, value) \
    do { \
        if (profiler && profiler->profiling_enabled) \
            dmr_perf_record_sample(profiler, metric_type, value, 0); \
    } while (0)

#define DMR_PERF_INCREMENT_COUNTER(counter) \
    atomic64_inc(&(counter))

#define DMR_PERF_ADD_TO_COUNTER(counter, value) \
    atomic64_add(value, &(counter))

/*
 * Function Declarations
 */

/* Profiler lifecycle */
int dmr_perf_profiler_init(struct dmr_performance_profiler **profiler);
void dmr_perf_profiler_cleanup(struct dmr_performance_profiler *profiler);

/* Sample recording */
void dmr_perf_record_sample(struct dmr_performance_profiler *profiler,
                           enum dmr_perf_metric_type type, u64 value, u32 context);

/* Statistics calculation */
void dmr_perf_calculate_stats(struct dmr_performance_profiler *profiler);
void dmr_perf_reset_stats(struct dmr_performance_profiler *profiler);

/* Hot path profiling */
void dmr_perf_hotpath_enter(struct dmr_performance_profiler *profiler);
void dmr_perf_hotpath_exit(struct dmr_performance_profiler *profiler);
void dmr_perf_hotpath_remap(struct dmr_performance_profiler *profiler);

/* Memory profiling */
void dmr_perf_memory_alloc(struct dmr_performance_profiler *profiler, 
                          int pool_type, size_t size, u64 alloc_time);
void dmr_perf_memory_free(struct dmr_performance_profiler *profiler,
                         int pool_type, size_t size, u64 free_time);

/* Lock profiling */
void dmr_perf_lock_acquired(struct dmr_performance_profiler *profiler, u64 wait_time);
void dmr_perf_lock_released(struct dmr_performance_profiler *profiler, u64 hold_time);

/* Export functions */
int dmr_perf_export_stats(struct dmr_performance_profiler *profiler, char *buffer, size_t size);
int dmr_perf_export_histograms(struct dmr_performance_profiler *profiler, char *buffer, size_t size);

#endif /* DM_REMAP_PERFORMANCE_PROFILER_H */