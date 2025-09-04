#include <linux/module.h>           // Core kernel module support
#include <linux/init.h>             // For module init/exit macros
#include <linux/device-mapper.h>    // Device Mapper API
#include <linux/slab.h>             // For memory allocation
#include <linux/debugfs.h>          // For debugfs signaling

#define DM_MSG_PREFIX "dm_remap"
#define MAX_BADBLOCKS 1024          // Max number of remapped sectors

// Internal state for the remap target
struct remap_c {
    struct dm_dev *dev;             // Underlying block device
    sector_t start;                 // Start offset for usable sectors
    sector_t spare_start;           // Start offset for spare sector pool
    int remap_count;                // Number of remapped sectors
    int spare_used;                 // Number of spare sectors assigned

    sector_t bad_sectors[MAX_BADBLOCKS];    // Logical sectors marked bad
    sector_t spare_sectors[MAX_BADBLOCKS];  // Physical sectors used as remap targets
    bool remap_valid[MAX_BADBLOCKS];        // Data validity flag (false = lost)
};

// Debugfs trigger for user-space daemon
static struct dentry *remap_debugfs_dir;
static struct dentry *remap_trigger_file;
static u32 remap_trigger = 0;

// Called for every I/O request to the DM target
static int remap_map(struct dm_target *ti, struct bio *bio)
{
    struct remap_c *rc = ti->private;
    sector_t sector = bio->bi_iter.bi_sector;
    int i;

    // Check if the sector is remapped
    for (i = 0; i < rc->remap_count; i++) {
        if (sector == rc->bad_sectors[i]) {

            // Fail read if data was lost
            if (bio_data_dir(bio) == READ && !rc->remap_valid[i]) {
                pr_warn("dm-remap: read from sector %llu failed — data lost\n",
                        (unsigned long long)sector);
                return DM_MAPIO_KILL;
            }

            // Redirect to spare sector
            sector = rc->spare_sectors[i];
            break;
        }
    }

    // Set device and sector for the bio
    bio_set_dev(bio, rc->dev->bdev);
    bio->bi_iter.bi_sector = rc->start + sector;

    return DM_MAPIO_REMAPPED;
}

// Handles runtime messages like: dmsetup message remap0 0 remap <sector>
static int remap_message(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc = ti->private;
    sector_t bad, spare;
    unsigned int valid;

    // remap <bad_sector>
    if (argc == 2 && strcmp(argv[0], "remap") == 0) {
        if (rc->remap_count >= MAX_BADBLOCKS || rc->spare_used >= MAX_BADBLOCKS)
            return -ENOSPC;

        if (kstrtoull(argv[1], 10, &bad))
            return -EINVAL;

        rc->bad_sectors[rc->remap_count] = bad;
        rc->spare_sectors[rc->remap_count] = rc->spare_start + rc->spare_used;
        rc->remap_valid[rc->remap_count] = false;  // Assume data lost

        rc->remap_count++;
        rc->spare_used++;
        remap_trigger++;  // Signal user-space daemon

        pr_info("dm-remap: sector %llu remapped to %llu (data lost)\n",
                (unsigned long long)bad,
                (unsigned long long)rc->spare_sectors[rc->remap_count - 1]);

        return 0;
    }

    // load <bad> <spare> <valid>
    if (argc == 4 && strcmp(argv[0], "load") == 0) {
        if (rc->remap_count >= MAX_BADBLOCKS)
            return -ENOSPC;

        if (kstrtoull(argv[1], 10, &bad) ||
            kstrtoull(argv[2], 10, &spare) ||
            kstrtouint(argv[3], 10, &valid))
            return -EINVAL;

        rc->bad_sectors[rc->remap_count] = bad;
        rc->spare_sectors[rc->remap_count] = spare;
        rc->remap_valid[rc->remap_count] = (valid != 0);

        rc->remap_count++;
        remap_trigger++;  // Signal daemon to re-save

        pr_info("dm-remap: loaded remap %llu → %llu (valid=%u)\n",
                (unsigned long long)bad,
                (unsigned long long)spare,
                valid);

        return 0;
    }

    // clear
    if (argc == 1 && strcmp(argv[0], "clear") == 0) {
        rc->remap_count = 0;
        rc->spare_used = 0;
        remap_trigger++;  // Signal daemon to re-save

        pr_info("dm-remap: remap table cleared\n");
        return 0;
    }

    return -EINVAL;
}

// Reports status via dmsetup status
static void remap_status(struct dm_target *ti, status_type_t type,
                         unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i;

    for (i = 0; i < rc->remap_count; i++) {
        if (!rc->remap_valid[i])
            lost++;
    }

    if (type == STATUSTYPE_INFO) {
        snprintf(result, maxlen, "remapped=%d lost=%d spare_used=%d",
                 rc->remap_count, lost, rc->spare_used);
    } else if (type == STATUSTYPE_TABLE) {
        snprintf(result, maxlen, "%llu %llu",
                 (unsigned long long)rc->start,
                 (unsigned long long)rc->spare_start);
    }
}

// Called when the target is created
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long start, spare;
    int ret;

    if (argc != 3) {
        ti->error = "Invalid argument count";
        return -EINVAL;
    }

    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc) {
        ti->error = "Memory allocation failed";
        return -ENOMEM;
    }

    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &rc->dev);
    if (ret) {
        kfree(rc);
        ti->error = "Device lookup failed";
        return ret;
    }

    if (kstrtoull(argv[1], 10, &start) || kstrtoull(argv[2], 10, &spare)) {
        dm_put_device(ti, rc->dev);
        kfree(rc);
        ti->error = "Invalid sector arguments";
        return -EINVAL;
    }

    rc->start = (sector_t)start;
    rc->spare_start = (sector_t)spare;
    rc->remap_count = 0;
    rc->spare_used = 0;

    ti->private = rc;
    return 0;
}

// Called when the target is destroyed
static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;
    dm_put_device(ti, rc->dev);
    kfree(rc);
}

// Target registration
static struct target_type remap_target = {
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
{
    int ret = dm_register_target(&remap_target);
    if (ret)
        return ret;

    remap_debugfs_dir = debugfs_create_dir("dm_remap", NULL);
    remap_trigger_file = debugfs_create_u32("trigger", 0644, remap_debugfs_dir, &remap_trigger);

    pr_info("dm-remap: module loaded\n");
    return 0;
}

// Module exit: unregister target + remove debugfs
static void __exit remap_exit(void)
{
    debugfs_remove_recursive(remap_debugfs_dir);
    dm_unregister_target(&remap_target);
    pr_info("dm-remap: module unloaded\n");
}

module_init(remap_init);
module_exit(remap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian");
MODULE_DESCRIPTION("Device
