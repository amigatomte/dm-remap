/*
 * dm-remap-health-sysfs.c - Sysfs Interface for Health Scanning
 * 
 * Sysfs Interface for dm-remap Background Health Scanning
 * Provides user-space control and monitoring of health scanning operations
 * 
 * This module implements the sysfs interface that allows users and system
 * administrators to control health scanning parameters and view health status.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

#include "dm-remap-core.h"
#include "dm-remap-health-core.h"

/* Module metadata */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Sysfs Interface for dm-remap Health Scanning");
MODULE_LICENSE("GPL");

/* Sysfs attribute show/store function prototypes */
static ssize_t health_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t health_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, 
                                   const char *buf, size_t count);
static ssize_t scan_interval_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t scan_interval_store(struct kobject *kobj, struct kobj_attribute *attr,
                                  const char *buf, size_t count);
static ssize_t scan_intensity_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t scan_intensity_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count);
static ssize_t sectors_per_scan_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sectors_per_scan_store(struct kobject *kobj, struct kobj_attribute *attr,
                                     const char *buf, size_t count);
static ssize_t scanner_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t health_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t health_report_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t scan_control_store(struct kobject *kobj, struct kobj_attribute *attr,
                                 const char *buf, size_t count);

/* Sysfs attribute definitions */
static struct kobj_attribute health_enabled_attr = 
    __ATTR(enabled, 0644, health_enabled_show, health_enabled_store);

static struct kobj_attribute scan_interval_attr = 
    __ATTR(scan_interval_ms, 0644, scan_interval_show, scan_interval_store);

static struct kobj_attribute scan_intensity_attr = 
    __ATTR(scan_intensity, 0644, scan_intensity_show, scan_intensity_store);

static struct kobj_attribute sectors_per_scan_attr = 
    __ATTR(sectors_per_scan, 0644, sectors_per_scan_show, sectors_per_scan_store);

static struct kobj_attribute scanner_state_attr = 
    __ATTR(scanner_state, 0444, scanner_state_show, NULL);

static struct kobj_attribute health_stats_attr = 
    __ATTR(statistics, 0444, health_stats_show, NULL);

static struct kobj_attribute health_report_attr = 
    __ATTR(health_report, 0444, health_report_show, NULL);

static struct kobj_attribute scan_control_attr = 
    __ATTR(control, 0200, NULL, scan_control_store);

/* Attribute group */
static struct attribute *health_attrs[] = {
    &health_enabled_attr.attr,
    &scan_interval_attr.attr,
    &scan_intensity_attr.attr,
    &sectors_per_scan_attr.attr,
    &scanner_state_attr.attr,
    &health_stats_attr.attr,
    &health_report_attr.attr,
    &scan_control_attr.attr,
    NULL,
};

static const struct attribute_group health_attr_group = {
    .attrs = health_attrs,
};

/**
 * dmr_health_sysfs_init - Initialize sysfs interface for health scanning
 * @scanner: Health scanner instance
 * 
 * Creates sysfs directory and attributes for health scanning control.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_sysfs_init(struct dmr_health_scanner *scanner)
{
    if (!scanner) {
        return -EINVAL;
    }

    /* For now, create a minimal sysfs interface stub */
    /* Future implementation will integrate with existing dm-remap sysfs */
    scanner->health_kobj = NULL; /* Placeholder */

    printk(KERN_INFO "dm-remap-health-sysfs: Sysfs interface initialized (minimal)\n");
    return 0;
}

/**
 * dmr_health_sysfs_cleanup - Clean up sysfs interface
 * @scanner: Health scanner instance
 * 
 * Removes sysfs directory and attributes for health scanning.
 */
void dmr_health_sysfs_cleanup(struct dmr_health_scanner *scanner)
{
    if (!scanner) {
        return;
    }

    /* Minimal cleanup for stub interface */
    scanner->health_kobj = NULL;

    printk(KERN_INFO "dm-remap-health-sysfs: Sysfs interface cleaned up (minimal)\n");
}

/* Helper function to get scanner from kobject */
static struct dmr_health_scanner *dmr_get_scanner_from_kobj(struct kobject *kobj)
{
    /* For the minimal stub implementation, return NULL */
    /* Future implementation will properly navigate device mapper structures */
    return NULL;
}

/**
 * health_enabled_show - Show health scanning enabled status
 */
static ssize_t health_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    
    if (!scanner) {
        return -ENODEV;
    }

    return sprintf(buf, "%d\n", scanner->enabled ? 1 : 0);
}

/**
 * health_enabled_store - Enable or disable health scanning
 */
