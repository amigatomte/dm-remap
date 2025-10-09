/*
 * dm-remap-optimization-sysfs.c - Phase 3.2B Optimization Monitoring Interface
 * 
 * This file implements a comprehensive sysfs interface for monitoring and
 * controlling the Phase 3.2B performance optimizations.
 * 
 * MONITORING CAPABILITIES:
 * - Real-time optimization statistics
 * - Per-CPU performance counters
 * - Fast/slow path hit rates
 * - Memory layout optimization status
 * - Runtime optimization flag control
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dm-remap-core.h"
#include "dm-remap-io-optimized.h"
#include "dm-remap-performance-optimization.h"

/* Phase 3.2B: Sysfs kobject for optimization monitoring */
static struct kobject *dmr_opt_kobj = NULL;

/*
 * Phase 3.2B: Optimization Statistics Attributes
 */

/**
 * opt_fast_path_hits_show - Show fast path hit count
 */
static ssize_t opt_fast_path_hits_show(struct kobject *kobj,
                                      struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.fast_path_hits);
}

/**
 * opt_slow_path_hits_show - Show slow path hit count
 */
static ssize_t opt_slow_path_hits_show(struct kobject *kobj,
                                      struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.slow_path_hits);
}

/**
 * opt_total_lookups_show - Show total lookup count
 */
static ssize_t opt_total_lookups_show(struct kobject *kobj,
                                     struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.total_lookups);
}

/**
 * opt_fast_path_hit_rate_show - Show fast path hit rate percentage
 */
static ssize_t opt_fast_path_hit_rate_show(struct kobject *kobj,
                                          struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu%%\n", stats.fast_path_hit_rate);
}

/**
 * opt_percpu_total_ios_show - Show per-CPU total I/O operations
 */
static ssize_t opt_percpu_total_ios_show(struct kobject *kobj,
                                        struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.percpu_total_ios);
}

/**
 * opt_percpu_avg_latency_ns_show - Show per-CPU average latency
 */
static ssize_t opt_percpu_avg_latency_ns_show(struct kobject *kobj,
                                             struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.avg_latency_ns);
}

/**
 * opt_percpu_total_bytes_show - Show per-CPU total bytes processed
 */
static ssize_t opt_percpu_total_bytes_show(struct kobject *kobj,
                                          struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.percpu_total_bytes);
}

/**
 * opt_percpu_cache_hits_show - Show per-CPU cache hits
 */
static ssize_t opt_percpu_cache_hits_show(struct kobject *kobj,
                                         struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.percpu_cache_hits);
}

/**
 * opt_percpu_cache_misses_show - Show per-CPU cache misses
 */
static ssize_t opt_percpu_cache_misses_show(struct kobject *kobj,
                                           struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.percpu_cache_misses);
}

/**
 * opt_percpu_remap_lookups_show - Show per-CPU remap lookups
 */
static ssize_t opt_percpu_remap_lookups_show(struct kobject *kobj,
                                            struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%llu\n", stats.percpu_remap_lookups);
}

/**
 * opt_remap_entries_show - Show current remap entry count
 */
static ssize_t opt_remap_entries_show(struct kobject *kobj,
                                     struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    
    dmr_io_optimized_get_stats(&stats);
    return sprintf(buf, "%u/%u\n", stats.remap_entries, stats.max_entries);
}

/*
 * Phase 3.2B: Optimization Control Attributes
 */

/**
 * opt_flags_show - Show current optimization flags
 */
static ssize_t opt_flags_show(struct kobject *kobj,
                             struct kobj_attribute *attr, char *buf)
{
    u32 flags = dmr_io_optimized_get_flags();
    
    return sprintf(buf, "0x%08x\n"
                   "  DMR_OPT_FAST_PATH_ENABLED:     %s\n"
                   "  DMR_OPT_PREFETCH_ENABLED:      %s\n"
                   "  DMR_OPT_PERCPU_STATS_ENABLED:  %s\n"
                   "  DMR_OPT_RBTREE_ENABLED:        %s\n"
                   "  DMR_OPT_SEQUENTIAL_DETECTION:  %s\n",
                   flags,
                   (flags & DMR_OPT_FAST_PATH_ENABLED) ? "YES" : "NO",
                   (flags & DMR_OPT_PREFETCH_ENABLED) ? "YES" : "NO",
                   (flags & DMR_OPT_PERCPU_STATS_ENABLED) ? "YES" : "NO",
                   (flags & DMR_OPT_RBTREE_ENABLED) ? "YES" : "NO",
                   (flags & DMR_OPT_SEQUENTIAL_DETECTION) ? "YES" : "NO");
}

