#include <linux/module.h>           // Core header for kernel modules
#include <linux/init.h>             // For module init/exit macros
#include <linux/device-mapper.h>    // Device Mapper API
#include <linux/slab.h>             // For kmalloc/kzalloc

#define DM_MSG_PREFIX "dm_remap"
#define MAX_BADBLOCKS 1024          // Max number of remapped sectors

// This struct holds all internal state for your DM target
struct remap_c {
    struct dm_dev *dev;             // Underlying block device (e.g. /dev/sdX)
    sector_t start;                 // Starting offset on the physical device
    sector_t spare_start;          // Beginning of the spare sector pool
    int remap_count;               // Number of remapped sectors
    int spare_used;                // Number of spare sectors assigned

    // Mapping table: logical bad sector → physical spare sector
    sector_t bad_sectors[MAX_BADBLOCKS];
    sector_t spare_sectors[MAX_BADBLOCKS];

    // Validity flag: false = data lost, true = preserved
    bool remap_valid[MAX_BADBLOCKS];
};

// This function is called for every I/O request to the DM target
static int remap_map(struct dm_target *ti, struct bio *bio)
{
    struct remap_c *rc = ti->private;
    sector_t sector = bio->bi_iter.bi_sector;
    int i;

    // Check if the sector is remapped
    for (i = 0; i < rc->remap_count; i++) {
        if (sector == rc->bad_sectors[i]) {

            // If it's a read and the data was lost, fail the I/O upward
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

    // Set the actual device and sector for the bio
    bio_set_dev(bio, rc->dev->bdev);
    bio->bi_iter.bi_sector = rc->start + sector;

    return DM_MAPIO_REMAPPED;
}

// This function handles runtime messages like: dmsetup message remap0 0 remap <sector>
static int remap_message(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc = ti->private;
    sector_t bad;

    // Expecting: remap <sector>
    if (argc != 2 || strcmp(argv[0], "remap") != 0)
        return -EINVAL;

    // Check for space in the remap table
    if (rc->remap_count >= MAX_BADBLOCKS || rc->spare_used >= MAX_BADBLOCKS)
        return -ENOSPC;

    // Parse the bad sector number
    if (kstrtoull(argv[1], 10, &bad))
        return -EINVAL;

    // Add to remap table
    rc->bad_sectors[rc->remap_count] = bad;
    rc->spare_sectors[rc->remap_count] = rc->spare_start + rc->spare_used;
    rc->remap_valid[rc->remap_count] = false;  // Assume data is lost

    pr_info("dm-remap: sector %llu remapped to %llu (data lost)\n",
            (unsigned long long)bad,
            (unsigned long long)rc->spare_sectors[rc->remap_count]);

    rc->remap_count++;
    rc->spare_used++;

    return 0;
}

// This function reports status via dmsetup status
static void remap_status(struct dm_target *ti, status_type_t type,
                         unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i;

    // Count how many remapped sectors have lost data
    for (i = 0; i < rc->remap_count; i++) {
        if (!rc->remap_valid[i])
            lost++;
    }

    // Report status depending on query type
    if (type == STATUSTYPE_INFO) {
        snprintf(result, maxlen, "remapped=%d lost=%d spare_used=%d",
                 rc->remap_count, lost, rc->spare_used);
    } else if (type == STATUSTYPE_TABLE) {
        snprintf(result, maxlen, "%llu %llu",
                 (unsigned long long)rc->start,
                 (unsigned long long)rc->spare_start);
    }
}

// Called when the DM target is created
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long start, spare;
    int ret;

    // Expecting: <device> <start_sector> <spare_start_sector>
    if (argc != 3) {
        ti->error = "Invalid argument count";
        return -EINVAL;
    }

    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc) {
        ti->error = "Memory allocation failed";
        return -ENOMEM;
    }

    // Get the underlying device (e.g. /dev/sdX)
    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &rc->dev);
    if (ret) {
        kfree(rc);
        ti->error = "Device lookup failed";
        return ret;
    }

    // Parse start sector
    if (kstrtoull(argv[1], 10, &start)) {
        dm_put_device(ti, rc->dev);
        kfree(rc);
        ti->error = "Invalid start sector";
        return -EINVAL;
    }

    // Parse spare start sector
    if (kstrtoull(argv[2], 10, &spare)) {
        dm_put_device(ti, rc->dev);
        kfree(rc);
        ti->error = "Invalid spare start sector";
        return -EINVAL;
    }

    rc->start = (sector_t)start;
    rc->spare_start = (sector_t)spare;
    rc->remap_count = 0;
    rc->spare_used = 0;

    ti->private = rc;
    return 0;
}

// Called when the DM target is destroyed
static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;
    dm_put_device(ti, rc->dev);  // Release device reference
    kfree(rc);                   // Free memory
}

// Register the DM target with the kernel
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

// Called when the module is loaded
static int __init remap_init(void)
{
    return dm_register_target(&remap_target);
}

// Called when the module is unloaded
static void __exit remap_exit(void)
{
    dm_unregister_target(&remap_target);
}

module_init(remap_init);
module_exit(remap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping");
