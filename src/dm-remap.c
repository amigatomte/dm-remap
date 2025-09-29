
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
#include <linux/errno.h>
#include <linux/string.h>
#define DM_MSG_PREFIX "dm_remap"

// Module parameters
static int debug_level = 0;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug verbosity level (0=quiet, 1=info, 2=debug)");

static int max_remaps = 1024;
module_param(max_remaps, int, 0644);
MODULE_PARM_DESC(max_remaps, "Maximum number of remappable sectors per target");

unsigned long dmr_clone_shallow_count = 0;
unsigned long dmr_clone_deep_count = 0;
// remap_endio removed - using direct bio remapping instead of cloning

// Debug logging macro
#define DMR_DEBUG(level, fmt, args...) \
    do { \
        if (debug_level >= level) \
            printk(KERN_INFO "dm-remap: " fmt "\n", ##args); \
    } while (0)

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

    pr_info("dm-remap: message handler called, argc=%u, maxlen=%u\n", argc, maxlen);
    for (i = 0; i < argc; i++) {
        pr_info("dm-remap: argv[%d] = '%s'\n", i, argv[i]);
    }

    /* Ensure result is a valid NUL-terminated string */
    if (maxlen)
    {
        result[0] = '\0';
        result[maxlen - 1] = '\0';
    }

    /* Need at least a command */
    if (argc < 1)
    {
        if (maxlen)
            scnprintf(result, maxlen, "error: missing command");
        return 0;
    }

    /* remap <bad_sector> */
    if (argc == 2 && strcmp(argv[0], "remap") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad))
        {
            if (maxlen)
                scnprintf(result, maxlen, "error: invalid sector '%s'", argv[1]);
            return 0;
        }

        spin_lock(&rc->lock);
        for (i = 0; i < rc->spare_used; i++)
        {
            if (rc->table[i].main_lba == bad)
            {
                spin_unlock(&rc->lock);
                if (maxlen)
                    scnprintf(result, maxlen, "error: already remapped");
                return 0;
            }
        }
        if (rc->spare_used >= rc->spare_len)
        {
            spin_unlock(&rc->lock);
            if (maxlen)
                scnprintf(result, maxlen, "error: no spare slots");
            return 0;
        }

        rc->table[rc->spare_used].main_lba = bad;
        spare = rc->table[rc->spare_used].spare_lba; /* set in ctr */
        rc->spare_used++;
        spin_unlock(&rc->lock);

        if (maxlen)
            scnprintf(result, maxlen, "remapped %llu -> %llu",
                      (unsigned long long)bad,
                      (unsigned long long)spare);
        return 0;
    }
    if (argc == 1 && strcmp(argv[0], "ping") == 0)
    {
        pr_info("dm-remap: handling ping, argc=%u, maxlen=%u\n", argc, maxlen);
        pr_info("dm-remap: argv[0]='%s' at %p, result at %p\n", argv[0], argv[0], result);
        
        /* Try writing response directly over the input argument */
        strcpy(argv[0], "pong");
        pr_info("dm-remap: overwrote argv[0] with 'pong'\n");
        
        /* Also write to result buffer */
        if (maxlen > 4) {
            strcpy(result, "pong");
            pr_info("dm-remap: also wrote 'pong' to result buffer\n");
        }
        return 0;
    }

    /* load <bad> <spare> */
    if (argc == 3 && strcmp(argv[0], "load") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad) || kstrtoull(argv[2], 10, &spare))
        {
            if (maxlen)
                scnprintf(result, maxlen, "error: invalid args");
            return 0;
        }

        spin_lock(&rc->lock);
        for (i = 0; i < rc->spare_used; i++)
        {
            if (rc->table[i].main_lba == bad || rc->table[i].spare_lba == spare)
            {
                spin_unlock(&rc->lock);
                if (maxlen)
                    scnprintf(result, maxlen, "error: conflict");
                return 0;
            }
        }
        if (rc->spare_used >= rc->spare_len)
        {
            spin_unlock(&rc->lock);
            if (maxlen)
                scnprintf(result, maxlen, "error: no spare slots");
            return 0;
        }

        rc->table[rc->spare_used].main_lba = bad;
        rc->table[rc->spare_used].spare_lba = spare;
        rc->spare_used++;
        spin_unlock(&rc->lock);

        if (maxlen)
            scnprintf(result, maxlen, "loaded %llu -> %llu",
                      (unsigned long long)bad,
                      (unsigned long long)spare);
        return 0;
    }

    /* clear */
    if (argc == 1 && strcmp(argv[0], "clear") == 0)
    {
        spin_lock(&rc->lock);
        rc->spare_used = 0;
        for (i = 0; i < rc->spare_len; i++)
            rc->table[i].main_lba = (sector_t)-1;
        spin_unlock(&rc->lock);

        if (maxlen)
            scnprintf(result, maxlen, "cleared");
        return 0;
    }

    /* verify <sector> */
    if (argc == 2 && strcmp(argv[0], "verify") == 0)
    {
        if (kstrtoull(argv[1], 10, &bad))
        {
            if (maxlen)
                scnprintf(result, maxlen, "error: invalid sector '%s'", argv[1]);
            return 0;
        }

        spin_lock(&rc->lock);
        for (i = 0; i < rc->spare_used; i++)
        {
            if (rc->table[i].main_lba == bad)
            {
                spare = rc->table[i].spare_lba;
                spin_unlock(&rc->lock);
                if (maxlen)
                    scnprintf(result, maxlen, "remapped to %llu",
                              (unsigned long long)spare);
                return 0;
            }
        }
        spin_unlock(&rc->lock);

        if (maxlen)
            scnprintf(result, maxlen, "not remapped");
        return 0;
    }

    /* Unknown command */
    if (maxlen)
        scnprintf(result, maxlen, "error: unknown command '%s'", argv[0]);
    return 0;
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
    struct remap_io_ctx *ctx = dmr_per_bio_data(bio, struct remap_io_ctx);
    
    // I/O debug logging - placed at start to capture ALL I/O operations
    if (debug_level >= 2) {
        printk(KERN_INFO "dm-remap: I/O: sector=%llu, size=%u, %s\n", 
               (unsigned long long)sector, bio->bi_iter.bi_size, 
               bio_data_dir(bio) == WRITE ? "WRITE" : "READ");
    }
    
    if (ctx->lba == 0)
    {
        ctx->lba = sector;
        ctx->was_write = (bio_data_dir(bio) == WRITE);
        ctx->retry_to_spare = false;
    }

    // Only auto-remap single-sector bios (512 bytes). Multi-sector bios are passed through for now.
    if (bio->bi_iter.bi_size != 512)
    {
        // For multi-sector bios, just remap directly to main device
        if (debug_level >= 2) {
            printk(KERN_INFO "dm-remap: Multi-sector passthrough: %u bytes\n", bio->bi_iter.bi_size);
        }
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
    }

    // Pass through special ops unless remapped
    if (bio_op(bio) == REQ_OP_FLUSH || bio_op(bio) == REQ_OP_DISCARD || bio_op(bio) == REQ_OP_WRITE_ZEROES)
    {
        bio_set_dev(bio, rc->main_dev->bdev);
        bio->bi_iter.bi_sector = rc->main_start + bio->bi_iter.bi_sector;
        return DM_MAPIO_REMAPPED;
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
            
            if (debug_level >= 1) {
                printk(KERN_INFO "dm-remap: REMAP: sector %llu -> spare sector %llu\n", 
                       (unsigned long long)sector, (unsigned long long)target_sector);
            }
            
            bio_set_dev(bio, target_dev->bdev);
            bio->bi_iter.bi_sector = target_sector;
            return DM_MAPIO_REMAPPED;
        }
    }
    spin_unlock(&rc->lock);

    // Not remapped: submit to primary device
    if (debug_level >= 2) {
        printk(KERN_INFO "dm-remap: Passthrough: sector %llu to main device\n", 
               (unsigned long long)sector);
    }
    
    bio_set_dev(bio, rc->main_dev->bdev);
    bio->bi_iter.bi_sector = rc->main_start + sector;
    return DM_MAPIO_REMAPPED;
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
    unsigned long long spare_start = 0, spare_len = 0;
    blk_mode_t mode = FMODE_READ | FMODE_WRITE;
    int ret, i;

    pr_info("dm-remap: remap_ctr called, argc=%u\n", argc);
    for (i = 0; i < argc; i++)
        pr_info("dm-remap: argv[%d] = %s\n", i, argv[i]);

    if (argc != 4)
    {
        ti->error = "Invalid argument count: expected 4";
        return -EINVAL;
    }

    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc)
    {
        ti->error = "Failed to allocate remap_c";
        return -ENOMEM;
    }

    /* Get main device */
    ret = dm_get_device(ti, argv[0], mode, &rc->main_dev);
    if (ret)
    {
        ti->error = "Failed to get main device";
        kfree(rc);
        return ret;
    }

    /* Get spare device */
    ret = dm_get_device(ti, argv[1], mode, &rc->spare_dev);
    if (ret)
    {
        ti->error = "Failed to get spare device";
        dm_put_device(ti, rc->main_dev);
        kfree(rc);
        return ret;
    }

    /* Parse spare_start */
    ret = kstrtoull(argv[2], 10, &spare_start);
    if (ret)
    {
        ti->error = "Invalid spare_start";
        goto bad;
    }

    /* Parse spare_len */
    ret = kstrtoull(argv[3], 10, &spare_len);
    if (ret)
    {
        ti->error = "Invalid spare_len";
        goto bad;
    }

    rc->spare_start = (sector_t)spare_start;
    rc->spare_len = (sector_t)spare_len;
    rc->spare_used = 0;
    spin_lock_init(&rc->lock);

    /* Apply max_remaps limit */
    if (rc->spare_len > max_remaps) {
        DMR_DEBUG(0, "Limiting spare_len from %llu to %d (max_remaps parameter)", 
                  (unsigned long long)rc->spare_len, max_remaps);
        rc->spare_len = max_remaps;
    }

    /* Safety check */
    if (!rc->spare_dev || rc->spare_len == 0)
    {
        ti->error = "Spare device missing or length zero";
        goto bad;
    }
    
    DMR_DEBUG(0, "Constructor: main_dev=%s, spare_dev=%s, spare_start=%llu, spare_len=%llu", 
              argv[0], argv[1], (unsigned long long)spare_start, (unsigned long long)rc->spare_len);

    /* Allocate remap table */
    rc->table = kcalloc(rc->spare_len, sizeof(struct remap_entry), GFP_KERNEL);
    if (!rc->table)
    {
        ti->error = "Remap table allocation failed";
        goto bad;
    }

    /* Initialize remap table */
    for (i = 0; i < rc->spare_len; i++)
    {
        rc->table[i].main_lba = (sector_t)-1;
        rc->table[i].spare_lba = rc->spare_start + i;
    }

    ti->private = rc;
    pr_info("dm-remap: target created successfully\n");
    return 0;

bad:
    if (rc->main_dev)
        dm_put_device(ti, rc->main_dev);
    if (rc->spare_dev)
        dm_put_device(ti, rc->spare_dev);
    kfree(rc);
    return -EINVAL;
}

static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;

    pr_info("dm-remap: remap_dtr called, starting cleanup\n");

    if (!rc)
    {
        pr_warn("dm-remap: ti->private is NULL, nothing to clean up\n");
        return;
    }

    /* Free remap table if allocated */
    if (rc->table)
    {
        kfree(rc->table);
        pr_info("dm-remap: freed remap table\n");
    }

    /* Release main device if acquired */
    if (rc->main_dev)
    {
        dm_put_device(ti, rc->main_dev);
        pr_info("dm-remap: released main device\n");
    }

    /* Release spare device if acquired */
    if (rc->spare_dev)
    {
        dm_put_device(ti, rc->spare_dev);
        pr_info("dm-remap: released spare device\n");
    }

    /* Free the control structure */
    kfree(rc);
    pr_info("dm-remap: freed remap_c struct\n");
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