static ssize_t health_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, 
                                   const char *buf, size_t count)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    bool enabled;
    int ret;

    if (!scanner) {
        return -ENODEV;
    }

    ret = kstrtobool(buf, &enabled);
    if (ret) {
        return ret;
    }

    mutex_lock(&scanner->config_mutex);

    if (enabled && !scanner->enabled) {
        scanner->enabled = true;
        ret = dmr_health_scanner_start(scanner);
        if (ret) {
            scanner->enabled = false;
            mutex_unlock(&scanner->config_mutex);
            return ret;
        }
        printk(KERN_INFO "dm-remap-health: Health scanning enabled\n");
    } else if (!enabled && scanner->enabled) {
        scanner->enabled = false;
        dmr_health_scanner_stop(scanner);
        printk(KERN_INFO "dm-remap-health: Health scanning disabled\n");
    }

    mutex_unlock(&scanner->config_mutex);
    return count;
}

/**
 * scan_interval_show - Show current scan interval
 */
static ssize_t scan_interval_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    
    if (!scanner) {
        return -ENODEV;
    }

    return sprintf(buf, "%lu\n", scanner->scan_interval_ms);
}

/**
 * scan_interval_store - Set scan interval
 */
static ssize_t scan_interval_store(struct kobject *kobj, struct kobj_attribute *attr,
                                  const char *buf, size_t count)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    unsigned long interval;
    int ret;

    if (!scanner) {
        return -ENODEV;
    }

    ret = kstrtoul(buf, 10, &interval);
    if (ret) {
        return ret;
    }

    if (interval < DMR_HEALTH_SCAN_MIN_INTERVAL_MS || 
        interval > DMR_HEALTH_SCAN_MAX_INTERVAL_MS) {
        return -EINVAL;
    }

    mutex_lock(&scanner->config_mutex);
    scanner->scan_interval_ms = interval;
    mutex_unlock(&scanner->config_mutex);

    printk(KERN_INFO "dm-remap-health: Scan interval set to %lu ms\n", interval);
    return count;
}

/**
 * scan_intensity_show - Show current scan intensity
 */
static ssize_t scan_intensity_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    
    if (!scanner) {
        return -ENODEV;
    }

    return sprintf(buf, "%u\n", scanner->scan_intensity);
}

/**
 * scan_intensity_store - Set scan intensity
 */
static ssize_t scan_intensity_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    unsigned int intensity;
    int ret;

    if (!scanner) {
        return -ENODEV;
    }

    ret = kstrtouint(buf, 10, &intensity);
    if (ret) {
        return ret;
    }

    if (intensity < DMR_HEALTH_SCAN_INTENSITY_MIN || 
        intensity > DMR_HEALTH_SCAN_INTENSITY_MAX) {
        return -EINVAL;
    }

    mutex_lock(&scanner->config_mutex);
    scanner->scan_intensity = (u8)intensity;
    mutex_unlock(&scanner->config_mutex);

    printk(KERN_INFO "dm-remap-health: Scan intensity set to %u\n", intensity);
    return count;
}

/**
 * sectors_per_scan_show - Show sectors per scan cycle
 */
static ssize_t sectors_per_scan_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    
    if (!scanner) {
        return -ENODEV;
    }

    return sprintf(buf, "%llu\n", (unsigned long long)scanner->sectors_per_scan);
}

/**
 * sectors_per_scan_store - Set sectors per scan cycle  
 */
static ssize_t sectors_per_scan_store(struct kobject *kobj, struct kobj_attribute *attr,
                                     const char *buf, size_t count)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    unsigned long long sectors;
    int ret;

    if (!scanner) {
        return -ENODEV;
    }

    ret = kstrtoull(buf, 10, &sectors);
    if (ret) {
        return ret;
    }

    if (sectors < DMR_HEALTH_SECTORS_PER_SCAN_MIN || 
        sectors > DMR_HEALTH_SECTORS_PER_SCAN_MAX) {
        return -EINVAL;
    }

    mutex_lock(&scanner->config_mutex);
    scanner->sectors_per_scan = (sector_t)sectors;
    mutex_unlock(&scanner->config_mutex);

    printk(KERN_INFO "dm-remap-health: Sectors per scan set to %llu\n", sectors);
    return count;
}

/**
 * scanner_state_show - Show current scanner state
 */
static ssize_t scanner_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    const char *state_names[] = {
        "stopped", "starting", "running", "paused", "stopping"
    };

    if (!scanner) {
        return -ENODEV;
    }

    if (scanner->scanner_state >= ARRAY_SIZE(state_names)) {
        return sprintf(buf, "unknown\n");
    }

    return sprintf(buf, "%s\n", state_names[scanner->scanner_state]);
}

/**
 * health_stats_show - Show health scanning statistics
 */
