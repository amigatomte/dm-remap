#include <linux/module.h>           // Core header for loading kernel modules
#include <linux/init.h>             // For module init and exit macros
#include <linux/device-mapper.h>    // Device Mapper API

#define DM_MSG_PREFIX "dm_remap"    // Prefix for logging messages from this module

// This struct holds all the state for your DM target
struct remap_c {
    struct dm_dev *dev;             // Underlying block device (e.g. /dev/sdX)
    sector_t start;                 // Starting sector offset on the physical device

    // Mapping table: bad logical sectors → spare physical sectors
    sector_t bad_sectors[1024];
    sector_t spare_sectors[1024];
    int remap_count;                // Number of remapped sectors

    // Spare sector pool tracking
    sector_t spare_start;          // First spare sector (e.g. end of disk)
    int spare_used;                // How many spare sectors have been assigned
};

// This function is called for every I/O request to the DM target
static int remap_map(struct dm_target *ti, struct bio *bio)
{
    struct remap_c *rc = ti->private;
    sector_t sector = bio->bi_iter.bi_sector;
    int i;

    // Check if the sector is in the remap table
    for (i = 0; i < rc->remap_count; i++) {
        if (sector == rc->bad_sectors[i]) {
            sector = rc->spare_sectors[i];  // Redirect to spare
            break;
        }
    }

    // Set the actual device and sector for the bio
    bio_set_dev(bio, rc->dev->bdev);
    bio->bi_iter.bi_sector = rc->start + sector;

    return DM_MAPIO_REMAPPED;  // Tell DM that we’ve remapped the I/O
}

// Called when the DM target is being destroyed
static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;

    dm_put_device(ti, rc->dev);  // Release the underlying device
    kfree(rc);                   // Free our custom struct
}

// Called when the DM target is being created
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long start;
    int ret;

    // Expecting 3 arguments: device path, start sector, spare start sector
    if (argc != 3) {
        ti->error = "Invalid argument count";
        return -EINVAL;
    }

    rc = kzalloc(sizeof(*rc), GFP_KERNEL);  // Allocate and zero memory
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
    ret = kstrtoull(argv[1], 10, &start);
    if (ret) {
        dm_put_device(ti, rc->dev);
        kfree(rc);
        ti->error = "Invalid start sector";
        return ret;
    }

    rc->start = (sector_t)start;

    // Parse spare sector start
    ret = kstrtoull(argv[2], 10, &start);
    if (ret) {
        dm_put_device(ti, rc->dev);
        kfree(rc);
        ti->error = "Invalid spare start sector";
        return ret;
    }

    rc->spare_start = (sector_t)start;
    rc->remap_count = 0;
    rc->spare_used = 0;

    ti->private = rc;  // Attach our struct to the target

    return 0;
}

// Handle runtime messages (e.g. remap commands)
static int remap_message(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc = ti->private;
    sector_t bad;

    // Expecting: remap <sector>
    if (argc != 2 || strcmp(argv[0], "remap") != 0)
        return -EINVAL;

    // Check if we have space left in the remap table
    if (rc->remap_count >= 1024 || rc->spare_used >= 1024)
        return -ENOSPC;

    // Parse the bad sector number
    if (kstrtoull(argv[1], 10, &bad))
        return -EINVAL;

    // Add to remap table
    rc->bad_sectors[rc->remap_count] = bad;
    rc->spare_sectors[rc->remap_count] = rc->spare_start + rc->spare_used;

    rc->remap_count++;
    rc->spare_used++;

    return 0;
}

// Register your DM target with the kernel
static struct target_type remap_target = {
    .name   = "remap",              // Name used in dmsetup
    .version = {1, 0, 0},
    .module = THIS_MODULE,
    .ctr    = remap_ctr,            // Constructor
    .dtr    = remap_dtr,            // Destructor
    .map    = remap_map,            // I/O mapping
    .message = remap_message,       // Runtime command handler
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
MODULE_DESCRIPTION("Custom DM target with dynamic bad sector remapping");
