/*
 * dm-remap-main.c - Main module file for dm-remap device mapper target
 * 
 * This file contains the module initialization, target lifecycle management,
 * and device mapper framework integration for the dm-remap target.
 * 
 * The dm-remap target provides bad sector remapping functionality:
 * - Redirects I/O from bad sectors on main device to spare sectors
 * - Supports dynamic remapping via message interface
 * - Provides status reporting and debugging capabilities
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>         /* Core kernel module support */
#include <linux/init.h>           /* Module init/exit macros */
#include <linux/kernel.h>         /* Kernel utilities like printk */
#include <linux/slab.h>           /* Memory allocation (kmalloc, kfree) */
#include <linux/string.h>         /* String manipulation functions */
#include <linux/device-mapper.h>  /* Device mapper framework API */

#include "dm-remap-core.h"        /* Core data structures and constants */
#include "dm-remap-io.h"          /* I/O processing functions */
#include "dm-remap-messages.h"    /* Message handling functions */

/*
 * MODULE METADATA
 * 
 * This information is embedded in the compiled module and shown
 * by tools like modinfo.
 */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Device Mapper target for dynamic bad sector remapping");
MODULE_LICENSE("GPL");           /* Required for device mapper targets */
MODULE_VERSION(DMR_VERSION);

/*
 * MODULE PARAMETERS
 * 
 * These can be set when loading the module or changed at runtime via sysfs.
 * Example: insmod dm-remap.ko debug_level=2 max_remaps=512
 */
int debug_level = 0;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug verbosity level (0=quiet, 1=info, 2=debug)");

int max_remaps = 1024;
module_param(max_remaps, int, 0644);
MODULE_PARM_DESC(max_remaps, "Maximum number of remappable sectors per target");

/*
 * GLOBAL STATISTICS
 * 
 * These counters track system-wide behavior across all dm-remap targets.
 * They're useful for monitoring and debugging.
 */
unsigned long dmr_clone_shallow_count = 0;
unsigned long dmr_clone_deep_count = 0;

/*
 * remap_ctr() - Target constructor
 * 
 * This function is called when a new dm-remap target is created via dmsetup.
 * It parses the command line arguments, allocates resources, and initializes
 * the target state.
 * 
 * Command line format:
 *   dmsetup create target-name --table "0 <size> remap <main_dev> <spare_dev> <spare_start> <spare_len>"
 * 
 * @ti: Device mapper target instance being created
 * @argc: Number of command line arguments (should be 4)
 * @argv: Array of command line argument strings
 * 
 * Returns: 0 on success, negative error code on failure
 */