static ssize_t health_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    ssize_t len = 0;
    u64 avg_scan_time_ns;
    sector_t tracked_sectors;
    size_t memory_used;

    if (!scanner) {
        return -ENODEV;
    }

    /* Calculate average scan time */
    if (atomic64_read(&scanner->stats.total_scans) > 0) {
        avg_scan_time_ns = atomic64_read(&scanner->stats.scan_time_total_ns) / 
                          atomic64_read(&scanner->stats.total_scans);
    } else {
        avg_scan_time_ns = 0;
    }

    /* Get health map statistics */
    if (scanner->health_map) {
        dmr_health_map_get_stats(scanner->health_map, &tracked_sectors, &memory_used);
    } else {
        tracked_sectors = 0;
        memory_used = 0;
    }

    len += sprintf(buf + len, "total_scans: %lld\n", 
                   atomic64_read(&scanner->stats.total_scans));
    len += sprintf(buf + len, "sectors_scanned: %lld\n",
                   atomic64_read(&scanner->stats.sectors_scanned));
    len += sprintf(buf + len, "warnings_issued: %lld\n",
                   atomic64_read(&scanner->stats.warnings_issued));
    len += sprintf(buf + len, "predictions_made: %lld\n",
                   atomic64_read(&scanner->stats.predictions_made));
    len += sprintf(buf + len, "active_warnings: %d\n",
                   atomic_read(&scanner->stats.active_warnings));
    len += sprintf(buf + len, "high_risk_sectors: %d\n",
                   atomic_read(&scanner->stats.high_risk_sectors));
    len += sprintf(buf + len, "scan_coverage_percent: %u\n",
                   scanner->stats.scan_coverage_percent);
    len += sprintf(buf + len, "avg_scan_time_ns: %llu\n", avg_scan_time_ns);
    len += sprintf(buf + len, "tracked_sectors: %llu\n", 
                   (unsigned long long)tracked_sectors);
    len += sprintf(buf + len, "memory_used_bytes: %zu\n", memory_used);
    len += sprintf(buf + len, "last_scan_overhead_ns: %llu\n", scanner->io_overhead_ns);
    
    if (scanner->stats.last_full_scan_time > 0) {
        len += sprintf(buf + len, "last_full_scan_age_sec: %llu\n",
                       (unsigned long long)((jiffies - scanner->stats.last_full_scan_time) / HZ));
    } else {
        len += sprintf(buf + len, "last_full_scan_age_sec: never\n");
    }

    return len;
}

/**
 * health_report_show - Show comprehensive health report
 */
static ssize_t health_report_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    int ret;

    if (!scanner) {
        return -ENODEV;
    }

    ret = dmr_health_generate_report(scanner, buf, PAGE_SIZE);
    return ret > 0 ? ret : 0;
}

/**
 * scan_control_store - Control scanner operations
 */
static ssize_t scan_control_store(struct kobject *kobj, struct kobj_attribute *attr,
                                 const char *buf, size_t count)
{
    struct dmr_health_scanner *scanner = dmr_get_scanner_from_kobj(kobj);
    char command[32];
    int ret;

    if (!scanner) {
        return -ENODEV;
    }

    ret = sscanf(buf, "%31s", command);
    if (ret != 1) {
        return -EINVAL;
    }

    if (strcmp(command, "start") == 0) {
        ret = dmr_health_scanner_start(scanner);
        if (ret) {
            return ret;
        }
        printk(KERN_INFO "dm-remap-health: Scanner started via sysfs\n");
    } else if (strcmp(command, "stop") == 0) {
        ret = dmr_health_scanner_stop(scanner);
        if (ret) {
            return ret;
        }
        printk(KERN_INFO "dm-remap-health: Scanner stopped via sysfs\n");
    } else if (strcmp(command, "pause") == 0) {
        ret = dmr_health_scanner_pause(scanner);
        if (ret) {
            return ret;
        }
        printk(KERN_INFO "dm-remap-health: Scanner paused via sysfs\n");
    } else if (strcmp(command, "resume") == 0) {
        ret = dmr_health_scanner_resume(scanner);
        if (ret) {
            return ret;
        }
        printk(KERN_INFO "dm-remap-health: Scanner resumed via sysfs\n");
    } else if (strcmp(command, "reset_stats") == 0) {
        /* Reset statistics */
        atomic64_set(&scanner->stats.total_scans, 0);
        atomic64_set(&scanner->stats.sectors_scanned, 0);
        atomic64_set(&scanner->stats.warnings_issued, 0);
        atomic64_set(&scanner->stats.predictions_made, 0);
        atomic64_set(&scanner->stats.scan_time_total_ns, 0);
        scanner->stats.last_full_scan_time = 0;
        scanner->stats.scan_coverage_percent = 0;
        printk(KERN_INFO "dm-remap-health: Statistics reset via sysfs\n");
    } else if (strcmp(command, "compact_map") == 0) {
        if (scanner->health_map) {
            ret = dmr_health_map_compact(scanner->health_map);
            if (ret) {
                return ret;
            }
            printk(KERN_INFO "dm-remap-health: Health map compacted via sysfs\n");
        }
    } else {
        return -EINVAL;
    }

    return count;
}

