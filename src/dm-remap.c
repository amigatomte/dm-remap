#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include "dm-remap.h"
#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/types.h>
#include "compat.h"
// Removed: struct dm_remap_ctx (replaced by per-io context)
// ...existing code...

// End_io callback for probe bios (auto-remap)
// Removed: probe bio endio callback (no longer needed)
// End_io callback for automatic remapping

// Device Mapper target: dm-remap
// This module remaps bad sectors from a primary device to spare sectors on a separate block device.
// It supports dynamic remapping, persistent state, debugfs integration, and per-target sysfs monitoring.
//
// Key features:
// - Dynamically sized remap table (user-supplied size)
// - Per-target sysfs directory with attributes for monitoring and control
// - Global sysfs summary for all targets
// - Thread-safe operations using spinlocks
// - Debugfs table output for user-space inspection

#include <linux/module.h>        // Core kernel module support
#include <linux/init.h>          // For module init/exit macros
#include <linux/device-mapper.h> // Device Mapper API
#include "dm-remap.h"            // Shared data structures and constants
#include <linux/slab.h>          // For memory allocation
#include <linux/debugfs.h>       // For debugfs signaling
#include <linux/seq_file.h>      // For debugfs table output
#include <linux/kobject.h>       // For sysfs integration
#include <linux/list.h>          // For multi-instance sysfs support
#include <linux/blk_types.h>     // For BLK_STS_OK, BLK_STS_IOERR, BLK_STS_MEDIUM, BLK_STS_TARGET
#include <linux/blk_types.h>     // For BLK_STS_OK, BLK_STS_IOERR, BLK_STS_MEDIUM
#include <linux/kernel.h>        // For NULL
#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/blk_types.h> // For BLK_STS_OK
#include <linux/kernel.h>    // For NULL
#include <linux/sysfs.h>
#define DM_MSG_PREFIX "dm_remap"

unsigned long dmr_clone_shallow_count = 0;
unsigned long dmr_clone_deep_count = 0;

static ssize_t name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", kobj->name);
}

static struct kobj_attribute name_attr = __ATTR_RO(name);

// Debugfs trigger for user-space daemon
static struct dentry *remap_debugfs_dir;
// static struct dentry *remap_trigger_file;
static u32 remap_trigger = 0;

// Remove unused variable from previous single-instance sysfs implementation
// static struct remap_c *global_remap_c;

// remap_c_list: Global linked list of all active dm-remap targets for sysfs summary
static LIST_HEAD(remap_c_list);

// Global sysfs kobjects for summary statistics
static struct kobject *dm_remap_kobj;       // /sys/kernel/dm_remap
static struct kobject *dm_remap_stats_kobj; // /sys/kernel/dm_remap_stats
static bool dm_remap_stats_initialized = false;

// Sysfs show functions for global statistics
static ssize_t total_remaps_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int total = 0;
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
        total += rc->remap_count;
    return sysfs_emit(buf, "%d\n", total);
}

static ssize_t total_spare_used_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int used = 0;
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
        used += rc->spare_used;
    return sysfs_emit(buf, "%d\n", used);
}

static ssize_t total_spare_remaining_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    unsigned long long remaining = 0;
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
        remaining += rc->spare_total - rc->spare_used;
    return sysfs_emit(buf, "%llu\n", remaining);
}

// Sysfs attributes for summary group
static struct kobj_attribute total_remaps_attr = __ATTR_RO(total_remaps);
static struct kobj_attribute total_spare_used_attr = __ATTR_RO(total_spare_used);
static struct kobj_attribute total_spare_remaining_attr = __ATTR_RO(total_spare_remaining);

static struct attribute *summary_attrs[] = {
    &total_remaps_attr.attr,
    &total_spare_used_attr.attr,
    &total_spare_remaining_attr.attr,
    NULL,
};

static struct attribute_group summary_attr_group = {
    .name = "summary",
    .attrs = summary_attrs,
};