static int remap_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
    struct remap_c *rc = NULL;           /* Our target context structure */
    unsigned long long spare_start = 0;  /* Parsed spare area start sector */
    unsigned long long spare_len = 0;    /* Parsed spare area length */
    blk_mode_t mode = FMODE_READ | FMODE_WRITE;  /* Device access mode */
    int ret, i;                          /* Return code and loop counter */

    /*
     * DEBUG LOGGING: Log constructor call
     * 
     * This helps debug target creation issues by showing exactly
     * what arguments were passed to the constructor.
     */
    printk(KERN_INFO "dm-remap: remap_ctr called, argc=%u\n", argc);
    for (i = 0; i < argc; i++)
        printk(KERN_INFO "dm-remap: argv[%d] = %s\n", i, argv[i]);

    /*
     * ARGUMENT VALIDATION
     * 
     * We expect exactly 4 arguments:
     * argv[0] = main device path (e.g., "/dev/sda1")
     * argv[1] = spare device path (e.g., "/dev/sdb1") 
     * argv[2] = spare area start sector (e.g., "0")
     * argv[3] = spare area length in sectors (e.g., "1024")
     */
    if (argc != 4) {
        ti->error = "Invalid argument count: expected 4 arguments";
        return -EINVAL;
    }

    /*
     * MEMORY ALLOCATION
     * 
     * Allocate our main context structure. This will hold all the
     * state for this target instance. Use GFP_KERNEL because we're
     * in a context where sleeping is allowed.
     */
    rc = kzalloc(sizeof(*rc), GFP_KERNEL);
    if (!rc) {
        ti->error = "Failed to allocate target context";
        return -ENOMEM;
    }

    /*
     * DEVICE ACQUISITION
     * 
     * Get references to the main and spare block devices.
     * The device mapper framework manages the device lifetimes for us.
     */
    
    /* Get main device (where bad sectors occur) */
    ret = dm_get_device(ti, argv[0], mode, &rc->main_dev);
    if (ret) {
        ti->error = "Failed to get main device";
        kfree(rc);
        return ret;
    }

    /* Get spare device (where remapped sectors go) */
    ret = dm_get_device(ti, argv[1], mode, &rc->spare_dev);
    if (ret) {
        ti->error = "Failed to get spare device";
        dm_put_device(ti, rc->main_dev);  /* Clean up main device */
        kfree(rc);
        return ret;
    }

    /*
     * ARGUMENT PARSING
     * 
     * Parse the spare area configuration from the command line.
     * kstrtoull() safely converts strings to unsigned long long.
     */
    
    /* Parse spare area start sector */
    ret = kstrtoull(argv[2], 10, &spare_start);
    if (ret) {
        ti->error = "Invalid spare_start sector number";
        goto bad;
    }

    /* Parse spare area length */
    ret = kstrtoull(argv[3], 10, &spare_len);
    if (ret) {
        ti->error = "Invalid spare_len sector count";
        goto bad;
    }

    /*
     * CONFIGURATION SETUP
     * 
     * Initialize the target configuration with parsed values.
     */
    rc->spare_start = (sector_t)spare_start;
    rc->spare_len = (sector_t)spare_len;
    rc->spare_used = 0;                  /* No sectors remapped initially */
    rc->main_start = 0;                  /* Main device starts at sector 0 */
    spin_lock_init(&rc->lock);           /* Initialize the spinlock */

    /*
     * APPLY MODULE PARAMETER LIMITS
     * 
     * The max_remaps parameter limits resource usage per target.
     * This prevents a single target from consuming too much memory.
     */
    if (rc->spare_len > max_remaps) {
        DMR_DEBUG(0, "Limiting spare_len from %llu to %d (max_remaps parameter)", 
                  (unsigned long long)rc->spare_len, max_remaps);
        rc->spare_len = max_remaps;
    }

    /*
     * CONFIGURATION VALIDATION
     * 
     * Sanity check the configuration before proceeding.
     */
    if (!rc->spare_dev || rc->spare_len == 0) {
        ti->error = "Spare device missing or spare area length is zero";
        goto bad;
    }
    
    /* Log the final configuration */
    DMR_DEBUG(0, "Constructor: main_dev=%s, spare_dev=%s, spare_start=%llu, spare_len=%llu", 
              argv[0], argv[1], (unsigned long long)spare_start, 
              (unsigned long long)rc->spare_len);

    /*
     * REMAP TABLE ALLOCATION
     * 
     * Allocate memory for the remap table. This is an array with one
     * entry for each spare sector. Use kcalloc() to get zero-filled memory.
     */
    rc->table = kcalloc(rc->spare_len, sizeof(struct remap_entry), GFP_KERNEL);
    if (!rc->table) {
        ti->error = "Failed to allocate remap table";
        goto bad;
    }

    /*
     * REMAP TABLE INITIALIZATION
     * 
     * Pre-calculate spare sector numbers for each table entry.
     * This makes remapping faster since we don't need to calculate
     * spare sectors during I/O operations.
     */
    for (i = 0; i < rc->spare_len; i++) {
        rc->table[i].main_lba = (sector_t)-1;           /* -1 = unused entry */
        rc->table[i].spare_lba = rc->spare_start + i;   /* Pre-calculated spare sector */
    }

    /*
     * DEVICE MAPPER INTEGRATION
     * 
     * Store our context in the target and set up per-bio data size.
     * The device mapper framework will call our functions with this context.
     */
    ti->private = rc;
    ti->per_io_data_size = sizeof(struct remap_io_ctx);

    printk(KERN_INFO "dm-remap: target created successfully\n");
    return 0;

