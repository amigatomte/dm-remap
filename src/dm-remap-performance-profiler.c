/*
 * dm-remap-performance-profiler.c - Advanced Performance Profiling Implementation
 * 
 * This module implements comprehensive performance profiling capabilities
 * for dm-remap v4.0, providing detailed I/O path analysis and optimization
 * insights.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time64.h>
#include <linux/cpu.h>
#include <linux/string.h>
#include "dm-remap-performance-profiler.h"
#include "dm-remap-core.h"

/*
 * Initialize Performance Profiler
 */
int dmr_perf_profiler_init(struct dmr_performance_profiler **profiler)
{
    struct dmr_performance_profiler *prof;
    int i, ret = 0;
    
    prof = kzalloc(sizeof(struct dmr_performance_profiler), GFP_KERNEL);
    if (!prof)
        return -ENOMEM;
    
    /* Initialize hot path profiler */
    prof->hotpath = kzalloc(sizeof(struct dmr_hotpath_profiler), GFP_KERNEL);
    if (!prof->hotpath) {
        ret = -ENOMEM;
        goto cleanup_main;
    }
    
    prof->hotpath->samples = vzalloc(sizeof(struct dmr_perf_sample) * DMR_PERF_HOTPATH_SAMPLES);
    if (!prof->hotpath->samples) {
        ret = -ENOMEM;
        goto cleanup_hotpath;
    }
    
    spin_lock_init(&prof->hotpath->lock);
    atomic_set(&prof->hotpath->sample_index, 0);
    
    /* Initialize memory profiler */
    prof->memory = kzalloc(sizeof(struct dmr_memory_profiler), GFP_KERNEL);
    if (!prof->memory) {
        ret = -ENOMEM;
        goto cleanup_hotpath_samples;
    }
    
    /* Initialize lock profiler */
    prof->locks = kzalloc(sizeof(struct dmr_lock_profiler), GFP_KERNEL);
    if (!prof->locks) {
        ret = -ENOMEM;
        goto cleanup_memory;
    }
    
    /* Initialize histograms */
    for (i = 0; i < DMR_PERF_METRIC_COUNT; i++) {
        int j;
        struct dmr_perf_histogram *hist = &prof->histograms[i];
        
        /* Initialize histogram buckets (exponential distribution) */
        for (j = 0; j <= DMR_PERF_HIST_BUCKETS; j++) {
            hist->bucket_ranges[j] = (1ULL << j) * 100; /* 100ns base */
        }
        
        hist->min_value = ULLONG_MAX;
        hist->max_value = 0;
        atomic64_set(&hist->total_samples, 0);
        
        /* Initialize bucket counters */
        for (j = 0; j < DMR_PERF_HIST_BUCKETS; j++) {
            atomic64_set(&hist->bucket_counts[j], 0);
        }
    }
    
    /* Initialize statistics */
    for (i = 0; i < DMR_PERF_METRIC_COUNT; i++) {
        struct dmr_perf_stats *stats = &prof->stats[i];
        atomic64_set(&stats->count, 0);
        atomic64_set(&stats->sum, 0);
        atomic64_set(&stats->sum_squares, 0);
        stats->min = ULLONG_MAX;
        stats->max = 0;
        stats->window_start = ktime_get_ns();
        atomic64_set(&stats->window_count, 0);
        atomic64_set(&stats->window_sum, 0);
    }
    
    /* Enable profiling */
    prof->profiling_enabled = true;
    prof->detailed_profiling = false; /* Can be enabled via sysfs */
    prof->profiling_start_time = ktime_get_ns();
    atomic64_set(&prof->total_samples, 0);
    
    *profiler = prof;
    
    printk(KERN_INFO "dm-remap: Performance profiler initialized successfully\n");
    return 0;
    
cleanup_memory:
    kfree(prof->memory);
cleanup_hotpath_samples:
    vfree(prof->hotpath->samples);
cleanup_hotpath:
    kfree(prof->hotpath);
cleanup_main:
    kfree(prof);
    return ret;
}

/*
 * Cleanup Performance Profiler
 */