// --- Per-target sysfs show/store function definitions ---
static ssize_t auto_remap_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%d\n", rc->auto_remap_enabled ? 1 : 0);
    return -ENODEV;
}

static ssize_t auto_remap_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct remap_c *rc;
    unsigned long val;
    if (kstrtoul(buf, 10, &val))
        return -EINVAL;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj)
    {
        rc->auto_remap_enabled = !!val;
        return count;
    }
    return -ENODEV;
}

static struct kobj_attribute auto_remap_enabled_attr = __ATTR(auto_remap_enabled, 0644, auto_remap_enabled_show, auto_remap_enabled_store);
static ssize_t auto_remap_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%d\n", atomic_read(&rc->auto_remap_count));
    return -ENODEV;
}

static ssize_t last_bad_sector_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%llu\n", (unsigned long long)rc->last_bad_sector);
    return -ENODEV;
}

static ssize_t spares_remaining_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%llu\n", (unsigned long long)(rc->spare_total - rc->spare_used));
    return -ENODEV;
}

static struct kobj_attribute auto_remap_count_attr = __ATTR_RO(auto_remap_count);
static struct kobj_attribute last_bad_sector_attr = __ATTR_RO(last_bad_sector);
static struct kobj_attribute spares_remaining_attr = __ATTR_RO(spares_remaining);
static ssize_t spare_total_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%llu\n", (unsigned long long)rc->spare_total);
    return -ENODEV;
}

static ssize_t spare_used_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%d\n", rc->spare_used);
    return -ENODEV;
}

static ssize_t remap_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%d\n", rc->remap_count);
    return -ENODEV;
}

static ssize_t lost_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    int lost = 0, i;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj)
    {
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
            if (!rc->remaps[i].valid)
                lost++;
        spin_unlock(&rc->lock);
        return sysfs_emit(buf, "%d\n", lost);
    }
    return -ENODEV;
}

static ssize_t spare_remaining_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%llu\n", (unsigned long long)(rc->spare_total - rc->spare_used));
    return -ENODEV;
}

// Helper for timestamp formatting
#include <linux/timekeeping.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#include <linux/string.h>

static void format_timestamp(char *buf, size_t buflen, time64_t t)
{
    struct tm tm;
    time64_to_tm(t, 0, &tm);
    snprintf(buf, buflen, "%04ld-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// Add last_reset_time to remap_c
// (struct remap_c is defined in dm-remap.h, ensure it has: char last_reset_time[32];)

static ssize_t last_reset_time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj) return sysfs_emit(buf, "%s\n", rc->last_reset_time);
    return -ENODEV;
}

static struct kobj_attribute last_reset_time_attr = __ATTR_RO(last_reset_time);

static ssize_t clear_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct remap_c *rc;
    char input[16];
    bool confirmed = false;
    size_t len = min(count, (size_t)15);
    strncpy(input, buf, len);
    input[len] = '\0';
    if (strcmp(input, "1") == 0 || strcmp(input, "reset") == 0)
        confirmed = true;
    if (!confirmed)
        return -EINVAL;
    list_for_each_entry(rc, &remap_c_list, list) if (rc->kobj == kobj)
    {
        spin_lock(&rc->lock);
        rc->remap_count = 0;
        rc->spare_used = 0;
        memset(rc->remaps, 0, rc->spare_total * sizeof(struct remap_entry));
        // Update last_reset_time
        format_timestamp(rc->last_reset_time, sizeof(rc->last_reset_time), ktime_get_real_seconds());
        spin_unlock(&rc->lock);
        // Log with target name and timestamp
        pr_info("dm-remap: remap table for target '%s' reset at %s\n", kobject_name(rc->kobj), rc->last_reset_time);
        return count;
    }
    return -ENODEV;
}

