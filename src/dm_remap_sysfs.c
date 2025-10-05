/*
 * dm-remap-sysfs.c - Sysfs interface for dm-remap v2.0
 * 
 * This file implements the proper sysfs interface for v2.0 features
 * since device-mapper messages don't reliably return data to userspace.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/string.h>

#include "dm-remap-core.h"
#include "dm-remap-error.h"
#include "dm-remap-sysfs.h"

/* Global sysfs directory for all dm-remap targets */
static struct kobject *dmr_kobj = NULL;

/*
 * Per-target sysfs attribute: health
 */
static ssize_t health_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    u8 health;
    u32 total_errors;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
        
    health = 1; /* Default to healthy for now */
    total_errors = rc->write_errors + rc->read_errors;
    
    return scnprintf(buf, PAGE_SIZE, "health=%s spare_usage=%llu/%llu errors=%u\n",
                     "good",
                     (unsigned long long)rc->spare_used, 
                     (unsigned long long)rc->spare_len,
                     total_errors);
}

/*
 * Per-target sysfs attribute: stats  
 */
static ssize_t stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    u32 total_errors;
    u32 bad_sectors = 0, healthy_sectors = 0;
    int i;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
        
    total_errors = rc->write_errors + rc->read_errors;
    
    /* Count sector health - simplified for v1.1 compatibility */
    spin_lock(&rc->lock);
    for (i = 0; i < rc->spare_used; i++) {
        if (rc->table[i].main_lba != (sector_t)-1) {
            healthy_sectors++; /* Assume remapped sectors are healthy for now */
        }
    }
    spin_unlock(&rc->lock);
    
    return scnprintf(buf, PAGE_SIZE, 
                     "errors=%u remapped=%llu bad=%u healthy=%u auto_remapped=%u health=%s\n",
                     total_errors, (unsigned long long)rc->spare_used, 
                     bad_sectors, healthy_sectors, rc->auto_remaps, 
                     "good");
}

/*
 * Per-target sysfs attribute: scan
 */
static ssize_t scan_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    int suspect_count = 0, bad_count = 0, remapped_count = 0;
    int i;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
    
    spin_lock(&rc->lock);
    for (i = 0; i < rc->spare_used; i++) {
        if (rc->table[i].main_lba != (sector_t)-1) {
            remapped_count++; /* Count all remapped sectors for now */
        }
    }
    spin_unlock(&rc->lock);
    
    return scnprintf(buf, PAGE_SIZE, "scan: suspect=%d bad=%d remapped=%d total=%llu\n",
                     suspect_count, bad_count, remapped_count, 
                     (unsigned long long)rc->spare_used);
}

/*
 * Per-target sysfs attribute: auto_remap (read/write)
 */
static ssize_t auto_remap_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
    return scnprintf(buf, PAGE_SIZE, "%s\n", rc->auto_remap_enabled ? "enabled" : "disabled");
}

static ssize_t auto_remap_store(struct kobject *kobj, struct kobj_attribute *attr,
                                const char *buf, size_t count)
{
    struct remap_c *rc;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
    
    if (strncmp(buf, "enable", 6) == 0) {
        rc->auto_remap_enabled = true;
        DMR_DEBUG(1, "Auto-remap enabled via sysfs");
    } else if (strncmp(buf, "disable", 7) == 0) {
        rc->auto_remap_enabled = false;
        DMR_DEBUG(1, "Auto-remap disabled via sysfs");
    } else {
        return -EINVAL;
    }
    
    return count;
}

/*
 * Per-target sysfs attribute: error_threshold (read/write)
 */
static ssize_t error_threshold_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
    return scnprintf(buf, PAGE_SIZE, "%u\n", rc->error_threshold);
}

static ssize_t error_threshold_store(struct kobject *kobj, struct kobj_attribute *attr,
                                     const char *buf, size_t count)
{
    struct remap_c *rc;
    u32 threshold;
    
    if (!kobj || !buf)
        return -EINVAL;
        
    rc = container_of(kobj, struct remap_c, kobj);
    if (!rc)
        return -EINVAL;
    
    if (kstrtou32(buf, 10, &threshold) || threshold == 0 || threshold > 100)
        return -EINVAL;
    
    rc->error_threshold = threshold;
    DMR_DEBUG(1, "Error threshold set to %u via sysfs", threshold);
    
    return count;
}

/* Define sysfs attributes */
static struct kobj_attribute health_attr = __ATTR_RO(health);
static struct kobj_attribute stats_attr = __ATTR_RO(stats);
static struct kobj_attribute scan_attr = __ATTR_RO(scan);
static struct kobj_attribute auto_remap_attr = __ATTR_RW(auto_remap);
static struct kobj_attribute error_threshold_attr = __ATTR_RW(error_threshold);

/* Array of per-target attributes */
static struct attribute *dmr_target_attrs[] = {
    &health_attr.attr,
    &stats_attr.attr,
    &scan_attr.attr,
    &auto_remap_attr.attr,
    &error_threshold_attr.attr,
    NULL,
};