void dmr_perf_profiler_cleanup(struct dmr_performance_profiler *profiler)
{
    if (!profiler)
        return;
    
    profiler->profiling_enabled = false;
    
    if (profiler->hotpath) {
        if (profiler->hotpath->samples)
            vfree(profiler->hotpath->samples);
        kfree(profiler->hotpath);
    }
    
    if (profiler->memory)
        kfree(profiler->memory);
    
    if (profiler->locks)
        kfree(profiler->locks);
    
    kfree(profiler);
    printk(KERN_INFO "dm-remap: Performance profiler cleaned up\n");
}

/*
 * Record Performance Sample
 */
void dmr_perf_record_sample(struct dmr_performance_profiler *profiler,
                           enum dmr_perf_metric_type type, u64 value, u32 context)
{
    struct dmr_perf_stats *stats;
    struct dmr_perf_histogram *hist;
    int bucket;
    
    if (!profiler || !profiler->profiling_enabled || type >= DMR_PERF_METRIC_COUNT)
        return;
    
    stats = &profiler->stats[type];
    hist = &profiler->histograms[type];
    
    /* Update statistics */
    atomic64_inc(&stats->count);
    atomic64_add(value, &stats->sum);
    atomic64_add(value * value, &stats->sum_squares);
    
    /* Update min/max (simple approach, might have race conditions but acceptable for profiling) */
    if (value < stats->min)
        stats->min = value;
    if (value > stats->max)
        stats->max = value;
    
    /* Update histogram */
    atomic64_inc(&hist->total_samples);
    if (value < hist->min_value)
        hist->min_value = value;
    if (value > hist->max_value)
        hist->max_value = value;
    
    /* Find appropriate histogram bucket */
    for (bucket = 0; bucket < DMR_PERF_HIST_BUCKETS; bucket++) {
        if (value < hist->bucket_ranges[bucket + 1]) {
            atomic64_inc(&hist->bucket_counts[bucket]);
            break;
        }
    }
    
    /* Update window statistics */
    atomic64_inc(&stats->window_count);
    atomic64_add(value, &stats->window_sum);
    
    /* Increment total sample counter */
    atomic64_inc(&profiler->total_samples);
}

/*
 * Hot Path Profiling Functions
 */
void dmr_perf_hotpath_enter(struct dmr_performance_profiler *profiler)
{
    if (!profiler || !profiler->profiling_enabled || !profiler->hotpath)
        return;
    
    profiler->hotpath->map_entry_time = ktime_get_ns();
    atomic64_inc(&profiler->hotpath->fast_path_count);
}

void dmr_perf_hotpath_exit(struct dmr_performance_profiler *profiler)
{
    u64 duration;
    
    if (!profiler || !profiler->profiling_enabled || !profiler->hotpath)
        return;
    
    if (profiler->hotpath->map_entry_time) {
        duration = ktime_get_ns() - profiler->hotpath->map_entry_time;
        dmr_perf_record_sample(profiler, DMR_PERF_HOTPATH_TIMING, duration, 0);
        profiler->hotpath->map_entry_time = 0;
    }
}

void dmr_perf_hotpath_remap(struct dmr_performance_profiler *profiler)
{
    if (!profiler || !profiler->profiling_enabled || !profiler->hotpath)
        return;
    
    atomic64_inc(&profiler->hotpath->remap_path_count);
}

/*
 * Memory Profiling Functions
 */
void dmr_perf_memory_alloc(struct dmr_performance_profiler *profiler, 
                          int pool_type, size_t size, u64 alloc_time)
{
    if (!profiler || !profiler->profiling_enabled || !profiler->memory)
        return;
    
    if (pool_type >= 0 && pool_type < 3) {
        if (alloc_time > 0) {
            atomic64_inc(&profiler->memory->pool_hits[pool_type]);
            atomic64_add(alloc_time, &profiler->memory->pool_alloc_time[pool_type]);
        } else {
            atomic64_inc(&profiler->memory->pool_misses[pool_type]);
        }
    }
    
    atomic64_inc(&profiler->memory->total_allocations);
    atomic64_add(size, &profiler->memory->current_memory_usage);
    
    /* Update peak memory usage */
    if (atomic64_read(&profiler->memory->current_memory_usage) > 
        atomic64_read(&profiler->memory->peak_memory_usage)) {
        atomic64_set(&profiler->memory->peak_memory_usage,
                    atomic64_read(&profiler->memory->current_memory_usage));
    }
    
    dmr_perf_record_sample(profiler, DMR_PERF_MEMORY_ALLOC, alloc_time, pool_type);
}