// --- Per-target sysfs attribute definitions ---
static struct kobj_attribute spare_total_attr = __ATTR_RO(spare_total);
static struct kobj_attribute spare_used_attr = __ATTR_RO(spare_used);
static struct kobj_attribute remap_count_attr = __ATTR_RO(remap_count);
static struct kobj_attribute lost_count_attr = __ATTR_RO(lost_count);
static struct kobj_attribute spare_remaining_attr = __ATTR_RO(spare_remaining);
static struct kobj_attribute clear_attr = __ATTR(clear, 0220, NULL, clear_store);
static struct attribute *target_attrs[] = {
    &auto_remap_enabled_attr.attr,
    &spare_total_attr.attr,
    &spare_used_attr.attr,
    &remap_count_attr.attr,
    &lost_count_attr.attr,
    &spare_remaining_attr.attr,
    &clear_attr.attr,
    &last_reset_time_attr.attr,
    &auto_remap_count_attr.attr,
    &last_bad_sector_attr.attr,
    &spares_remaining_attr.attr,
    NULL,
};
static struct attribute_group target_attr_group = {
    .attrs = target_attrs,
};

// Called for every I/O request to the DM target
// remap_map: Called for every I/O request to the DM target.
// If the sector is remapped, redirect the bio to the spare device and sector.
// Otherwise, pass through to the original device.
// This function is the main I/O path for the target and must be fast and thread-safe.
static int remap_message(struct dm_target *ti, unsigned argc, char **argv, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    sector_t bad, spare;
    int valid, i;

    // remap <bad_sector>
    if (argc == 2 && strcmp(argv[0], "remap") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad))
            return -EINVAL;
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
        {
            if (bad == rc->remaps[i].orig_sector)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
        }
        if (rc->spare_used >= rc->spare_total)
        {
            spin_unlock(&rc->lock);
            return -ENOSPC;
        }
        rc->remaps[rc->remap_count].orig_sector = bad;
        rc->remaps[rc->remap_count].spare_dev = rc->spare_dev;
        rc->remaps[rc->remap_count].spare_sector = rc->spare_start + rc->spare_used;
        rc->remaps[rc->remap_count].valid = false;
        rc->remap_count++;
        rc->spare_used++;
        spin_unlock(&rc->lock);
        pr_info("dm-remap: manually remapped sector %llu to spare %llu\n",
                (unsigned long long)bad,
                (unsigned long long)(rc->spare_start + rc->spare_used - 1));
        return 0;
    }

    // load <bad> <spare> <valid>
    if (argc == 4 && strcmp(argv[0], "load") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad) || kstrtoull(argv[2], 10, &spare) || kstrtoint(argv[3], 10, &valid))
            return -EINVAL;
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
        {
            if (bad == rc->remaps[i].orig_sector || rc->remaps[i].spare_sector == spare)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
        }
        rc->remaps[rc->remap_count].orig_sector = bad;
        rc->remaps[rc->remap_count].spare_dev = rc->spare_dev;
        rc->remaps[rc->remap_count].spare_sector = spare;
        rc->remaps[rc->remap_count].valid = (valid != 0);
        rc->remap_count++;
        remap_trigger++;
        spin_unlock(&rc->lock);
        pr_info("dm-remap: loaded remap %llu → %llu (valid=%u)\n",
                (unsigned long long)bad,
                (unsigned long long)spare,
                valid);
        return 0;
    }

    // clear: Reset the remap table and usage counters
    if (argc == 1 && strcmp(argv[0], "clear") == 0)
    {
        spin_lock(&rc->lock);
        rc->remap_count = 0;
        rc->spare_used = 0;
        memset(rc->remaps, 0, rc->spare_total * sizeof(struct remap_entry));
        remap_trigger++;
        spin_unlock(&rc->lock);
        pr_info("dm-remap: remap table cleared\n");
        return 0;
    }

    // verify <sector>
    if (argc == 2 && strcmp(argv[0], "verify") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad))
            return -EINVAL;
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
        {
            if (bad == rc->remaps[i].orig_sector)
            {
                snprintf(result, maxlen, "remapped to %llu valid=%d",
                         (unsigned long long)rc->remaps[i].spare_sector,
                         rc->remaps[i].valid);
                spin_unlock(&rc->lock);
                return 0;
            }
        }
        spin_unlock(&rc->lock);
        snprintf(result, maxlen, "not remapped");
        return 0;
    }

    // Unknown command
    return -EINVAL;
}
// Handles runtime messages like: dmsetup message remap0 0 remap <sector>
// remap_message: Handles runtime messages from dmsetup for runtime control and inspection.
// Supported commands:
//   remap <bad_sector>   - Remap a bad sector to the next available spare sector
//   load <bad> <spare> <valid> - Load a remap entry (for persistence)
//   clear                - Clear all remap entries
//   verify <sector>      - Query remap status for a sector
static int remap_map(struct dm_target *ti, struct bio *bio)
{
    struct remap_c *rc = ti->private;
    sector_t sector = bio->bi_iter.bi_sector;
    int i;
    struct dm_dev *target_dev = rc->dev;
    sector_t target_sector = rc->start + sector;
    struct remap_io_ctx *ctx = dmr_per_bio_data(bio, struct remap_io_ctx);
    if (ctx->lba == 0)
    {
        ctx->lba = sector;
        ctx->was_write = (bio_data_dir(bio) == WRITE);
        ctx->retry_to_spare = false;
    }

    // MVP: Only auto-remap single-sector bios (512 bytes). Multi-sector bios are passed through for now.
    if (bio->bi_iter.bi_size != 512)
    {
        bio_set_dev(bio, rc->dev->bdev);
        bio->bi_iter.bi_sector = rc->start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
    }

    // Pass through special ops unless remapped
    if (bio_op(bio) == REQ_OP_FLUSH || bio_op(bio) == REQ_OP_DISCARD || bio_op(bio) == REQ_OP_WRITE_ZEROES)
    {
        bio_set_dev(bio, rc->dev->bdev);
        bio->bi_iter.bi_sector = rc->start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
    }

    // Check if sector is remapped
    spin_lock(&rc->lock);
    for (i = 0; i < rc->remap_count; i++)
    {
        if (sector == rc->remaps[i].orig_sector)
        {
            // Redirect to spare device and sector
            target_dev = rc->remaps[i].spare_dev ? rc->remaps[i].spare_dev : rc->spare_dev;
            target_sector = rc->remaps[i].spare_sector;
            spin_unlock(&rc->lock);
            bio_set_dev(bio, target_dev->bdev);
            bio->bi_iter.bi_sector = target_sector;
            return DM_MAPIO_REMAPPED;
        }
    }
    spin_unlock(&rc->lock);

    // Not remapped: submit to primary device, handle errors asynchronously
    if (!rc->auto_remap_enabled || bio->bi_iter.bi_size != 512)
    {
        // Just pass through if auto-remap is disabled or bio is not 1 sector
        bio_set_dev(bio, target_dev->bdev);
        bio->bi_iter.bi_sector = target_sector;
        return DM_MAPIO_REMAPPED;
    }
    // No probe bio: auto-remap handled via DM end_io retry path
    return DM_MAPIO_REMAPPED;
}

