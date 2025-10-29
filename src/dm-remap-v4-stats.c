/*
 * dm-remap v4.0 - Simple Statistics Export via sysfs
 * 
 * Provides a clean sysfs interface for monitoring tools (Prometheus,
 * Nagios, Grafana, etc.) to consume dm-remap statistics without
 * complex parsing of dmsetup status output.
 * 
 * Design Philosophy:
 * - Simple, not fancy
 * - Expose what we already track
 * - Let existing monitoring tools do the analysis
 * - No "AI/ML" theater - just facts
 * 
 * Copyright (C) 2025 dm-remap Development Team
 */

#define DM_MSG_PREFIX "dm-remap-v4-stats"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/atomic.h>

MODULE_DESCRIPTION("dm-remap v4.0 Statistics Export");
MODULE_AUTHOR("dm-remap Development Team");
MODULE_LICENSE("GPL");
MODULE_VERSION("4.0.1");

/*
 * Statistics structure - matches what dm-remap-v4-real.c tracks
 */
struct dm_remap_stats {
    /* Basic I/O counters */
    atomic64_t total_reads;
    atomic64_t total_writes;
    atomic64_t total_remaps;
    atomic64_t total_errors;
    
    /* Remap activity */
    atomic_t active_mappings;
    atomic64_t last_remap_time;      /* Unix timestamp */
    atomic64_t last_error_time;      /* Unix timestamp */
    
    /* Performance metrics */
    atomic64_t avg_latency_us;       /* Microseconds */
    atomic64_t remapped_sectors;
    atomic64_t spare_sectors_used;
    
    /* Health indicators */
    atomic_t remap_rate_per_hour;    /* Recent remap activity */
    atomic_t error_rate_per_hour;    /* Recent error activity */
    atomic_t health_score;           /* 0-100 */
};

static struct dm_remap_stats global_stats;
static struct kobject *dm_remap_kobj;

/*
 * Sysfs attribute show functions
 */

static ssize_t total_reads_show(struct kobject *kobj,
                               struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.total_reads));
}

static ssize_t total_writes_show(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.total_writes));
}

static ssize_t total_remaps_show(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.total_remaps));
}

static ssize_t total_errors_show(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.total_errors));
}

static ssize_t active_mappings_show(struct kobject *kobj,
                                   struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", atomic_read(&global_stats.active_mappings));
}

static ssize_t last_remap_time_show(struct kobject *kobj,
                                   struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.last_remap_time));
}

static ssize_t last_error_time_show(struct kobject *kobj,
                                   struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.last_error_time));
}

static ssize_t avg_latency_us_show(struct kobject *kobj,
                                  struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.avg_latency_us));
}

static ssize_t remapped_sectors_show(struct kobject *kobj,
                                    struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.remapped_sectors));
}

static ssize_t spare_sectors_used_show(struct kobject *kobj,
                                      struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%llu\n", atomic64_read(&global_stats.spare_sectors_used));
}

static ssize_t remap_rate_per_hour_show(struct kobject *kobj,
                                       struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", atomic_read(&global_stats.remap_rate_per_hour));
}

static ssize_t error_rate_per_hour_show(struct kobject *kobj,
                                       struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", atomic_read(&global_stats.error_rate_per_hour));
}

static ssize_t health_score_show(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", atomic_read(&global_stats.health_score));
}

/*
 * Convenience: All stats in one file (Prometheus-style)
 */
static ssize_t all_stats_show(struct kobject *kobj,
                             struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,
        "# dm-remap v4.0 statistics\n"
        "dm_remap_total_reads %llu\n"
        "dm_remap_total_writes %llu\n"
        "dm_remap_total_remaps %llu\n"
        "dm_remap_total_errors %llu\n"
        "dm_remap_active_mappings %u\n"
        "dm_remap_last_remap_time %llu\n"
        "dm_remap_last_error_time %llu\n"
        "dm_remap_avg_latency_us %llu\n"
        "dm_remap_remapped_sectors %llu\n"
        "dm_remap_spare_sectors_used %llu\n"
        "dm_remap_remap_rate_per_hour %u\n"
        "dm_remap_error_rate_per_hour %u\n"
        "dm_remap_health_score %u\n",
        atomic64_read(&global_stats.total_reads),
        atomic64_read(&global_stats.total_writes),
        atomic64_read(&global_stats.total_remaps),
        atomic64_read(&global_stats.total_errors),
        atomic_read(&global_stats.active_mappings),
        atomic64_read(&global_stats.last_remap_time),
        atomic64_read(&global_stats.last_error_time),
        atomic64_read(&global_stats.avg_latency_us),
        atomic64_read(&global_stats.remapped_sectors),
        atomic64_read(&global_stats.spare_sectors_used),
        atomic_read(&global_stats.remap_rate_per_hour),
        atomic_read(&global_stats.error_rate_per_hour),
        atomic_read(&global_stats.health_score));
}

/*
 * Define sysfs attributes
 */
static struct kobj_attribute total_reads_attr = __ATTR_RO(total_reads);
static struct kobj_attribute total_writes_attr = __ATTR_RO(total_writes);
static struct kobj_attribute total_remaps_attr = __ATTR_RO(total_remaps);
static struct kobj_attribute total_errors_attr = __ATTR_RO(total_errors);
static struct kobj_attribute active_mappings_attr = __ATTR_RO(active_mappings);
static struct kobj_attribute last_remap_time_attr = __ATTR_RO(last_remap_time);
static struct kobj_attribute last_error_time_attr = __ATTR_RO(last_error_time);
static struct kobj_attribute avg_latency_us_attr = __ATTR_RO(avg_latency_us);
static struct kobj_attribute remapped_sectors_attr = __ATTR_RO(remapped_sectors);
static struct kobj_attribute spare_sectors_used_attr = __ATTR_RO(spare_sectors_used);
static struct kobj_attribute remap_rate_per_hour_attr = __ATTR_RO(remap_rate_per_hour);
static struct kobj_attribute error_rate_per_hour_attr = __ATTR_RO(error_rate_per_hour);
static struct kobj_attribute health_score_attr = __ATTR_RO(health_score);
static struct kobj_attribute all_stats_attr = __ATTR_RO(all_stats);