void dmr_perf_memory_free(struct dmr_performance_profiler *profiler,
                         int pool_type, size_t size, u64 free_time)
{
    if (!profiler || !profiler->profiling_enabled || !profiler->memory)
        return;
    
    if (pool_type >= 0 && pool_type < 3) {
        atomic64_add(free_time, &profiler->memory->pool_free_time[pool_type]);
    }
    
    atomic64_inc(&profiler->memory->total_frees);
    atomic64_sub(size, &profiler->memory->current_memory_usage);
}

/*
 * Lock Profiling Functions
 */
void dmr_perf_lock_acquired(struct dmr_performance_profiler *profiler, u64 wait_time)
{
    int cpu;
    
    if (!profiler || !profiler->profiling_enabled || !profiler->locks)
        return;
    
    atomic64_inc(&profiler->locks->lock_acquisitions);
    
    if (wait_time > 0) {
        atomic64_inc(&profiler->locks->lock_contentions);
        dmr_perf_record_sample(profiler, DMR_PERF_LOCK_CONTENTION, wait_time, 0);
    }
    
    cpu = smp_processor_id();
    if (cpu < NR_CPUS) {
        atomic64_inc(&profiler->locks->per_cpu_acquisitions[cpu]);
    }
}

void dmr_perf_lock_released(struct dmr_performance_profiler *profiler, u64 hold_time)
{
    if (!profiler || !profiler->profiling_enabled || !profiler->locks)
        return;
    
    atomic64_add(hold_time, &profiler->locks->lock_hold_time);
    
    if (hold_time > atomic64_read(&profiler->locks->max_hold_time)) {
        atomic64_set(&profiler->locks->max_hold_time, hold_time);
    }
}

/*
 * Statistics Export Functions
 */
int dmr_perf_export_stats(struct dmr_performance_profiler *profiler, char *buffer, size_t size)
{
    int written = 0;
    int i;
    
    if (!profiler || !buffer)
        return -EINVAL;
    
    written += snprintf(buffer + written, size - written,
        "=== dm-remap Performance Statistics ===\n");
    written += snprintf(buffer + written, size - written,
        "Profiling Duration: %llu ns\n", 
        ktime_get_ns() - profiler->profiling_start_time);
    written += snprintf(buffer + written, size - written,
        "Total Samples: %lld\n\n", 
        atomic64_read(&profiler->total_samples));
    
    /* Export statistics for each metric type */
    for (i = 0; i < DMR_PERF_METRIC_COUNT; i++) {
        struct dmr_perf_stats *stats = &profiler->stats[i];
        u64 count = atomic64_read(&stats->count);
        u64 sum = atomic64_read(&stats->sum);
        u64 avg = count > 0 ? sum / count : 0;
        
        if (count == 0) continue;
        
        written += snprintf(buffer + written, size - written,
            "Metric %d: Count=%lld, Avg=%lld ns, Min=%lld ns, Max=%lld ns\n",
            i, count, avg, stats->min, stats->max);
    }
    
    /* Export hot path statistics */
    if (profiler->hotpath) {
        written += snprintf(buffer + written, size - written,
            "\n=== Hot Path Statistics ===\n");
        written += snprintf(buffer + written, size - written,
            "Fast Path Count: %lld\n", 
            atomic64_read(&profiler->hotpath->fast_path_count));
        written += snprintf(buffer + written, size - written,
            "Remap Path Count: %lld\n", 
            atomic64_read(&profiler->hotpath->remap_path_count));
    }
    
    /* Export memory statistics */
    if (profiler->memory) {
        written += snprintf(buffer + written, size - written,
            "\n=== Memory Statistics ===\n");
        written += snprintf(buffer + written, size - written,
            "Peak Memory Usage: %lld bytes\n", 
            atomic64_read(&profiler->memory->peak_memory_usage));
        written += snprintf(buffer + written, size - written,
            "Current Memory Usage: %lld bytes\n", 
            atomic64_read(&profiler->memory->current_memory_usage));
        written += snprintf(buffer + written, size - written,
            "Total Allocations: %lld\n", 
            atomic64_read(&profiler->memory->total_allocations));
        written += snprintf(buffer + written, size - written,
            "Total Frees: %lld\n", 
            atomic64_read(&profiler->memory->total_frees));
    }
    
    /* Export lock statistics */
    if (profiler->locks) {
        written += snprintf(buffer + written, size - written,
            "\n=== Lock Statistics ===\n");
        written += snprintf(buffer + written, size - written,
            "Lock Acquisitions: %lld\n", 
            atomic64_read(&profiler->locks->lock_acquisitions));
        written += snprintf(buffer + written, size - written,
            "Lock Contentions: %lld\n", 
            atomic64_read(&profiler->locks->lock_contentions));
        written += snprintf(buffer + written, size - written,
            "Max Hold Time: %lld ns\n", 
            atomic64_read(&profiler->locks->max_hold_time));
    }
    
    return written;
}