static int dm_remap_endio(struct dm_target *ti, struct bio *bio, blk_status_t *status)
{
    struct remap_c *rc = ti->private;

    /* Ignore success or errors from spare path */
    if (*status == BLK_STS_OK || bio->bi_bdev == rc->spare_dev->bdev)
        return DM_ENDIO_DONE;

    /* Only act if auto-remap is enabled and it's a hard error */
    if (!rc->auto_remap_enabled)
        return DM_ENDIO_DONE;
    if (*status != BLK_STS_IOERR && *status != BLK_STS_MEDIUM)
        return DM_ENDIO_DONE;

    sector_t logical = bio->bi_iter.bi_sector - rc->start;
    int i;

    spin_lock(&rc->lock);
    /* Already remapped? */
    for (i = 0; i < rc->remap_count; i++)
    {
        if (logical == rc->remaps[i].orig_sector)
        {
            spin_unlock(&rc->lock);
            bio_set_dev(bio, (rc->remaps[i].spare_dev ? rc->remaps[i].spare_dev : rc->spare_dev)->bdev);
            bio->bi_iter.bi_sector = rc->remaps[i].spare_sector;
            *status = BLK_STS_OK;
            return DM_ENDIO_REQUEUE;
        }
    }
    /* Spare exhausted? */
    if (rc->spare_used >= rc->spare_total)
    {
        pr_warn("dm-remap: no spare sectors available for auto-remap\n");
        spin_unlock(&rc->lock);
        return DM_ENDIO_DONE;
    }
    /* Insert new mapping */
    rc->remaps[rc->remap_count].orig_sector = logical;
    rc->remaps[rc->remap_count].spare_dev = rc->spare_dev;
    rc->remaps[rc->remap_count].spare_sector = rc->spare_start + rc->spare_used;
    rc->remaps[rc->remap_count].valid = false;
    rc->remap_count++;
    rc->spare_used++;
    atomic_inc(&rc->auto_remap_count);
    rc->last_bad_sector = logical;
    pr_info("dm-remap: auto-remapped sector %llu to spare %llu\n",
            (unsigned long long)logical,
            (unsigned long long)(rc->spare_start + rc->spare_used - 1));
    spin_unlock(&rc->lock);

    /* Rewrite bio to spare and requeue */
    bio_set_dev(bio, rc->spare_dev->bdev);
    bio->bi_iter.bi_sector = rc->spare_start + rc->spare_used - 1;
    *status = BLK_STS_OK;
    return DM_ENDIO_REQUEUE;
}

