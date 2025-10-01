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
#include <linux/moduleparam.h>   // Module parameter support
#include <linux/types.h>         // Basic kernel types
#include "dm-remap-core.h"       // Core data structures and debug macros
#include "dm-remap-messages.h"   // Debug macros and messaging support
#include "dm-remap-sysfs.h"      // Sysfs interface declarations
#include <linux/device-mapper.h> // Device mapper framework
#include <linux/bio.h>           // Block I/O structures
#include <linux/init.h>          // Module initialization
#include <linux/kernel.h>        // Kernel utilities  
#include <linux/vmalloc.h>       // Virtual memory allocation
#include <linux/string.h>        // String functions
#include <linux/time64.h>        // Time functions for v2.0 timestamping
#include "dm-remap-io.h"         // I/O processing functions
#include "dm-remap-error.h"      // Error handling functions

/*
 * Module parameters - configurable via modprobe or /sys/module/
 */
int debug_level = 1;             // Debug verbosity: 0=quiet, 1=info, 2=debug
int max_remaps = 1000;           // Maximum remappable sectors per target
int error_threshold = 3;         // Default error threshold for auto-remap
int auto_remap_enabled = 0;      // Enable automatic remapping (disabled by default)
unsigned int global_write_errors = 0;     // Global write error counter for testing
unsigned int global_read_errors = 0;      // Global read error counter for testing  
unsigned int global_auto_remaps = 0;      // Global auto-remap counter for testing

/* Module parameter registration */
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug verbosity level (0=quiet, 1=info, 2=debug)");

// Maximum number of sectors that can be remapped per target instance
module_param(max_remaps, int, 0644);
MODULE_PARM_DESC(max_remaps, "Maximum number of remappable sectors per target");

// Error detection and auto-remap parameters
module_param(error_threshold, int, 0644);
MODULE_PARM_DESC(error_threshold, "Number of errors before auto-remap is triggered");

module_param(auto_remap_enabled, int, 0644);
MODULE_PARM_DESC(auto_remap_enabled, "Enable automatic remapping on errors (0=disabled, 1=enabled)");

module_param(global_write_errors, uint, 0444);
MODULE_PARM_DESC(global_write_errors, "Total write errors detected (read-only)");

module_param(global_read_errors, uint, 0444);
MODULE_PARM_DESC(global_read_errors, "Total read errors detected (read-only)");

module_param(global_auto_remaps, uint, 0444);
MODULE_PARM_DESC(global_auto_remaps, "Total automatic remaps performed (read-only)");

/* 
 * EXPORTED VARIABLES 
 * These are accessible from other source files in the dm-remap module
 */
// DMR_DEBUG is now defined in dm-remap-core.h

// DEVICE MAPPER TARGET FUNCTIONS
// These are called by the device mapper framework to manage our target.
// Each function handles a specific aspect of target lifecycle and operation.

// remap_map is now in dm-remap-io.c as dmr_enhanced_map()
// Message handling is now in dm-remap-messages.c

// Reports status via dmsetup status
static void remap_status(struct dm_target *ti, status_type_t type,
                         // remap_status: Reports status via dmsetup status.
                         // Shows number of remapped sectors, lost sectors, and spare usage.
                         // This function is used by dmsetup to report target status.
                         unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i, remapped = 0;

    // Count remapped and lost sectors
    for (i = 0; i < rc->spare_used; i++) {
        if (rc->table[i].main_lba != (sector_t)-1) {
            remapped++;
        } else {
            lost++;
        }
    }

    switch (type) {
    case STATUSTYPE_INFO:
        // Format: remapped/total_spare lost/total_spare used/total_spare
        scnprintf(result, maxlen, 
                 "v2.0 %d/%llu %d/%llu %llu/%llu health=%u errors=W%u:R%u auto_remaps=%u manual_remaps=%u scan=%u%%",
                 remapped, (unsigned long long)rc->spare_len,
                 lost, (unsigned long long)rc->spare_len, 
                 (unsigned long long)rc->spare_used, (unsigned long long)rc->spare_len,
                 rc->overall_health,
                 rc->write_errors, rc->read_errors,
                 rc->auto_remaps, rc->manual_remaps,
                 rc->scan_progress);
        break;
        
    case STATUSTYPE_TABLE:
        // Format: <main_dev> <spare_dev> <spare_start> <spare_len>
        scnprintf(result, maxlen, "%s %s %llu %llu", 
                 rc->main_dev->name, rc->spare_dev->name,
                 (unsigned long long)rc->spare_start, (unsigned long long)rc->spare_len);
        break;
        
    default:
        scnprintf(result, maxlen, "unknown status type %u", type);
        break;
    }
}

