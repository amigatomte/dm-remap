/**
 * dm-remap-v4-minimal.c - Minimal v4.0 Core for Build Testing
 * 
 * This is a simplified version of the v4.0 core that demonstrates
 * the clean slate architecture and compiles successfully.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/atomic.h>
#include <linux/slab.h>

#include "dm-remap-v4-compat.h"

/* Module metadata */
MODULE_DESCRIPTION("Device Mapper Remapping Target v4.0 - Minimal Build Demo");
MODULE_AUTHOR("dm-remap Development Team");
MODULE_LICENSE("GPL");
MODULE_VERSION("4.0.0-minimal");

/* Module parameters */
int dm_remap_debug = 1;
module_param(dm_remap_debug, int, 0644);
MODULE_PARM_DESC(dm_remap_debug, "Debug level (0=off, 1=info, 2=verbose)");

/* Simple device structure for v4.0 demo */
struct dm_remap_device_v4_minimal {
    struct block_device *main_dev;
    struct block_device *spare_dev;
    atomic64_t read_count;
    atomic64_t write_count;
    atomic64_t remap_count;
};

/* Global statistics */
static atomic64_t global_reads = ATOMIC64_INIT(0);
static atomic64_t global_writes = ATOMIC64_INIT(0);
static atomic64_t global_remaps = ATOMIC64_INIT(0);

/**
 * dm_remap_map_v4_minimal() - Minimal I/O mapping function
 */
static int dm_remap_map_v4_minimal(struct dm_target *ti, struct bio *bio)
{
    struct dm_remap_device_v4_minimal *device = ti->private;
    bool is_read = bio_data_dir(bio) == READ;
    
    /* Update statistics */
    if (is_read) {
        atomic64_inc(&device->read_count);
        atomic64_inc(&global_reads);
    } else {
        atomic64_inc(&device->write_count);
        atomic64_inc(&global_writes);
    }
    
    DMR_DEBUG(2, "v4.0 minimal I/O: %s to sector %llu (demo mode - no actual I/O)",
              is_read ? "read" : "write", 
              (unsigned long long)bio->bi_iter.bi_sector);
    
    /* For this demo, just complete the bio with success */
    bio->bi_status = BLK_STS_OK;
    bio_endio(bio);
    
    return DM_MAPIO_SUBMITTED;
}

/**
 * dm_remap_ctr_v4_minimal() - Constructor for minimal v4.0 target
 */
static int dm_remap_ctr_v4_minimal(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct dm_remap_device_v4_minimal *device;
    
    if (argc != 2) {
        ti->error = "Invalid argument count: dm-remap-v4 <main_device> <spare_device>";
        return -EINVAL;
    }
    
    DMR_DEBUG(1, "Creating minimal v4.0 target: main=%s, spare=%s", argv[0], argv[1]);
    
    /* Allocate device structure */
    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device) {
        ti->error = "Cannot allocate device structure";
        return -ENOMEM;
    }
    
    /* For this minimal demo, we'll store NULL pointers and just log */
    /* Real implementation would open the devices properly */
    device->main_dev = NULL;  /* Would be opened device */
    device->spare_dev = NULL; /* Would be opened device */
    atomic64_set(&device->read_count, 0);
    atomic64_set(&device->write_count, 0);
    atomic64_set(&device->remap_count, 0);
    
    ti->private = device;
    
    DMR_DEBUG(1, "v4.0 minimal target created successfully (demo mode)");
    return 0;
}

/**
 * dm_remap_dtr_v4_minimal() - Destructor for minimal v4.0 target
 */
static void dm_remap_dtr_v4_minimal(struct dm_target *ti)
{
    struct dm_remap_device_v4_minimal *device = ti->private;
    
    if (device) {
        DMR_DEBUG(1, "Destroying minimal v4.0 target (demo mode)");
        
        /* In demo mode, no actual devices to close */
        
        kfree(device);
        
        DMR_DEBUG(1, "v4.0 minimal target destroyed");
    }
}

/**
 * dm_remap_status_v4_minimal() - Status reporting for minimal v4.0 target
 */
static void dm_remap_status_v4_minimal(struct dm_target *ti, status_type_t type,
                                      unsigned status_flags, char *result, unsigned maxlen)
{
    struct dm_remap_device_v4_minimal *device = ti->private;
    
    switch (type) {
    case STATUSTYPE_INFO:
        scnprintf(result, maxlen,
                 "v4.0-minimal reads:%llu writes:%llu remaps:%llu",
                 atomic64_read(&device->read_count),
                 atomic64_read(&device->write_count),
                 atomic64_read(&device->remap_count));
        break;
        
    case STATUSTYPE_TABLE:
        scnprintf(result, maxlen, "demo-main demo-spare");
        break;
        
    case STATUSTYPE_IMA:
        /* IMA status not supported */
        *result = '\0';
        break;
    }
}

/* Device mapper target structure */
static struct target_type dm_remap_target_v4_minimal = {
    .name    = "remap-v4-minimal",
    .version = {4, 0, 0},
    .module  = THIS_MODULE,
    .ctr     = dm_remap_ctr_v4_minimal,
    .dtr     = dm_remap_dtr_v4_minimal,
    .map     = dm_remap_map_v4_minimal,
    .status  = dm_remap_status_v4_minimal,
};

/**
 * Module initialization
 */
static int __init dm_remap_v4_minimal_init(void)
{
    int ret;
    
    printk(KERN_INFO "dm-remap v4.0 minimal: Clean Slate Architecture Demo\n");
    printk(KERN_INFO "dm-remap v4.0 minimal: Demonstrating v4.0 core concepts\n");
    
    /* Register device mapper target */
    ret = dm_register_target(&dm_remap_target_v4_minimal);
    if (ret) {
        printk(KERN_ERR "dm-remap v4.0 minimal: Failed to register target: %d\n", ret);
        return ret;
    }
    
    printk(KERN_INFO "dm-remap v4.0 minimal: Module loaded successfully\n");
    return 0;
}

/**
 * Module cleanup
 */
static void __exit dm_remap_v4_minimal_exit(void)
{
    printk(KERN_INFO "dm-remap v4.0 minimal: Module unloading...\n");
    
    /* Unregister target */
    dm_unregister_target(&dm_remap_target_v4_minimal);
    
    printk(KERN_INFO "dm-remap v4.0 minimal: Module unloaded. Global stats: reads=%llu, writes=%llu\n",
           atomic64_read(&global_reads), atomic64_read(&global_writes));
}

module_init(dm_remap_v4_minimal_init);
module_exit(dm_remap_v4_minimal_exit);