// Reports status via dmsetup status
static void remap_status(struct dm_target *ti, status_type_t type,
                         // remap_status: Reports status via dmsetup status.
                         // Shows number of remapped sectors, lost sectors, and spare usage.
                         // This function is used by dmsetup to report target status.
                         unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i;

    for (i = 0; i < rc->remap_count; i++)
    {
        if (!rc->remaps[i].valid)
            lost++;
    }

    if (type == STATUSTYPE_INFO)
    {
        int percent = rc->spare_total ? (100 * rc->spare_used) / rc->spare_total : 0;
        if (percent > 100)
            percent = 100;
        snprintf(result, maxlen,
                 "remapped=%d lost=%d spare_used=%d/%llu (%d%%)",
                 rc->remap_count, lost, rc->spare_used, (unsigned long long)rc->spare_total, percent);
    }
    else if (type == STATUSTYPE_TABLE)
    {
        snprintf(result, maxlen, "%llu %llu",
                 (unsigned long long)rc->start,
                 (unsigned long long)rc->spare_start);
    }
}

// --- remap_ctr and remap_dtr definitions ---
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long start, spare_start, spare_total;
    int ret;
    // pr_info("remap_ctr: creating kobject for target name: %s\n", ti->name);

    // Argument parsing and validation
    if (argc != 5)
    {
        ti->error = "Invalid argument count (expected 5: dev start spare_dev spare_start spare_total)";
        return -EINVAL;
    }

    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc)
    {
        ti->error = "Memory allocation failed";
        return -ENOMEM;
    }

    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &rc->dev);
    if (ret)
    {
        kfree(rc);
        ti->error = "Device lookup failed";
        return ret;
    }

    ret = dm_get_device(ti, argv[2], dm_table_get_mode(ti->table), &rc->spare_dev);
    if (ret)
    {
        dm_put_device(ti, rc->dev);
        kfree(rc);
        ti->error = "Spare device lookup failed";
        return ret;
    }

    if (kstrtoull(argv[1], 10, &start) || kstrtoull(argv[3], 10, &spare_start) || kstrtoull(argv[4], 10, &spare_total))
    {
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Invalid sector arguments";
        return -EINVAL;
    }

    rc->start = (sector_t)start;
    rc->spare_start = (sector_t)spare_start;
    rc->spare_total = (sector_t)spare_total;
    rc->remap_count = 0;
    rc->spare_used = 0;
    // Create per-target sysfs directory and attributes
    static atomic_t remap_kobj_counter = ATOMIC_INIT(0);
    char target_name[64];
    snprintf(target_name, sizeof(target_name), "remap_kobject_%u",
             atomic_inc_return(&remap_kobj_counter)); // Use index for uniqueness
    pr_info("remap_ctr: creating kobject with name: %s\n", target_name);

    rc->kobj = kobject_create_and_add(target_name, dm_remap_kobj);
    if (!rc->kobj)
    {
        pr_warn("Failed to create kobject for target %s\n", target_name);
        kfree(rc->remaps);
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Failed to create sysfs kobject";
        return -ENOMEM;
    }
    // Add the 'name' attribute to the kobject
    if (sysfs_create_file(rc->kobj, &name_attr.attr))
    {
        pr_warn("Failed to create 'name' sysfs file for target %s\n", target_name);
        kobject_put(rc->kobj);
        kfree(rc->remaps);
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Failed to create sysfs attribute";
        return -ENOMEM;
    }
    // Register all sysfs attributes for this target
    if (sysfs_create_group(rc->kobj, &target_attr_group))
    {
        kobject_put(rc->kobj);
        kfree(rc->remaps);
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Failed to create sysfs attribute group";
        return -ENOMEM;
    }
    INIT_LIST_HEAD(&rc->list);
    list_add(&rc->list, &remap_c_list);
    ti->private = rc;
    return 0;
}

