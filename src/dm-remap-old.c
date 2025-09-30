
/*
 * dm-remap v2.0 - Device Mapper target for bad sector remapping
 * 
 * This module remaps bad sectors from a primary device to spare sectors 
 * on a separate block device. v2.0 adds intelligent error detection,
 * automatic remapping, and comprehensive health monitoring.
 *
 * Key v2.0 features:
 * - Automatic bad sector detection from I/O errors
 * - Intelligent retry logic with exponential backoff
 * - Proactive remapping based on error patterns
 * - Health assessment and monitoring
 * - Enhanced statistics and reporting
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>        // Core kernel module support
#include "dm-remap-core.h"       // Core data structures and debug macros
#include "dm-remap-messages.h"   // Debug macros and messaging support
#include "dm-remap-sysfs.h"      // Sysfs interface declarations
#include <linux/init.h>          // For module init/exit macros
#include <linux/device-mapper.h> // Device Mapper API
#include "dm-remap-core.h"       // v2.0 core structures  
#include "dm-remap-error.h"      // v2.0 error handling
#include "dm-remap-io.h"         // v2.0 I/O processing
#include "dm-remap-sysfs.h"      // v2.0 sysfs interface
#include <linux/slab.h>          // For memory allocation
#include <linux/debugfs.h>       // For debugfs signaling
#include <linux/seq_file.h>      // For debugfs table output
#include <linux/blk_types.h>     // For BLK_STS_OK, BLK_STS_IOERR, BLK_STS_MEDIUM
#include <linux/kernel.h>        // For NULL
#include <linux/bio.h>
#include <linux/sysfs.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>         // For msleep in retry logic
#include <linux/workqueue.h>     // For deferred work
#define DM_MSG_PREFIX "dm_remap"

// Module parameters
int debug_level = 0;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug verbosity level (0=quiet, 1=info, 2=debug)");

int max_remaps = 1024;
module_param(max_remaps, int, 0644);
MODULE_PARM_DESC(max_remaps, "Maximum number of remappable sectors per target");

unsigned long dmr_clone_shallow_count = 0;
unsigned long dmr_clone_deep_count = 0;
// remap_endio removed - using direct bio remapping instead of cloning

// DMR_DEBUG is now defined in dm-remap-core.h

// Called for every I/O request to the DM target
// remap_map: Called for every I/O request to the DM target.
// If the sector is remapped, redirect the bio to the spare device and sector.
// Otherwise, pass through to the original device.
// This function is the main I/O path for the target and must be fast and thread-safe.

/* Message handling moved to dm-remap-messages.c - commenting out duplicate function
int remap_message_old(struct dm_target *ti, unsigned argc, char **argv, char *result, unsigned maxlen)
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

    /* v2.0 INFO: Redirect stats to sysfs */
    if (argc == 1 && strcmp(argv[0], "stats") == 0)
    {
        if (maxlen)
            scnprintf(result, maxlen, "use: cat /sys/kernel/dm_remap/<target>/stats");
        return 0;
    }
    
    /* v2.0 INFO: Redirect health to sysfs */
    if (argc == 1 && strcmp(argv[0], "health") == 0)
    {
        if (maxlen)
            scnprintf(result, maxlen, "use: cat /sys/kernel/dm_remap/<target>/health");
        return 0;
    }
    
    /* v2.0 INFO: Redirect scan to sysfs */
    if (argc == 1 && strcmp(argv[0], "scan") == 0)
    {
        if (maxlen)
            scnprintf(result, maxlen, "use: cat /sys/kernel/dm_remap/<target>/scan");
        return 0;
    }
    
    /* v2.0 COMMANDS: auto_remap enable/disable (still works via message) */
    if (argc == 2 && strcmp(argv[0], "auto_remap") == 0)
    {
        if (strcmp(argv[1], "enable") == 0) {
            rc->auto_remap_enabled = true;
            DMR_DEBUG(1, "Auto-remap enabled via message");
            if (maxlen)
                scnprintf(result, maxlen, "enabled (also use: echo enable > /sys/kernel/dm_remap/<target>/auto_remap)");
        } else if (strcmp(argv[1], "disable") == 0) {
            rc->auto_remap_enabled = false;
            DMR_DEBUG(1, "Auto-remap disabled via message");
            if (maxlen)
                scnprintf(result, maxlen, "disabled (also use: echo disable > /sys/kernel/dm_remap/<target>/auto_remap)");
        } else {
            if (maxlen)
                scnprintf(result, maxlen, "error: use 'enable' or 'disable'");
        }
        return 0;
    }

    /* Unknown command */
    if (maxlen)
        scnprintf(result, maxlen, "error: unknown command '%s'", argv[0]);
    return 0;
}
*/

// Handles runtime messages like: dmsetup message remap0 0 remap <sector>
// remap_message: Handles runtime messages from dmsetup for runtime control and inspection.
// Supported commands:
//   remap <bad_sector>   - Remap a bad sector to the next available spare sector
//   load <bad> <spare> <valid> - Load a remap entry (for persistence)
//   clear                - Clear all remap entries
//   verify <sector>      - Query remap status for a sector
/* remap_map function is now in dm-remap-io.c */

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
    
    /* Initialize v2.0 fields */
    rc->write_errors = 0;
    rc->read_errors = 0;
    rc->auto_remaps = 0;
    rc->auto_remap_enabled = true;  /* Enable auto-remap by default */
    rc->error_threshold = 3;        /* Auto-remap after 3 errors */
    rc->overall_health = DMR_DEVICE_HEALTH_EXCELLENT;

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
// remap_init: Module initialization for v2.0 - Registers target and initializes subsystems
static int __init dm_remap_init(void)
{
    int result;
    
    DMR_DEBUG(1, "Initializing dm-remap module");
    
    /* Initialize sysfs interface first */
    result = dmr_sysfs_init();
    if (result) {
        DMR_ERROR("Failed to initialize sysfs interface: %d", result);
        return result;
    }
    
    result = dm_register_target(&remap_target);
    if (result < 0) {
        DMR_ERROR("register failed %d", result);
        dmr_sysfs_exit();
        return result;
    }
    
    DMR_DEBUG(1, "dm-remap module initialized successfully");
    return result;
}

static void __exit dm_remap_exit(void)
{
    DMR_DEBUG(1, "Exiting dm-remap module");
    
    dm_unregister_target(&remap_target);
    dmr_sysfs_exit();
    
    DMR_DEBUG(1, "dm-remap module exited");
}

module_init(dm_remap_init);
module_exit(dm_remap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping v2.0 with intelligent bad sector detection and sysfs interface");


