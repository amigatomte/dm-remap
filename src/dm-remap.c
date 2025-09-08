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

#define DM_MSG_PREFIX "dm_remap"

// Debugfs trigger for user-space daemon
static struct dentry *remap_debugfs_dir;
// static struct dentry *remap_trigger_file;
static u32 remap_trigger = 0;

// Remove unused variable from previous single-instance sysfs implementation
// static struct remap_c *global_remap_c;

// remap_c_list: Global linked list of all active dm-remap targets for sysfs summary
static LIST_HEAD(remap_c_list);

// summary_kobj: Global sysfs kobject for summary directory
static struct kobject *summary_kobj;
// Global sysfs attribute structs for summary
static struct kobj_attribute total_remaps_attr;
static struct kobj_attribute total_spare_used_attr;
static struct kobj_attribute total_spare_remaining_attr;

// Called for every I/O request to the DM target
// remap_map: Called for every I/O request to the DM target.
// If the sector is remapped, redirect the bio to the spare device and sector.
// Otherwise, pass through to the original device.
// This function is the main I/O path for the target and must be fast and thread-safe.
static int remap_map(struct dm_target *ti, struct bio *bio)
{
    struct remap_c *rc = ti->private;         // Get target context
    sector_t sector = bio->bi_iter.bi_sector; // Requested sector
    int i;
    struct dm_dev *target_dev = rc->dev;         // Default to main device
    sector_t target_sector = rc->start + sector; // Default to main device offset

    // Check if the sector is in the spare area of the spare device
    // This prevents user I/O from accessing spare sectors directly
    if (sector >= rc->spare_start && sector < rc->spare_start + rc->spare_total)
    {
        pr_warn("dm-remap: access to spare sector %llu denied\n",
                (unsigned long long)sector);
        return DM_MAPIO_KILL;
    }

    // Check if the sector is remapped
    // If so, redirect to the spare device and sector
    spin_lock(&rc->lock); // Protect remap table access
    for (i = 0; i < rc->remap_count; i++)
    {
        if (sector == rc->remaps[i].orig_sector)
        {
            // Fail read if data was lost
            if (bio_data_dir(bio) == READ && !rc->remaps[i].valid)
            {
                pr_warn("dm-remap: read from sector %llu failed — data lost\n",
                        (unsigned long long)sector);
                spin_unlock(&rc->lock);
                return DM_MAPIO_KILL;
            }
            // Redirect to spare device and sector
            target_dev = rc->remaps[i].spare_dev ? rc->remaps[i].spare_dev : rc->spare_dev;
            target_sector = rc->remaps[i].spare_sector;
            break;
        }
    }
    spin_unlock(&rc->lock);

    // Set device and sector for the bio
    bio_set_dev(bio, target_dev->bdev);
    bio->bi_iter.bi_sector = target_sector;

    return DM_MAPIO_REMAPPED;
}

// Handles runtime messages like: dmsetup message remap0 0 remap <sector>
// remap_message: Handles runtime messages from dmsetup for runtime control and inspection.
// Supported commands:
//   remap <bad_sector>   - Remap a bad sector to the next available spare sector
//   load <bad> <spare> <valid> - Load a remap entry (for persistence)
//   clear                - Clear all remap entries
//   verify <sector>      - Query remap status for a sector
static int remap_message(struct dm_target *ti,
                         unsigned argc,
                         char **argv,
                         char *result,
                         unsigned maxlen)
{
    struct remap_c *rc = ti->private; // Get target context
    sector_t bad, spare;
    unsigned int valid;
    int i;

    // remap <bad_sector>: Add a new bad sector to the remap table
    if (argc == 2 && strcmp(argv[0], "remap") == 0)
    {
        // Check if remap table is full
        if (rc->remap_count >= rc->spare_total || rc->spare_used >= rc->spare_total)
            return -ENOSPC;
        // Parse bad sector argument
        if (kstrtoull(argv[1], 10, &bad))
            return -EINVAL;
        // Prevent duplicate remaps
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
        {
            if (rc->remaps[i].orig_sector == bad)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
            if (rc->remaps[i].spare_sector == rc->spare_start + rc->spare_used)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
        }
        // Add new remap entry
        rc->remaps[rc->remap_count].orig_sector = bad;
        rc->remaps[rc->remap_count].spare_dev = rc->spare_dev;
        rc->remaps[rc->remap_count].spare_sector = rc->spare_start + rc->spare_used;
        rc->remaps[rc->remap_count].valid = false; // Assume data lost
        rc->remap_count++;
        rc->spare_used++;
        remap_trigger++; // Signal user-space daemon
        spin_unlock(&rc->lock);
        pr_info("dm-remap: sector %llu remapped to %llu (data lost)\n",
                (unsigned long long)bad,
                (unsigned long long)(rc->spare_start + rc->spare_used - 1));
        return 0;
    }

    // load <bad> <spare> <valid>: Load a remap entry (used for restoring state)
    if (argc == 4 && strcmp(argv[0], "load") == 0)
    {
        if (rc->remap_count >= rc->spare_total)
            return -ENOSPC;
        // Parse arguments
        if (kstrtoull(argv[1], 10, &bad) ||
            kstrtoull(argv[2], 10, &spare) ||
            kstrtouint(argv[3], 10, &valid))
            return -EINVAL;
        // Prevent duplicate remaps
        spin_lock(&rc->lock);
        for (i = 0; i < rc->remap_count; i++)
        {
            if (rc->remaps[i].orig_sector == bad)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
            if (rc->remaps[i].spare_sector == spare)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
        }
        // Add loaded remap entry
        rc->remaps[rc->remap_count].orig_sector = bad;
        rc->remaps[rc->remap_count].spare_dev = rc->spare_dev;
        rc->remaps[rc->remap_count].spare_sector = spare;
        rc->remaps[rc->remap_count].valid = (valid != 0);
        rc->remap_count++;
        remap_trigger++; // Signal daemon to re-save
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
        memset(rc->remaps, 0, rc->spare_total * sizeof(struct remap_entry)); // Zero out remap table
        remap_trigger++;                                                     // Signal daemon to re-save
        spin_unlock(&rc->lock);
        pr_info("dm-remap: remap table cleared\n");
        return 0;
    }

    // verify <sector>: Check if a sector is remapped and report its status
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

    // Unknown command: return error
    return -EINVAL;
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

static ssize_t spare_total_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        if (rc->kobj == kobj)
            return sysfs_emit(buf, "%llu\n", (unsigned long long)rc->spare_total);
    }
    return -ENODEV;
}