bad:
    /*
     * ERROR CLEANUP
     * 
     * If anything went wrong, we need to clean up all the resources
     * we've allocated so far. This prevents memory and device leaks.
     */
    if (rc->table)
        kfree(rc->table);
    if (rc->main_dev)
        dm_put_device(ti, rc->main_dev);
    if (rc->spare_dev)
        dm_put_device(ti, rc->spare_dev);
    kfree(rc);
    return ret;
}

/*
 * remap_dtr() - Target destructor
 * 
 * This function is called when a dm-remap target is being destroyed
 * (via dmsetup remove). It releases all resources allocated by the constructor.
 * 
 * @ti: Device mapper target instance being destroyed
 */
static void remap_dtr(struct dm_target *ti)
{
    struct remap_c *rc = ti->private;

    printk(KERN_INFO "dm-remap: remap_dtr called, starting cleanup\n");

    /*
     * RESOURCE CLEANUP
     * 
     * Release all resources in reverse order of allocation.
     * This ensures we don't leave any dangling references.
     */
    
    if (rc) {
        /* Free the remap table memory */
        if (rc->table) {
            kfree(rc->table);
            printk(KERN_INFO "dm-remap: freed remap table\n");
        }

        /* Release device references */
        if (rc->main_dev) {
            dm_put_device(ti, rc->main_dev);
            printk(KERN_INFO "dm-remap: released main device\n");
        }
        
        if (rc->spare_dev) {
            dm_put_device(ti, rc->spare_dev);
            printk(KERN_INFO "dm-remap: released spare device\n");
        }

        /* Free the main context structure */
        kfree(rc);
        printk(KERN_INFO "dm-remap: freed target context\n");
    }
}

/*
 * remap_status() - Target status reporting
 * 
 * This function is called when someone runs "dmsetup status" on our target.
 * It generates a human-readable status string showing target statistics.
 * 
 * @ti: Device mapper target instance
 * @type: Type of status requested (STATUSTYPE_INFO or STATUSTYPE_TABLE)
 * @status_flags: Additional flags (unused currently)
 * @result: Buffer to write status string
 * @maxlen: Size of result buffer
 */
static void remap_status(struct dm_target *ti, status_type_t type,
                        unsigned status_flags, char *result, unsigned maxlen)
{
    struct remap_c *rc = ti->private;
    int lost = 0, i, remapped = 0;

    /*
     * STATISTICS CALCULATION
     * 
     * Count how many sectors are remapped vs lost.
     * - Remapped: entries with valid main_lba (not -1)
     * - Lost: entries that were remapped but later marked as lost
     * 
     * Currently we don't have "lost" sector tracking, so lost=0.
     * This will be implemented in v2 with error handling.
     */
    for (i = 0; i < rc->spare_used; i++) {
        if (rc->table[i].main_lba != (sector_t)-1)
            remapped++;
        else
            lost++;  /* This case shouldn't happen in v1 */
    }

    /*
     * STATUS FORMAT SELECTION
     * 
     * STATUSTYPE_INFO: Human-readable statistics
     * STATUSTYPE_TABLE: Constructor arguments (for target reconstruction)
     */
    if (type == STATUSTYPE_INFO) {
        /*
         * Generate human-readable status
         * 
         * Format: "remapped=N lost=N spare_used=N/M (X%)"
         * - N = current count
         * - M = maximum possible
         * - X% = percentage utilization
         */
        scnprintf(result, maxlen, "remapped=%d lost=%d spare_used=%llu/%llu (%d%%)",
                 remapped, lost, (unsigned long long)rc->spare_used,
                 (unsigned long long)rc->spare_len,
                 rc->spare_len ? (int)((rc->spare_used * 100) / rc->spare_len) : 0);
    } else {
        /*
         * Generate table format (constructor arguments)
         * 
         * This format can be used to recreate the target with the same
         * configuration. It's used by dmsetup for target migration.
         */
        scnprintf(result, maxlen, "%s %s %llu %llu",
                 rc->main_dev->name, rc->spare_dev->name,
                 (unsigned long long)rc->spare_start,
                 (unsigned long long)rc->spare_len);
    }
}

