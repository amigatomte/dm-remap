
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
static void remap_endio(struct bio *bio)
{
    struct bio *orig = bio->bi_private;
    dmr_endio(orig, bio->bi_status);
    bio_put(bio);
}
// Called for every I/O request to the DM target
// remap_map: Called for every I/O request to the DM target.
// If the sector is remapped, redirect the bio to the spare device and sector.
// Otherwise, pass through to the original device.
// This function is the main I/O path for the target and must be fast and thread-safe.
static int remap_message(struct dm_target *ti, unsigned argc, char **argv, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    sector_t bad, spare;
    int i;

    // remap <bad_sector>
    if (argc == 2 && strcmp(argv[0], "remap") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad))
            return -EINVAL;
        spin_lock(&rc->lock);
        // Check for duplicate
        for (i = 0; i < rc->spare_used; i++)
        {
            if (rc->table[i].main_lba == bad)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
        }
        if (rc->spare_used >= rc->spare_len)
        {
            spin_unlock(&rc->lock);
            return -ENOSPC;
        }
        rc->table[rc->spare_used].main_lba = bad;
        // spare_lba is already set in ctr
        rc->spare_used++;
        spin_unlock(&rc->lock);
        pr_info("dm-remap: manually remapped sector %llu to spare %llu\n",
                (unsigned long long)bad,
                (unsigned long long)rc->table[rc->spare_used - 1].spare_lba);
        return 0;
    }

    // load <bad> <spare>
    if (argc == 3 && strcmp(argv[0], "load") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad) || kstrtoull(argv[2], 10, &spare))
            return -EINVAL;
        spin_lock(&rc->lock);
        for (i = 0; i < rc->spare_used; i++)
        {
            if (rc->table[i].main_lba == bad || rc->table[i].spare_lba == spare)
            {
                spin_unlock(&rc->lock);
                return -EEXIST;
            }
        }
        if (rc->spare_used >= rc->spare_len)
        {
            spin_unlock(&rc->lock);
            return -ENOSPC;
        }
        rc->table[rc->spare_used].main_lba = bad;
        rc->table[rc->spare_used].spare_lba = spare;
        rc->spare_used++;
        spin_unlock(&rc->lock);
        pr_info("dm-remap: loaded remap %llu → %llu\n",
                (unsigned long long)bad,
                (unsigned long long)spare);
        return 0;
    }

    // clear: Reset the remap table and usage counters
    if (argc == 1 && strcmp(argv[0], "clear") == 0)
    {
        spin_lock(&rc->lock);
        rc->spare_used = 0;
        for (i = 0; i < rc->spare_len; i++)
            rc->table[i].main_lba = (sector_t)-1;
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
        for (i = 0; i < rc->spare_used; i++)
        {
            if (rc->table[i].main_lba == bad)
            {
                snprintf(result, maxlen, "remapped to %llu",
                         (unsigned long long)rc->table[i].spare_lba);
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
    struct dm_dev *target_dev = rc->main_dev;
    sector_t target_sector = rc->main_start + sector;
    struct bio *clone;
    struct remap_io_ctx *ctx = dmr_per_bio_data(bio, struct remap_io_ctx);
    if (ctx->lba == 0)
    {
        ctx->lba = sector;
        ctx->was_write = (bio_data_dir(bio) == WRITE);
        ctx->retry_to_spare = false;
    }

    // Only auto-remap single-sector bios (512 bytes). Multi-sector bios are passed through for now.
    if (bio->bi_iter.bi_size != 512)
    {
        if (!clone)
        {
            dmr_endio(bio, BLK_STS_IOERR);
            return DM_MAPIO_SUBMITTED;
        }
        bio_set_dev(clone, rc->main_dev->bdev);
        clone->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        clone->bi_end_io = (dm_remap_endio_fn)remap_endio;
        clone->bi_private = bio;
        submit_bio(clone);
        return DM_MAPIO_SUBMITTED;
    }

    // Pass through special ops unless remapped
    if (bio_op(bio) == REQ_OP_FLUSH || bio_op(bio) == REQ_OP_DISCARD || bio_op(bio) == REQ_OP_WRITE_ZEROES)
    {
        clone = dmr_bio_clone_shallow(bio, GFP_NOIO);
        if (!clone)
        {
            dmr_endio(bio, BLK_STS_IOERR);
            return DM_MAPIO_SUBMITTED;
        }
        bio_set_dev(clone, rc->main_dev->bdev);
        clone->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        clone->bi_end_io = (dm_remap_endio_fn)remap_endio;
        clone->bi_private = bio;
        submit_bio(clone);
        return DM_MAPIO_SUBMITTED;
    }

    // Check if sector is remapped
    spin_lock(&rc->lock);
    for (i = 0; i < rc->spare_used; i++)
    {
        if (sector == rc->table[i].main_lba && rc->table[i].main_lba != (sector_t)-1)
        {
            // Redirect to spare device and sector
            target_dev = rc->spare_dev;
            target_sector = rc->table[i].spare_lba;
            spin_unlock(&rc->lock);
            clone = dmr_bio_clone_shallow(bio, GFP_NOIO);
            if (!clone)
            {
                dmr_endio(bio, BLK_STS_IOERR);
                return DM_MAPIO_SUBMITTED;
            }
            bio_set_dev(clone, target_dev->bdev);
            clone->bi_iter.bi_sector = target_sector;
            clone->bi_end_io = (dm_remap_endio_fn)remap_endio;
            clone->bi_private = bio;
            submit_bio(clone);
            return DM_MAPIO_SUBMITTED;
        }
    }
    spin_unlock(&rc->lock);

    // Not remapped: submit to primary device
    clone = dmr_bio_clone_shallow(bio, GFP_NOIO);
    if (!clone)
    {
        dmr_endio(bio, BLK_STS_IOERR);
        return DM_MAPIO_SUBMITTED;
    }
    bio_set_dev(clone, rc->main_dev->bdev);
    clone->bi_iter.bi_sector = rc->main_start + sector;
    clone->bi_end_io = (dm_remap_endio_fn)remap_endio;
    submit_bio(clone);
    return DM_MAPIO_SUBMITTED;
}

// Reports status via dmsetup status
static void remap_status(struct dm_target *ti, status_type_t type,
                         // remap_status: Reports status via dmsetup status.
                         // Shows number of remapped sectors, lost sectors, and spare usage.
                         // This function is used by dmsetup to report target status.
                         unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i, remapped = 0;

    for (i = 0; i < rc->spare_used; i++)
    {
        if (rc->table[i].main_lba != (sector_t)-1)
            remapped++;
        else
            lost++;
    }

    if (type == STATUSTYPE_INFO)
    {
        int percent = rc->spare_len ? (100 * rc->spare_used) / rc->spare_len : 0;
        if (percent > 100)
            percent = 100;
        snprintf(result, maxlen,
                 "remapped=%d lost=%d spare_used=%llu/%llu (%d%%)",
                 remapped, lost, (unsigned long long)rc->spare_used, (unsigned long long)rc->spare_len, percent);
    }
    else if (type == STATUSTYPE_TABLE)
    {
        snprintf(result, maxlen, "%llu %llu",
                 (unsigned long long)rc->main_start,
                 (unsigned long long)rc->spare_start);
    }
}

// --- remap_ctr and remap_dtr definitions ---
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc = NULL;
    unsigned long long __maybe_unused main_start = 0;
    unsigned long long spare_start = 0, spare_len = 0;

    int ret = 0;

    if (argc != 5)
        return -EINVAL;

    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc)
    {
        ti->error = "Failed to allocate remap_c";
        return -ENOMEM;
    }
    // Parse spare_start and spare_len from argv[2] and argv[3]
    ret = kstrtoull(argv[2], 10, &spare_start);
    if (ret)
    {
        ti->error = "Invalid spare_start";
        kfree(rc);
        return -EINVAL;
    }

    ret = kstrtoull(argv[3], 10, &spare_len);
    if (ret)
    {
        ti->error = "Invalid spare_len";
        kfree(rc);
        return -EINVAL;
    }

    blk_mode_t mode = FMODE_READ | FMODE_WRITE;

    ret = dm_get_device(ti, argv[0], mode, &rc->main_dev);
    if (ret)
    {
        ti->error = "Failed to get main device";
        kfree(rc);
        return ret;
    }

    ret = dm_get_device(ti, argv[1], mode, &rc->spare_dev);
    if (ret)
    {
        dm_put_device(ti, rc->main_dev);
        ti->error = "Failed to get spare device";
        kfree(rc);
        return ret;
    }

    pr_info("dm-remap: creating target with spare_start=%llu spare_len=%llu\n", spare_start, spare_len);

    // ...existing code...
    rc->spare_start = (sector_t)spare_start;
    rc->spare_len = (sector_t)spare_len;
    rc->spare_used = 0;
    spin_lock_init(&rc->lock);

    // Safety: fail if spare is missing or zero length
    if (!rc->spare_dev || rc->spare_len == 0)
    {
        dm_put_device(ti, rc->main_dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Spare device missing or length zero";
        return -EINVAL;
    }

    // Allocate remap table (volatile, size = spare_len)
    rc->table = kcalloc(rc->spare_len, sizeof(struct remap_entry), GFP_KERNEL);
    if (!rc->table)
    {
        dm_put_device(ti, rc->main_dev);
        dm_put_device(ti, rc->spare_dev);
        kfree(rc);
        ti->error = "Remap table allocation failed";
        return -ENOMEM;
    }

    // Initialize remap table entries to invalid
    for (ret = 0; ret < rc->spare_len; ret++)
    {
        rc->table[ret].main_lba = (sector_t)-1;
        rc->table[ret].spare_lba = rc->spare_start + ret;
    }

    ti->private = rc;
    return 0;
}

static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;

    pr_info("dm-remap: remap_dtr called, cleaning up\n");

    if (!rc)
        return;

    if (rc->main_dev)
        dm_put_device(ti, rc->main_dev);

    if (rc->spare_dev)
        dm_put_device(ti, rc->spare_dev);

    if (rc->table)
        kfree(rc->table);

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
    .message = remap_message,
    .status = remap_status,
};

// Debugfs: show remap table

// Module init: register target + create debugfs trigger
// remap_init: Module initialization. Registers target and sets up debugfs.
// v1: No sysfs/debugfs/list code. Only register/unregister target.
static int __init remap_init(void)
{
    int ret = dm_register_target(&remap_target);
    if (ret == -EEXIST)
        pr_warn("dm-remap: target 'remap' already registered\n");
    else if (ret)
        pr_warn("dm-remap: failed to register target: %d\n", ret);
    else
        pr_info("dm-remap: module loaded\n");
    return ret;
}

static void __exit remap_exit(void)
{
    dm_unregister_target(&remap_target);
    pr_info("dm-remap: module unloaded\n");
}

module_init(remap_init);
module_exit(remap_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping with external persistence and debugfs signaling");