// Parses target construction arguments and initializes the target
static int remap_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct remap_c *rc;
    unsigned long long tmp;
    int ret;
    sector_t dev_size;
    char target_name[256];

    pr_info("dm-remap: v2.0 Constructor called with %u args\n", argc);

    /* Validate argument count */
    if (argc != 4) {
        ti->error = "Invalid argument count, need: <main_dev> <spare_dev> <spare_start> <spare_len>";
        return -EINVAL;
    }

    /* Allocate context structure */
    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc) {
        ti->error = "Cannot allocate remap context";
        return -ENOMEM;
    }

    /* Get main device */
    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &rc->main_dev);
    if (ret) {
        ti->error = "Main device lookup failed";
        goto bad;
    }

    /* Get spare device */  
    ret = dm_get_device(ti, argv[1], dm_table_get_mode(ti->table), &rc->spare_dev);
    if (ret) {
        ti->error = "Spare device lookup failed";
        goto bad;
    }

    /* Parse spare start sector */
    ret = kstrtoull(argv[2], 10, &tmp);
    if (ret) {
        ti->error = "Invalid spare start sector";
        goto bad;
    }
    rc->spare_start = tmp;

    /* Parse spare length */
    ret = kstrtoull(argv[3], 10, &tmp);
    if (ret) {
        ti->error = "Invalid spare length";
        goto bad;
    }
    rc->spare_len = tmp;

    /* Initialize spare usage tracking */
    rc->spare_used = 0;
    rc->health_entries = 0;

    /* Initialize v2.0 fields */
    rc->write_errors = 0;
    rc->read_errors = 0;
    rc->auto_remaps = 0;
    rc->manual_remaps = 0;
    rc->scan_progress = 0;
    rc->last_scan_time = 0;
    rc->overall_health = DMR_HEALTH_GOOD;
    rc->auto_remap_enabled = auto_remap_enabled;  /* Use global parameter */
    rc->background_scan = false;
    rc->error_threshold = error_threshold;  /* Use global parameter */

    /* Enforce module parameter limits */
    if (rc->spare_len > max_remaps) {
        DMR_DEBUG(0, "Limiting spare_len from %llu to %d (max_remaps parameter)", 
                  (unsigned long long)rc->spare_len, max_remaps);
        rc->spare_len = max_remaps;
    }

    /* Validate spare area fits in spare device */
    dev_size = bdev_nr_sectors(rc->spare_dev->bdev);
    if (rc->spare_start + rc->spare_len > dev_size) {
        ti->error = "Spare area exceeds device size";
        goto bad;
    }

    DMR_DEBUG(0, "Constructor: main_dev=%s, spare_dev=%s, spare_start=%llu, spare_len=%llu", 
              rc->main_dev->name, rc->spare_dev->name,
              (unsigned long long)rc->spare_start, (unsigned long long)rc->spare_len);

    /* Allocate remap table */
    rc->table = kcalloc(rc->spare_len, sizeof(struct remap_entry), GFP_KERNEL);
    if (!rc->table) {
        ti->error = "Cannot allocate remap table";
        goto bad;
    }

    /* Set up target */
    ti->private = rc;
    ti->num_flush_bios = 1;      /* Forward flush requests */
    ti->num_discard_bios = 1;    /* Forward discard requests */

    /* Initialize v2.0 I/O processing subsystem */
    ret = dmr_io_init();
    if (ret) {
        ti->error = "Failed to initialize I/O subsystem";
        goto bad;
    }

    /* Create sysfs interface for this target */
    snprintf(target_name, sizeof(target_name), "%s", dm_table_device_name(ti->table));
    ret = dmr_sysfs_create_target(rc, target_name);
    if (ret) {
        DMR_DEBUG(0, "Failed to create sysfs interface for target: %d", ret);
        /* Continue without sysfs - not a fatal error */
    }

    pr_info("dm-remap: v2.0 target created successfully\n");
    return 0;

bad:
    if (rc->table)
        kfree(rc->table);
    if (rc->spare_dev)
        dm_put_device(ti, rc->spare_dev);
    if (rc->main_dev)
        dm_put_device(ti, rc->main_dev);
    kfree(rc);
    return -EINVAL;
}

// Destructor - cleans up resources when target is removed
static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;

    pr_info("dm-remap: v2.0 Destructor called\n");

    /* Remove sysfs interface */
    dmr_sysfs_remove_target(rc);

    /* Free remap table */
    if (rc->table) {
        kfree(rc->table);
        pr_info("dm-remap: freed remap table\n");
    }

    /* Release devices */
    if (rc->spare_dev) {
        dm_put_device(ti, rc->spare_dev);
        pr_info("dm-remap: released spare device\n");
    }
    
    if (rc->main_dev) {
        dm_put_device(ti, rc->main_dev);
        pr_info("dm-remap: released main device\n");
    }

    /* Cleanup v2.0 I/O processing subsystem */
    dmr_io_exit();
    
    /* Free context structure */
    kfree(rc);
    pr_info("dm-remap: freed remap_c struct\n");
}

// Device mapper target structure - defines our target interface
static struct target_type remap_target = {
    .name            = "remap",
    .version         = {2, 0, 0},
    .features        = DM_TARGET_PASSES_INTEGRITY,
    .module          = THIS_MODULE,
    .ctr             = remap_ctr,
    .dtr             = remap_dtr,
    .map             = remap_map,           /* Main I/O mapping function */
    .status          = remap_status,
    .message         = remap_message,       /* From dm-remap-messages.c */
};

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