/**
 * opt_flags_store - Set optimization flags
 */
static ssize_t opt_flags_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf, size_t count)
{
    u32 flags;
    int ret;
    
    ret = kstrtou32(buf, 0, &flags);
    if (ret)
        return ret;
    
    dmr_io_optimized_set_flags(flags);
    
    DMR_DEBUG(1, "Phase 3.2B optimization flags updated: 0x%08x", flags);
    
    return count;
}

/**
 * opt_optimize_layout_store - Trigger memory layout optimization
 */
static ssize_t opt_optimize_layout_store(struct kobject *kobj,
                                        struct kobj_attribute *attr,
                                        const char *buf, size_t count)
{
    int trigger;
    int ret;
    
    ret = kstrtoint(buf, 10, &trigger);
    if (ret)
        return ret;
    
    if (trigger == 1) {
        dmr_io_optimized_optimize_layout();
        DMR_DEBUG(1, "Phase 3.2B memory layout optimization triggered");
    }
    
    return count;
}

/**
 * opt_comprehensive_stats_show - Show comprehensive optimization statistics
 */
static ssize_t opt_comprehensive_stats_show(struct kobject *kobj,
                                           struct kobj_attribute *attr, char *buf)
{
    struct dmr_io_optimization_stats stats;
    u64 cache_hit_rate = 0;
    u64 total_mb = 0;
    
    dmr_io_optimized_get_stats(&stats);
    
    /* Calculate cache hit rate */
    if (stats.percpu_cache_hits + stats.percpu_cache_misses > 0) {
        cache_hit_rate = (stats.percpu_cache_hits * 100) / 
                        (stats.percpu_cache_hits + stats.percpu_cache_misses);
    }
    
    /* Convert bytes to MB */
    total_mb = stats.percpu_total_bytes / (1024 * 1024);
    
    return sprintf(buf,
        "=== Phase 3.2B Optimization Statistics ===\n"
        "\n"
        "Fast Path Performance:\n"
        "  Fast Path Hits:       %llu\n"
        "  Slow Path Hits:       %llu\n"
        "  Total Lookups:        %llu\n"
        "  Fast Path Hit Rate:   %llu%%\n"
        "\n"
        "Per-CPU Statistics:\n"
        "  Total I/Os:           %llu\n"
        "  Average Latency:      %llu ns\n"
        "  Total Data:           %llu MB\n"
        "  Cache Hits:           %llu\n"
        "  Cache Misses:         %llu\n"
        "  Cache Hit Rate:       %llu%%\n"
        "  Remap Lookups:        %llu\n"
        "\n"
        "Configuration:\n"
        "  Optimization Flags:   0x%08x\n"
        "  Remap Entries:        %u/%u\n"
        "\n"
        "Performance Analysis:\n"
        "  Latency Target:       <100ns (Current: %llu ns)\n"
        "  Throughput:           %llu MB processed\n"
        "  Efficiency:           %llu%% fast path usage\n",
        
        stats.fast_path_hits,
        stats.slow_path_hits,
        stats.total_lookups,
        stats.fast_path_hit_rate,
        
        stats.percpu_total_ios,
        stats.avg_latency_ns,
        total_mb,
        stats.percpu_cache_hits,
        stats.percpu_cache_misses,
        cache_hit_rate,
        stats.percpu_remap_lookups,
        
        stats.optimization_flags,
        stats.remap_entries,
        stats.max_entries,
        
        stats.avg_latency_ns,
        total_mb,
        stats.fast_path_hit_rate);
}

/* Phase 3.2B: Sysfs attribute definitions */
static struct kobj_attribute opt_fast_path_hits_attr = 
    __ATTR(fast_path_hits, 0444, opt_fast_path_hits_show, NULL);
static struct kobj_attribute opt_slow_path_hits_attr = 
    __ATTR(slow_path_hits, 0444, opt_slow_path_hits_show, NULL);
static struct kobj_attribute opt_total_lookups_attr = 
    __ATTR(total_lookups, 0444, opt_total_lookups_show, NULL);