static ssize_t spare_used_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        if (rc->kobj == kobj)
            return sysfs_emit(buf, "%d\n", rc->spare_used);
    }
    return -ENODEV;
}

// remap_count_show: sysfs attribute handler for per-target remap_count
// Returns the number of sectors currently remapped for this target
static ssize_t remap_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        if (rc->kobj == kobj)
            return sysfs_emit(buf, "%d\n", rc->remap_count);
    }
    return -ENODEV;
}

// lost_count_show: sysfs attribute handler for per-target lost_count
// Returns the number of remapped sectors that are marked as lost (not valid)
static ssize_t lost_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    int lost = 0, i;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        if (rc->kobj == kobj)
        {
            spin_lock(&rc->lock);
            for (i = 0; i < rc->remap_count; i++)
            {
                if (!rc->remaps[i].valid)
                    lost++;
            }
            spin_unlock(&rc->lock);
            return sysfs_emit(buf, "%d\n", lost);
        }
    }
    return -ENODEV;
}

// spare_remaining_show: sysfs attribute handler for per-target spare_remaining
// Returns the number of spare sectors left for remapping
static ssize_t spare_remaining_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct remap_c *rc;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        if (rc->kobj == kobj)
            return sysfs_emit(buf, "%llu\n", (unsigned long long)(rc->spare_total - rc->spare_used));
    }
    return -ENODEV;
}

// clear_store: sysfs attribute handler for writable clear
// Writing '1' resets the remap table and usage counters for this target
// Ensures thread safety with locking
static ssize_t clear_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct remap_c *rc;
    unsigned long val;
    if (kstrtoul(buf, 10, &val) || val != 1)
        return -EINVAL;
    list_for_each_entry(rc, &remap_c_list, list)
    {
        if (rc->kobj == kobj)
        {
            spin_lock(&rc->lock);
            rc->remap_count = 0;
            rc->spare_used = 0;
            memset(rc->remaps, 0, rc->spare_total * sizeof(struct remap_entry)); // Zero out remap table
            spin_unlock(&rc->lock);
            return count;
        }
    }
    return -ENODEV;
}

// Remove unused global summary functions if not referenced elsewhere
// static ssize_t total_remaps_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
// static ssize_t total_spare_used_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
// static ssize_t total_spare_remaining_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static struct kobj_attribute spare_total_attr = __ATTR_RO(spare_total);
static struct kobj_attribute spare_used_attr = __ATTR_RO(spare_used);
static struct kobj_attribute remap_count_attr = __ATTR_RO(remap_count);
static struct kobj_attribute lost_count_attr = __ATTR_RO(lost_count);
static struct kobj_attribute spare_remaining_attr = __ATTR_RO(spare_remaining);
static struct kobj_attribute clear_attr = __ATTR(clear, 0220, NULL, clear_store);

