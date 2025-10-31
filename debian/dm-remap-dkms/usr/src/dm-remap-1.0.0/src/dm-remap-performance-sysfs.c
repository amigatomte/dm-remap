/*
 * dm-remap-performance-sysfs.c - Performance Profiler Sysfs Interface
 * 
 * This module provides sysfs interface for accessing performance
 * profiling data from dm-remap v4.0.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include "dm-remap-core.h"
#include "dm-remap-performance-profiler.h"

#define DMR_PERF_SYSFS_BUFFER_SIZE    (16 * 1024)  /* 16KB buffer for stats */

/* Global context for sysfs access */
static struct remap_c *global_perf_context = NULL;

/*
 * Show Performance Statistics
 */
static ssize_t dmr_perf_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    char *stats_buffer;
    int written = 0;
    int ret;
    
    if (!rc->perf_profiler)
        return sprintf(buf, "Performance profiler not available\n");
    
    stats_buffer = vmalloc(DMR_PERF_SYSFS_BUFFER_SIZE);
    if (!stats_buffer)
        return sprintf(buf, "Memory allocation failed\n");
    
    ret = dmr_perf_export_stats(rc->perf_profiler, stats_buffer, DMR_PERF_SYSFS_BUFFER_SIZE);
    if (ret > 0) {
        /* Copy as much as fits in the sysfs buffer */
        written = min(ret, (int)(PAGE_SIZE - 1));
        memcpy(buf, stats_buffer, written);
        buf[written] = '\0';
    } else {
        written = sprintf(buf, "Error exporting statistics\n");
    }
    
    vfree(stats_buffer);
    return written;
}

/*
 * Reset Performance Statistics
 */
static ssize_t dmr_perf_reset_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    
    if (!rc->perf_profiler)
        return -ENODEV;
    
    if (strncmp(buf, "reset", 5) == 0) {
        dmr_perf_reset_stats(rc->perf_profiler);
        return count;
    }
    
    return -EINVAL;
}

/*
 * Show Profiler Status
 */
static ssize_t dmr_perf_status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    
    if (!rc->perf_profiler) {
        return sprintf(buf, "disabled\n");
    }
    
    return sprintf(buf, "enabled (detailed: %s, samples: %lld)\n",
                   rc->perf_profiler->detailed_profiling ? "yes" : "no",
                   atomic64_read(&rc->perf_profiler->total_samples));
}

/*
 * Enable/Disable Detailed Profiling
 */
static ssize_t dmr_perf_detailed_store(struct kobject *kobj, struct kobj_attribute *attr,
                                      const char *buf, size_t count)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    
    if (!rc->perf_profiler)
        return -ENODEV;
    
    if (strncmp(buf, "1", 1) == 0 || strncmp(buf, "enable", 6) == 0) {
        rc->perf_profiler->detailed_profiling = true;
        printk(KERN_INFO "dm-remap: detailed profiling enabled\n");
    } else if (strncmp(buf, "0", 1) == 0 || strncmp(buf, "disable", 7) == 0) {
        rc->perf_profiler->detailed_profiling = false;
        printk(KERN_INFO "dm-remap: detailed profiling disabled\n");
    } else {
        return -EINVAL;
    }
    
    return count;
}

/*
 * Show Hot Path Statistics
 */
static ssize_t dmr_perf_hotpath_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    
    if (!rc->perf_profiler || !rc->perf_profiler->hotpath)
        return sprintf(buf, "Hot path profiler not available\n");
    
    return sprintf(buf, "Fast Path: %lld\nSlow Path: %lld\nRemap Path: %lld\n",
                   atomic64_read(&rc->perf_profiler->hotpath->fast_path_count),
                   atomic64_read(&rc->perf_profiler->hotpath->slow_path_count),
                   atomic64_read(&rc->perf_profiler->hotpath->remap_path_count));
}

/*
 * Show Memory Statistics
 */