/*
 * Reset Statistics
 */
void dmr_perf_reset_stats(struct dmr_performance_profiler *profiler)
{
    int i, j;
    
    if (!profiler)
        return;
    
    /* Reset core statistics */
    for (i = 0; i < DMR_PERF_METRIC_COUNT; i++) {
        struct dmr_perf_stats *stats = &profiler->stats[i];
        struct dmr_perf_histogram *hist = &profiler->histograms[i];
        
        atomic64_set(&stats->count, 0);
        atomic64_set(&stats->sum, 0);
        atomic64_set(&stats->sum_squares, 0);
        stats->min = ULLONG_MAX;
        stats->max = 0;
        stats->window_start = ktime_get_ns();
        atomic64_set(&stats->window_count, 0);
        atomic64_set(&stats->window_sum, 0);
        
        /* Reset histogram */
        hist->min_value = ULLONG_MAX;
        hist->max_value = 0;
        atomic64_set(&hist->total_samples, 0);
        for (j = 0; j < DMR_PERF_HIST_BUCKETS; j++) {
            atomic64_set(&hist->bucket_counts[j], 0);
        }
    }
    
    /* Reset hot path statistics */
    if (profiler->hotpath) {
        atomic64_set(&profiler->hotpath->fast_path_count, 0);
        atomic64_set(&profiler->hotpath->slow_path_count, 0);
        atomic64_set(&profiler->hotpath->remap_path_count, 0);
        atomic_set(&profiler->hotpath->sample_index, 0);
    }
    
    /* Reset memory statistics */
    if (profiler->memory) {
        for (i = 0; i < 3; i++) {
            atomic64_set(&profiler->memory->pool_hits[i], 0);
            atomic64_set(&profiler->memory->pool_misses[i], 0);
            atomic64_set(&profiler->memory->pool_alloc_time[i], 0);
            atomic64_set(&profiler->memory->pool_free_time[i], 0);
        }
        atomic64_set(&profiler->memory->peak_memory_usage, 0);
        atomic64_set(&profiler->memory->total_allocations, 0);
        atomic64_set(&profiler->memory->total_frees, 0);
        atomic64_set(&profiler->memory->fragmentation_events, 0);
    }
    
    /* Reset lock statistics */
    if (profiler->locks) {
        atomic64_set(&profiler->locks->lock_acquisitions, 0);
        atomic64_set(&profiler->locks->lock_contentions, 0);
        atomic64_set(&profiler->locks->lock_hold_time, 0);
        atomic64_set(&profiler->locks->max_hold_time, 0);
        
        for (i = 0; i < NR_CPUS; i++) {
            atomic64_set(&profiler->locks->per_cpu_acquisitions[i], 0);
            profiler->locks->per_cpu_contention_time[i] = 0;
        }
    }
    
    /* Reset global counters */
    profiler->profiling_start_time = ktime_get_ns();
    atomic64_set(&profiler->total_samples, 0);
    
    printk(KERN_INFO "dm-remap: Performance statistics reset\n");
}