// remap_ctr: Target constructor. Initializes remap_c, allocates remap table, sets up sysfs and debugfs.
// Arguments: dev start spare_dev spare_start spare_total
// Returns: 0 on success, negative error code on failure
// This function is called when the target is created by dmsetup.
// It parses arguments, allocates memory, and registers sysfs/debugfs entries.
// All error paths clean up resources to avoid leaks.
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long start, spare_start, spare_total;
    int ret;

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

    if (kstrtoull(argv[1], 10, &start) || kstrtoull(argv[3], 10, &spare_start))
    {
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Invalid sector arguments";
        return -EINVAL;
    }

    rc->start = (sector_t)start;
    rc->spare_start = (sector_t)spare_start;

    // Parse mandatory spare_total
    if (kstrtoull(argv[4], 10, &spare_total))
    {
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Invalid spare_total argument";
        return -EINVAL;
    }
    rc->spare_total = (sector_t)spare_total;

    rc->remap_count = 0;
    rc->spare_used = 0;
    spin_lock_init(&rc->lock);
    // Allocate remap table and handle errors
    rc->remaps = kcalloc(rc->spare_total, sizeof(struct remap_entry), GFP_KERNEL);
    if (!rc->remaps)
    {
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Remap table allocation failed";
        return -ENOMEM;
    }
    // Create per-target sysfs directory and attributes
    rc->kobj = kobject_create_and_add("dm_remap_stats", kernel_kobj);
    if (!rc->kobj)
    {
        kfree(rc->remaps);
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Failed to create sysfs kobject";
        return -ENOMEM;
    }
    // Register all sysfs attributes for this target
    if (sysfs_create_file(rc->kobj, &spare_total_attr.attr) ||
        sysfs_create_file(rc->kobj, &spare_used_attr.attr) ||
        sysfs_create_file(rc->kobj, &remap_count_attr.attr) ||
        sysfs_create_file(rc->kobj, &lost_count_attr.attr) ||
        sysfs_create_file(rc->kobj, &spare_remaining_attr.attr) ||
        sysfs_create_file(rc->kobj, &clear_attr.attr))
    {
        kobject_put(rc->kobj);
        kfree(rc->remaps);
        dm_put_device(ti, rc->dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Failed to create sysfs attributes";
        return -ENOMEM;
    }
    // Add to global list for summary and multi-instance support
    INIT_LIST_HEAD(&rc->list);
    list_add(&rc->list, &remap_c_list);

    ti->private = rc;
    return 0;
}

// Called when the target is destroyed
static void remap_dtr(struct dm_target *ti)
// remap_dtr: Target destructor. Cleans up device references, memory, sysfs, and removes from global list.
// This function is called when the target is removed.
// It releases all resources and unregisters sysfs/debugfs entries.
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

// Debugfs: show remap table
static int remap_table_show(struct seq_file *m, void *v)
// remap_table_show: Outputs the remap table to debugfs for user-space inspection.
// Format: bad=<sector> spare=<sector> dev=<name> valid=<0|1>
// This function is used for debugging and monitoring from user space.
{
    struct remap_c *rc = m->private;
    int i;
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

// Target registration
static struct target_type remap_target = {
    // remap_target: Device Mapper target registration structure.
    .name = "remap",
    .version = {1, 0, 0},
    .module = THIS_MODULE,
    .ctr = remap_ctr,
    .dtr = remap_dtr,
    .map = remap_map,
    .message = remap_message,
    .status = remap_status,
};

// Module init: register target + create debugfs trigger
static int __init remap_init(void)
// remap_init: Module initialization. Registers target and sets up debugfs.
{
    int ret = dm_register_target(&remap_target);
    if (ret)
        return ret;

    remap_debugfs_dir = debugfs_create_dir("dm_remap", NULL);
    debugfs_create_u32("trigger", 0644, remap_debugfs_dir, &remap_trigger);
    debugfs_create_file("remap_table", 0444, remap_debugfs_dir, NULL, &remap_table_fops);
    summary_kobj = kobject_create_and_add("summary", kernel_kobj);
    if (summary_kobj)
    {
        if (sysfs_create_file(summary_kobj, &total_remaps_attr.attr))
            pr_warn("Failed to create total_remaps sysfs file\n");

        if (sysfs_create_file(summary_kobj, &total_spare_used_attr.attr))
            pr_warn("Failed to create total_spare_used sysfs file\n");

        if (sysfs_create_file(summary_kobj, &total_spare_remaining_attr.attr))
            pr_warn("Failed to create total_spare_remaining sysfs file\n");
    }
    pr_info("dm-remap: module loaded\n");
    return 0;
}

// Module exit: unregister target + remove debugfs
static void __exit remap_exit(void)
// remap_exit: Module cleanup. Unregisters target and removes debugfs entries.
{
    debugfs_remove_recursive(remap_debugfs_dir);
    if (summary_kobj)
        kobject_put(summary_kobj);
    dm_unregister_target(&remap_target);
    pr_info("dm-remap: module unloaded\n");
}

module_init(remap_init);
module_exit(remap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Roth");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping with external persistence and debugfs signaling");