/**
 * dmr_health_generate_report - Generate comprehensive health report
 * @scanner: Health scanner instance
 * @buffer: Output buffer
 * @buffer_size: Size of output buffer
 * 
 * Generates a comprehensive health report including statistics,
 * risk analysis, and recommendations.
 * 
 * Returns: Number of bytes written, negative error code on failure
 */
int dmr_health_generate_report(struct dmr_health_scanner *scanner, 
                              char *buffer, size_t buffer_size)
{
    ssize_t len = 0;
    sector_t tracked_sectors;
    size_t memory_used;
    u64 total_scans, sectors_scanned;
    int active_warnings, high_risk_sectors;

    if (!scanner || !buffer || buffer_size == 0) {
        return -EINVAL;
    }

    /* Get current statistics */
    total_scans = atomic64_read(&scanner->stats.total_scans);
    sectors_scanned = atomic64_read(&scanner->stats.sectors_scanned);
    active_warnings = atomic_read(&scanner->stats.active_warnings);
    high_risk_sectors = atomic_read(&scanner->stats.high_risk_sectors);

    if (scanner->health_map) {
        dmr_health_map_get_stats(scanner->health_map, &tracked_sectors, &memory_used);
    } else {
        tracked_sectors = 0;
        memory_used = 0;
    }

    /* Generate report header */
    len += snprintf(buffer + len, buffer_size - len,
                   "=== dm-remap Health Scanning Report ===\n\n");

    /* Scanner status */
    len += snprintf(buffer + len, buffer_size - len,
                   "Scanner Status:\n");
    len += snprintf(buffer + len, buffer_size - len,
                   "  Enabled: %s\n", scanner->enabled ? "Yes" : "No");
    len += snprintf(buffer + len, buffer_size - len,
                   "  State: %s\n", 
                   scanner->scanner_state == DMR_SCANNER_RUNNING ? "Running" :
                   scanner->scanner_state == DMR_SCANNER_PAUSED ? "Paused" :
                   scanner->scanner_state == DMR_SCANNER_STOPPED ? "Stopped" : "Other");
    len += snprintf(buffer + len, buffer_size - len,
                   "  Scan Interval: %lu ms\n", scanner->scan_interval_ms);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Sectors per Scan: %llu\n", 
                   (unsigned long long)scanner->sectors_per_scan);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Scan Intensity: %u\n\n", scanner->scan_intensity);

    /* Scanning statistics */
    len += snprintf(buffer + len, buffer_size - len,
                   "Scanning Statistics:\n");
    len += snprintf(buffer + len, buffer_size - len,
                   "  Total Scans: %llu\n", total_scans);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Sectors Scanned: %llu\n", sectors_scanned);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Coverage: %u%%\n", scanner->stats.scan_coverage_percent);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Tracked Sectors: %llu\n", (unsigned long long)tracked_sectors);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Memory Usage: %zu bytes\n\n", memory_used);

    /* Health status */
    len += snprintf(buffer + len, buffer_size - len,
                   "Health Status:\n");
    len += snprintf(buffer + len, buffer_size - len,
                   "  Active Warnings: %d\n", active_warnings);
    len += snprintf(buffer + len, buffer_size - len,
                   "  High Risk Sectors: %d\n", high_risk_sectors);
    len += snprintf(buffer + len, buffer_size - len,
                   "  Warnings Issued: %lld\n", 
                   atomic64_read(&scanner->stats.warnings_issued));
    len += snprintf(buffer + len, buffer_size - len,
                   "  Predictions Made: %lld\n\n", 
                   atomic64_read(&scanner->stats.predictions_made));

    /* Recommendations */
    len += snprintf(buffer + len, buffer_size - len,
                   "Recommendations:\n");
    
    if (high_risk_sectors > 0) {
        len += snprintf(buffer + len, buffer_size - len,
                       "  ⚠️  HIGH PRIORITY: %d sectors at high risk - consider backup\n",
                       high_risk_sectors);
    }
    
    if (active_warnings > 10) {
        len += snprintf(buffer + len, buffer_size - len,
                       "  ⚠️  Multiple warnings active - monitor closely\n");
    }
    
    if (scanner->stats.scan_coverage_percent < 50) {
        len += snprintf(buffer + len, buffer_size - len,
                       "  ℹ️  Scan coverage low - consider reducing scan interval\n");
    }
    
    if (total_scans == 0) {
        len += snprintf(buffer + len, buffer_size - len,
                       "  ℹ️  No scans completed yet - scanner may be stopped\n");
    }
    
    if (len >= buffer_size - 100) {
        len += snprintf(buffer + len, buffer_size - len,
                       "\n... (report truncated)\n");
    }

    return len;
}