/*
 * Attribute groups for sysfs organization
 */
static struct attribute *dm_remap_attrs[] = {
    &total_reads_attr.attr,
    &total_writes_attr.attr,
    &total_remaps_attr.attr,
    &total_errors_attr.attr,
    &active_mappings_attr.attr,
    &last_remap_time_attr.attr,
    &last_error_time_attr.attr,
    &avg_latency_us_attr.attr,
    &remapped_sectors_attr.attr,
    &spare_sectors_used_attr.attr,
    &remap_rate_per_hour_attr.attr,
    &error_rate_per_hour_attr.attr,
    &health_score_attr.attr,
    &all_stats_attr.attr,
    NULL,
};

static struct attribute_group dm_remap_attr_group = {
    .attrs = dm_remap_attrs,
};

/*
 * Public API for dm-remap-v4-real.c to update stats
 */

void dm_remap_stats_inc_reads(void)
{
    atomic64_inc(&global_stats.total_reads);
}
EXPORT_SYMBOL(dm_remap_stats_inc_reads);

void dm_remap_stats_inc_writes(void)
{
    atomic64_inc(&global_stats.total_writes);
}
EXPORT_SYMBOL(dm_remap_stats_inc_writes);

void dm_remap_stats_inc_remaps(void)
{
    atomic64_inc(&global_stats.total_remaps);
    atomic64_set(&global_stats.last_remap_time, ktime_get_real_seconds());
}
EXPORT_SYMBOL(dm_remap_stats_inc_remaps);

void dm_remap_stats_inc_errors(void)
{
    atomic64_inc(&global_stats.total_errors);
    atomic64_set(&global_stats.last_error_time, ktime_get_real_seconds());
}
EXPORT_SYMBOL(dm_remap_stats_inc_errors);

void dm_remap_stats_set_active_mappings(unsigned int count)
{
    atomic_set(&global_stats.active_mappings, count);
}
EXPORT_SYMBOL(dm_remap_stats_set_active_mappings);

void dm_remap_stats_update_latency(u64 latency_ns)
{
    /* Simple moving average approximation */
    u64 current_avg = atomic64_read(&global_stats.avg_latency_us);
    u64 new_latency_us = latency_ns / 1000;
    u64 new_avg = (current_avg * 7 + new_latency_us) / 8; /* 87.5% weight to history */
    atomic64_set(&global_stats.avg_latency_us, new_avg);
}
EXPORT_SYMBOL(dm_remap_stats_update_latency);

void dm_remap_stats_update_health_score(unsigned int score)
{
    if (score <= 100)
        atomic_set(&global_stats.health_score, score);
}
EXPORT_SYMBOL(dm_remap_stats_update_health_score);

/*
 * Module initialization and cleanup
 */

static int __init dm_remap_stats_init(void)
{
    int ret;
    
    /* Initialize stats */
    atomic64_set(&global_stats.total_reads, 0);
    atomic64_set(&global_stats.total_writes, 0);
    atomic64_set(&global_stats.total_remaps, 0);
    atomic64_set(&global_stats.total_errors, 0);
    atomic_set(&global_stats.active_mappings, 0);
    atomic64_set(&global_stats.last_remap_time, 0);
    atomic64_set(&global_stats.last_error_time, 0);
    atomic64_set(&global_stats.avg_latency_us, 0);
    atomic64_set(&global_stats.remapped_sectors, 0);
    atomic64_set(&global_stats.spare_sectors_used, 0);
    atomic_set(&global_stats.remap_rate_per_hour, 0);
    atomic_set(&global_stats.error_rate_per_hour, 0);
    atomic_set(&global_stats.health_score, 100);
    
    /* Create /sys/kernel/dm_remap/ */
    dm_remap_kobj = kobject_create_and_add("dm_remap", kernel_kobj);
    if (!dm_remap_kobj) {
        pr_err("dm-remap-stats: Failed to create kobject\n");
        return -ENOMEM;
    }
    
    /* Create sysfs attributes */
    ret = sysfs_create_group(dm_remap_kobj, &dm_remap_attr_group);
    if (ret) {
        pr_err("dm-remap-stats: Failed to create sysfs group\n");
        kobject_put(dm_remap_kobj);
        return ret;
    }
    
    pr_info("dm-remap-stats: Statistics export initialized\n");
    pr_info("dm-remap-stats: Available at /sys/kernel/dm_remap/\n");
    pr_info("dm-remap-stats: Prometheus-style: cat /sys/kernel/dm_remap/all_stats\n");
    
    return 0;
}

static void __exit dm_remap_stats_exit(void)
{
    sysfs_remove_group(dm_remap_kobj, &dm_remap_attr_group);
    kobject_put(dm_remap_kobj);
    pr_info("dm-remap-stats: Statistics export removed\n");
}

/* Module initialization disabled for integrated builds
 * When dm-remap-v4-stats.c is linked into dm-remap-v4-real,
 * the main module's init/exit functions handle initialization.
 * For standalone builds, uncomment these lines and rebuild.
 *
 * module_init(dm_remap_stats_init);
 * module_exit(dm_remap_stats_exit);
 */