static struct kobj_attribute opt_fast_path_hit_rate_attr = 
    __ATTR(fast_path_hit_rate, 0444, opt_fast_path_hit_rate_show, NULL);

static struct kobj_attribute opt_percpu_total_ios_attr = 
    __ATTR(percpu_total_ios, 0444, opt_percpu_total_ios_show, NULL);
static struct kobj_attribute opt_percpu_avg_latency_ns_attr = 
    __ATTR(percpu_avg_latency_ns, 0444, opt_percpu_avg_latency_ns_show, NULL);
static struct kobj_attribute opt_percpu_total_bytes_attr = 
    __ATTR(percpu_total_bytes, 0444, opt_percpu_total_bytes_show, NULL);
static struct kobj_attribute opt_percpu_cache_hits_attr = 
    __ATTR(percpu_cache_hits, 0444, opt_percpu_cache_hits_show, NULL);
static struct kobj_attribute opt_percpu_cache_misses_attr = 
    __ATTR(percpu_cache_misses, 0444, opt_percpu_cache_misses_show, NULL);
static struct kobj_attribute opt_percpu_remap_lookups_attr = 
    __ATTR(percpu_remap_lookups, 0444, opt_percpu_remap_lookups_show, NULL);

static struct kobj_attribute opt_remap_entries_attr = 
    __ATTR(remap_entries, 0444, opt_remap_entries_show, NULL);

static struct kobj_attribute opt_flags_attr = 
    __ATTR(optimization_flags, 0644, opt_flags_show, opt_flags_store);
static struct kobj_attribute opt_optimize_layout_attr = 
    __ATTR(optimize_layout, 0200, NULL, opt_optimize_layout_store);
static struct kobj_attribute opt_comprehensive_stats_attr = 
    __ATTR(comprehensive_stats, 0444, opt_comprehensive_stats_show, NULL);

/* Phase 3.2B: Attribute group */
static struct attribute *dmr_opt_attrs[] = {
    /* Fast/slow path statistics */
    &opt_fast_path_hits_attr.attr,
    &opt_slow_path_hits_attr.attr,
    &opt_total_lookups_attr.attr,
    &opt_fast_path_hit_rate_attr.attr,
    
    /* Per-CPU statistics */
    &opt_percpu_total_ios_attr.attr,
    &opt_percpu_avg_latency_ns_attr.attr,
    &opt_percpu_total_bytes_attr.attr,
    &opt_percpu_cache_hits_attr.attr,
    &opt_percpu_cache_misses_attr.attr,
    &opt_percpu_remap_lookups_attr.attr,
    
    /* Configuration */
    &opt_remap_entries_attr.attr,
    &opt_flags_attr.attr,
    &opt_optimize_layout_attr.attr,
    
    /* Comprehensive view */
    &opt_comprehensive_stats_attr.attr,
    
    NULL,
};

static struct attribute_group dmr_opt_attr_group = {
    .attrs = dmr_opt_attrs,
};

/**
 * dmr_optimization_sysfs_init - Initialize Phase 3.2B optimization sysfs interface
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_optimization_sysfs_init(void)
{
    int ret;
    
    /* Create optimization kobject under /sys/kernel/ */
    dmr_opt_kobj = kobject_create_and_add("dm_remap_optimization", kernel_kobj);
    if (!dmr_opt_kobj) {
        DMR_DEBUG(0, "Failed to create Phase 3.2B optimization sysfs kobject");
        return -ENOMEM;
    }
    
    /* Create attribute group */
    ret = sysfs_create_group(dmr_opt_kobj, &dmr_opt_attr_group);
    if (ret) {
        DMR_DEBUG(0, "Failed to create Phase 3.2B optimization sysfs attributes: %d", ret);
        kobject_put(dmr_opt_kobj);
        dmr_opt_kobj = NULL;
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2B optimization sysfs interface initialized at /sys/kernel/dm_remap_optimization/");
    
    return 0;
}

/**
 * dmr_optimization_sysfs_cleanup - Cleanup Phase 3.2B optimization sysfs interface
 */
void dmr_optimization_sysfs_cleanup(void)
{
    if (dmr_opt_kobj) {
        sysfs_remove_group(dmr_opt_kobj, &dmr_opt_attr_group);
        kobject_put(dmr_opt_kobj);
        dmr_opt_kobj = NULL;
    }
    
    DMR_DEBUG(1, "Phase 3.2B optimization sysfs interface cleaned up");
}