static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;
    if (rc->kobj)
        kobject_put(rc->kobj);
    list_del(&rc->list);
    kfree(rc->remaps);
    dm_put_device(ti, rc->dev);
    if (rc->spare_dev)
        dm_put_device(ti, rc->spare_dev);
    kfree(rc);
}

// --- remap_target struct ---
static struct target_type remap_target = {
    .name = "remap",
    .version = {1, 0, 0},
    .module = THIS_MODULE,
    .ctr = remap_ctr,
    .dtr = remap_dtr,
    .map = remap_map,
    .end_io = dm_remap_endio,
    .message = remap_message,
    .status = remap_status,
};

// Debugfs: show remap table
static int remap_table_show(struct seq_file *m, void *v)
// remap_table_show: Outputs the remap table to debugfs for user-space inspection.
// Format: bad=<sector> spare=<sector> dev=<name> valid=<0|1>
// This function is used for debugging and monitoring from user space.
{
    /* List all remap tables for all targets */
    struct remap_c *rc;
    int i;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
        {
            seq_printf(m, "bad=%llu spare=%llu dev=%s valid=%d\n",
                       (unsigned long long)rc->remaps[i].orig_sector,
                       (unsigned long long)rc->remaps[i].spare_sector,
                       rc->remaps[i].spare_dev ? rc->remaps[i].spare_dev->name : "default",
                       rc->remaps[i].valid ? 1 : 0);
        }
        spin_unlock(&rc->lock);
    }
    return 0;
}

static int remap_table_open(struct inode *inode, struct file *file)
{
    return single_open(file, remap_table_show, inode->i_private);
}