static ssize_t dmr_perf_memory_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    int written = 0;
    int i;
    
    if (!rc->perf_profiler || !rc->perf_profiler->memory)
        return sprintf(buf, "Memory profiler not available\n");
    
    written += sprintf(buf + written, "=== Memory Pool Statistics ===\n");
    for (i = 0; i < 3; i++) {
        written += sprintf(buf + written, "Pool %d - Hits: %lld, Misses: %lld\n", i,
                          atomic64_read(&rc->perf_profiler->memory->pool_hits[i]),
                          atomic64_read(&rc->perf_profiler->memory->pool_misses[i]));
    }
    
    written += sprintf(buf + written, "\n=== Memory Usage ===\n");
    written += sprintf(buf + written, "Current: %lld bytes\n", 
                      atomic64_read(&rc->perf_profiler->memory->current_memory_usage));
    written += sprintf(buf + written, "Peak: %lld bytes\n", 
                      atomic64_read(&rc->perf_profiler->memory->peak_memory_usage));
    written += sprintf(buf + written, "Allocations: %lld\n", 
                      atomic64_read(&rc->perf_profiler->memory->total_allocations));
    written += sprintf(buf + written, "Frees: %lld\n", 
                      atomic64_read(&rc->perf_profiler->memory->total_frees));
    
    return written;
}

/*
 * Show Lock Statistics
 */
static ssize_t dmr_perf_locks_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc = container_of(kobj, struct remap_c, kobj);
    
    if (!rc->perf_profiler || !rc->perf_profiler->locks)
        return sprintf(buf, "Lock profiler not available\n");
    
    return sprintf(buf, "Acquisitions: %lld\nContentions: %lld\nMax Hold Time: %lld ns\n",
                   atomic64_read(&rc->perf_profiler->locks->lock_acquisitions),
                   atomic64_read(&rc->perf_profiler->locks->lock_contentions),
                   atomic64_read(&rc->perf_profiler->locks->max_hold_time));
}

/*
 * Sysfs Attributes
 */
static struct kobj_attribute dmr_perf_stats_attr = 
    __ATTR(performance_stats, 0444, dmr_perf_stats_show, NULL);

static struct kobj_attribute dmr_perf_reset_attr = 
    __ATTR(performance_reset, 0200, NULL, dmr_perf_reset_store);

static struct kobj_attribute dmr_perf_status_attr = 
    __ATTR(performance_status, 0444, dmr_perf_status_show, NULL);

static struct kobj_attribute dmr_perf_detailed_attr = 
    __ATTR(performance_detailed, 0644, dmr_perf_status_show, dmr_perf_detailed_store);

static struct kobj_attribute dmr_perf_hotpath_attr = 
    __ATTR(performance_hotpath, 0444, dmr_perf_hotpath_show, NULL);

static struct kobj_attribute dmr_perf_memory_attr = 
    __ATTR(performance_memory, 0444, dmr_perf_memory_show, NULL);

static struct kobj_attribute dmr_perf_locks_attr = 
    __ATTR(performance_locks, 0444, dmr_perf_locks_show, NULL);

/*
 * Attribute Group
 */
static struct attribute *dmr_perf_attrs[] = {
    &dmr_perf_stats_attr.attr,
    &dmr_perf_reset_attr.attr,
    &dmr_perf_status_attr.attr,
    &dmr_perf_detailed_attr.attr,
    &dmr_perf_hotpath_attr.attr,
    &dmr_perf_memory_attr.attr,
    &dmr_perf_locks_attr.attr,
    NULL,
};

static struct attribute_group dmr_perf_attr_group = {
    .name = "performance",
    .attrs = dmr_perf_attrs,
};

/*
 * Create Performance Sysfs Interface
 */
int dmr_perf_sysfs_create(struct remap_c *rc)
{
    if (!rc->perf_profiler)
        return 0;  /* No profiler, no sysfs interface */
    
    /* Store reference for attribute access */
    global_perf_context = rc;
    
    printk(KERN_INFO "dm-remap: performance profiler ready (use module parameters for basic stats)\n");
    return 0;
}

/*
 * Remove Performance Sysfs Interface
 */
void dmr_perf_sysfs_remove(struct remap_c *rc)
{
    if (!rc->perf_profiler)
        return;
    
    /* Clear global reference */
    global_perf_context = NULL;
    printk(KERN_INFO "dm-remap: performance profiler context removed\n");
}