/*
 * DEVICE MAPPER TARGET OPERATIONS
 * 
 * This structure defines the interface between our target and the
 * device mapper framework. Each field points to one of our functions.
 */
static struct target_type remap_target = {
    .name    = "remap",           /* Target type name (used in dmsetup commands) */
    .version = {1, 1, 0},        /* Version number: major.minor.patch */
    .module  = THIS_MODULE,       /* Kernel module that owns this target */
    .ctr     = remap_ctr,        /* Constructor function */
    .dtr     = remap_dtr,        /* Destructor function */
    .map     = remap_map,        /* I/O processing function */
    .status  = remap_status,     /* Status reporting function */
    .message = remap_message,    /* Message handling function */
};

/*
 * remap_init() - Module initialization
 * 
 * This function is called when the module is loaded (via insmod).
 * It registers our target type with the device mapper framework.
 * 
 * Returns: 0 on success, negative error code on failure
 */
static int __init remap_init(void)
{
    int ret;

    /*
     * REGISTER TARGET TYPE
     * 
     * Tell the device mapper framework about our target type.
     * After this succeeds, users can create targets of our type.
     */
    ret = dm_register_target(&remap_target);
    if (ret < 0) {
        printk(KERN_ERR "dm-remap: failed to register target type\n");
        return ret;
    }

    printk(KERN_INFO "dm-remap: module loaded (version " DMR_VERSION ")\n");
    printk(KERN_INFO "dm-remap: debug_level=%d, max_remaps=%d\n", 
           debug_level, max_remaps);
    
    return 0;
}

/*
 * remap_exit() - Module cleanup
 * 
 * This function is called when the module is unloaded (via rmmod).
 * It unregisters our target type from the device mapper framework.
 */
static void __exit remap_exit(void)
{
    /*
     * UNREGISTER TARGET TYPE
     * 
     * Remove our target type from the device mapper framework.
     * After this, no new targets of our type can be created.
     * 
     * Note: This will fail if any targets of our type still exist.
     * Users must remove all targets before unloading the module.
     */
    dm_unregister_target(&remap_target);
    
    printk(KERN_INFO "dm-remap: module unloaded\n");
}

/*
 * KERNEL MODULE REGISTRATION
 * 
 * These macros tell the kernel which functions to call for
 * module loading and unloading.
 */
module_init(remap_init);
module_exit(remap_exit);

/*
 * DESIGN NOTES:
 * 
 * 1. ERROR HANDLING:
 *    All functions use proper error handling with cleanup paths.
 *    Resources are released in reverse order of allocation.
 * 
 * 2. MEMORY MANAGEMENT:
 *    We use standard kernel memory allocation (kzalloc/kfree).
 *    All allocations are checked for failure.
 * 
 * 3. DEVICE MANAGEMENT:
 *    Device references are managed by the device mapper framework.
 *    We just acquire and release them properly.
 * 
 * 4. THREAD SAFETY:
 *    The constructor and destructor are called with appropriate
 *    locking by the device mapper framework. We don't need additional
 *    synchronization for target lifecycle operations.
 * 
 * 5. PARAMETER VALIDATION:
 *    All user inputs are validated before use. Invalid inputs
 *    result in clear error messages.
 */