/* Attribute group for per-target sysfs directory */
static struct attribute_group dmr_target_attr_group = {
    .attrs = dmr_target_attrs,
};

/*
 * dmr_sysfs_create_target() - Create sysfs directory for a target
 * 
 * @rc: Target context
 * @target_name: Name for the sysfs directory
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_sysfs_create_target(struct remap_c *rc, const char *target_name)
{
    int ret;
    
    if (!dmr_kobj) {
        DMR_DEBUG(0, "Global sysfs not initialized");
        return -ENODEV;
    }
    
    /* Initialize target kobject */
    kobject_init(&rc->kobj, &ktype_dmr_target);
    
    /* Add target directory under /sys/module/dm_remap/targets/ */
    ret = kobject_add(&rc->kobj, dmr_kobj, "%s", target_name);
    if (ret) {
        DMR_DEBUG(0, "Failed to create target sysfs directory '%s': error %d", target_name, ret);
        kobject_put(&rc->kobj);
        return ret;
    }

    /* Create attribute files */
    ret = sysfs_create_group(&rc->kobj, &dmr_target_attr_group);
    if (ret) {
        int i;
        for (i = 0; dmr_target_attrs[i]; i++) {
            if (!dmr_target_attrs[i]->name) continue;
            DMR_DEBUG(0, "Attribute setup failed: %s", dmr_target_attrs[i]->name);
        }
        DMR_DEBUG(0, "Failed to create target sysfs attributes for '%s': error %d", target_name, ret);
        kobject_del(&rc->kobj);
        kobject_put(&rc->kobj);
        return ret;
    }

    DMR_DEBUG(1, "Created sysfs directory for target: %s", target_name);
    return 0;
}

/*
 * dmr_sysfs_remove_target() - Remove sysfs directory for a target
 * 
 * @rc: Target context
 */
void dmr_sysfs_remove_target(struct remap_c *rc)
{
    if (rc->kobj.state_initialized) {
        sysfs_remove_group(&rc->kobj, &dmr_target_attr_group);
        kobject_del(&rc->kobj);
        kobject_put(&rc->kobj);
        DMR_DEBUG(1, "Removed sysfs directory for target");
    }
}

/*
 * Global sysfs attributes
 */
static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "dm-remap v2.0 - Intelligent Bad Sector Detection\n");
}

static ssize_t targets_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    /* This would list all active targets - simplified for now */
    return scnprintf(buf, PAGE_SIZE, "Use 'dmsetup ls --target remap' to list active targets\n");
}

static struct kobj_attribute version_attr = __ATTR_RO(version);
static struct kobj_attribute targets_attr = __ATTR_RO(targets);

static struct attribute *dmr_global_attrs[] = {
    &version_attr.attr,
    &targets_attr.attr,
    NULL,
};

static struct attribute_group dmr_global_attr_group = {
    .attrs = dmr_global_attrs,
};

/* Kobject type for targets */
static void dmr_target_release(struct kobject *kobj)
{
    /* Nothing special needed for release */
}

/* Sysfs operations for target attributes */
static ssize_t dmr_target_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
    struct kobj_attribute *kattr = container_of(attr, struct kobj_attribute, attr);
    return kattr->show ? kattr->show(kobj, kattr, buf) : -ENOENT;
}

static ssize_t dmr_target_attr_store(struct kobject *kobj, struct attribute *attr, 
                                     const char *buf, size_t count)
{
    struct kobj_attribute *kattr = container_of(attr, struct kobj_attribute, attr);
    return kattr->store ? kattr->store(kobj, kattr, buf, count) : -ENOENT;
}

static const struct sysfs_ops dmr_target_sysfs_ops = {
    .show = dmr_target_attr_show,
    .store = dmr_target_attr_store,
};

struct kobj_type ktype_dmr_target = {
    .release = dmr_target_release,
    .sysfs_ops = &dmr_target_sysfs_ops,
};

/*
 * dmr_sysfs_init() - Initialize global sysfs interface
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_sysfs_init(void)
{
    int ret;
    
    /* Create main sysfs directory: /sys/module/dm_remap/ */
    dmr_kobj = kobject_create_and_add("dm_remap", kernel_kobj);
    if (!dmr_kobj) {
        DMR_DEBUG(0, "Failed to create main sysfs directory");
        return -ENOMEM;
    }
    
    /* Create global attributes */
    ret = sysfs_create_group(dmr_kobj, &dmr_global_attr_group);
    if (ret) {
        DMR_DEBUG(0, "Failed to create global sysfs attributes: %d", ret);
        kobject_put(dmr_kobj);
        dmr_kobj = NULL;
        return ret;
    }
    
    DMR_DEBUG(1, "Initialized global sysfs interface");
    return 0;
}

/*
 * dmr_sysfs_exit() - Cleanup global sysfs interface
 */
void dmr_sysfs_exit(void)
{
    if (dmr_kobj) {
        sysfs_remove_group(dmr_kobj, &dmr_global_attr_group);
        kobject_put(dmr_kobj);
        dmr_kobj = NULL;
        DMR_DEBUG(1, "Cleaned up global sysfs interface");
    }
}