static const struct file_operations remap_table_fops = {
    .owner = THIS_MODULE,
    .open = remap_table_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
static void __init dmr_compat_selftest(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
    pr_info("dm-remap: per-bio data shim: 2-arg form (bio, sizeof(type))\n");
    pr_info("dm-remap: bio clone shim: using bio_alloc_clone(bdev, bio, gfp, NULL)\n");
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    pr_info("dm-remap: per-bio data shim: 1-arg form (bio)\n");
    pr_info("dm-remap: bio clone shim: using bio_dup() / bio_alloc_clone(bdev, bio, gfp, NULL)\n");
#else
    pr_info("dm-remap: per-bio data shim: 1-arg form (bio)\n");
    pr_info("dm-remap: bio clone shim: using bio_clone_fast()/bio_clone_bioset()\n");
#endif
}

static void __exit dmr_compat_usage_report(void)
{
    if (dmr_clone_shallow_count == 0 && dmr_clone_deep_count == 0)
        pr_warn("dm-remap: WARNING - clone shims were never used during module lifetime\n");
    else
        pr_info("dm-remap: clone shim usage - shallow: %lu, deep: %lu\n",
                dmr_clone_shallow_count, dmr_clone_deep_count);
}

// Module init: register target + create debugfs trigger
// remap_init: Module initialization. Registers target and sets up debugfs.
static int __init remap_init(void)
{
    int ret = 0;
    bool target_registered = false;

    dm_remap_stats_kobj = NULL;
    dm_remap_kobj = NULL;
    remap_debugfs_dir = NULL;
    dm_remap_stats_initialized = false;

    ret = dm_register_target(&remap_target);
    if (ret == -EEXIST) {
        pr_warn("dm-remap: target 'remap' already registered\n");
        goto out;
    }
    if (ret) {
        pr_warn("dm-remap: failed to register target: %d\n", ret);
        goto out;
    }
    target_registered = true;

    dm_remap_stats_kobj = kobject_create_and_add("dm_remap_stats", kernel_kobj);
    if (!dm_remap_stats_kobj) {
        pr_warn("Failed to create dm_remap_stats kobject\n");
        ret = -ENOMEM;
        goto err_stats_kobj;
    }
    ret = sysfs_create_group(dm_remap_stats_kobj, &summary_attr_group);
    if (ret) {
        pr_warn("Failed to create sysfs group for dm_remap_stats\n");
        goto err_stats_group;
    }
    dm_remap_stats_initialized = true;

    remap_debugfs_dir = debugfs_create_dir("dm_remap", NULL);
    if (!remap_debugfs_dir) {
        pr_warn("Failed to create debugfs directory\n");
        ret = -ENOMEM;
        goto err_debugfs;
    }
    debugfs_create_u32("trigger", 0644, remap_debugfs_dir, &remap_trigger);
    debugfs_create_file("remap_table", 0444, remap_debugfs_dir, NULL, &remap_table_fops);

    dm_remap_kobj = kobject_create_and_add("dm_remap", kernel_kobj);
    if (!dm_remap_kobj) {
        pr_warn("Failed to create dm_remap parent kobject\n");
        ret = -ENOMEM;
        goto err_remap_kobj;
    }

    pr_info("dm-remap: module loaded\n");
    dmr_compat_selftest();
    return 0;

err_remap_kobj:
    if (dm_remap_kobj) {
        kobject_put(dm_remap_kobj);
        dm_remap_kobj = NULL;
    }
    debugfs_remove_recursive(remap_debugfs_dir);
    remap_debugfs_dir = NULL;
err_debugfs:
    if (dm_remap_stats_initialized) {
        sysfs_remove_group(dm_remap_stats_kobj, &summary_attr_group);
        kobject_put(dm_remap_stats_kobj);
        dm_remap_stats_kobj = NULL;
        dm_remap_stats_initialized = false;
    }
err_stats_group:
    if (dm_remap_stats_kobj) {
        kobject_put(dm_remap_stats_kobj);
        dm_remap_stats_kobj = NULL;
    }
err_stats_kobj:
    if (target_registered)
        dm_unregister_target(&remap_target);
out:
    return ret;
}

// Module exit: unregister target + remove debugfs
// remap_exit: Module cleanup. Unregisters target and removes debugfs entries.
static void __exit remap_exit(void)
{
    if (dm_remap_stats_initialized && dm_remap_stats_kobj) {
        sysfs_remove_group(dm_remap_stats_kobj, &summary_attr_group);
        kobject_put(dm_remap_stats_kobj);
        dm_remap_stats_kobj = NULL;
        dm_remap_stats_initialized = false;
    }
    if (dm_remap_kobj) {
        kobject_put(dm_remap_kobj);
        dm_remap_kobj = NULL;
    }
    debugfs_remove_recursive(remap_debugfs_dir);
    remap_debugfs_dir = NULL;
    dm_unregister_target(&remap_target);
    dmr_compat_usage_report();
    pr_info("dm-remap: module unloaded\n");
}

module_init(remap_init);
module_exit(remap_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping with external persistence and debugfs signaling");
