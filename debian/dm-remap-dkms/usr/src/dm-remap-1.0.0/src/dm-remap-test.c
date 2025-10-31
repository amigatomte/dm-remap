/**
 * dm-remap-test.c - Minimal dm-remap test module
 * 
 * A minimal Device Mapper target for testing and validation purposes.
 * 
 * Copyright (C) 2025 dm-remap Development Team
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>

MODULE_DESCRIPTION("dm-remap minimal test module");
MODULE_AUTHOR("dm-remap Development Team");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

/* Minimal device structure - just one field */
struct minimal_device {
    struct dm_dev *dev;
};

/**
 * Minimal constructor - just open device and allocate structure
 */
static int minimal_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct minimal_device *md;
    int ret;
    
    printk(KERN_INFO "minimal-test: Constructor called\n");
    
    if (argc != 1) {
        ti->error = "Invalid argument count";
        return -EINVAL;
    }
    
    md = kzalloc(sizeof(*md), GFP_KERNEL);
    if (!md) {
        ti->error = "Cannot allocate context";
        return -ENOMEM;
    }
    
    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &md->dev);
    if (ret) {
        ti->error = "Device lookup failed";
        kfree(md);
        return ret;
    }
    
    ti->private = md;
    
    printk(KERN_INFO "minimal-test: Constructor complete\n");
    return 0;
}

/**
 * Minimal destructor - just close device and free structure
 */
static void minimal_dtr(struct dm_target *ti)
{
    struct minimal_device *md = ti->private;
    
    printk(KERN_INFO "minimal-test: Destructor called\n");
    
    if (md) {
        if (md->dev)
            dm_put_device(ti, md->dev);
        kfree(md);
    }
    
    printk(KERN_INFO "minimal-test: Destructor complete\n");
}

/**
 * Minimal map - just pass through to underlying device
 */
static int minimal_map(struct dm_target *ti, struct bio *bio)
{
    struct minimal_device *md = ti->private;
    
    /* Simple pass-through - redirect to underlying device */
    bio_set_dev(bio, md->dev->bdev);
    
    return DM_MAPIO_REMAPPED;
}

/**
 * Minimal status - just report we exist
 */
static void minimal_status(struct dm_target *ti, status_type_t type,
                          unsigned status_flags, char *result, unsigned maxlen)
{
    struct minimal_device *md = ti->private;
    
    switch (type) {
    case STATUSTYPE_INFO:
        scnprintf(result, maxlen, "minimal");
        break;
    case STATUSTYPE_TABLE:
        scnprintf(result, maxlen, "%s", md->dev->name);
        break;
    case STATUSTYPE_IMA:
        *result = '\0';
        break;
    }
}

/* Target structure - MINIMAL fields only */
static struct target_type minimal_target = {
    .name = "remap-test",
    .version = {1, 0, 0},
    .module = THIS_MODULE,
    .ctr = minimal_ctr,
    .dtr = minimal_dtr,
    .map = minimal_map,
    .status = minimal_status,
};

/**
 * Module init - just register target
 */
static int __init minimal_init(void)
{
    int ret;
    
    printk(KERN_INFO "minimal-test: Loading module\n");
    
    ret = dm_register_target(&minimal_target);
    if (ret < 0) {
        printk(KERN_ERR "minimal-test: Failed to register target: %d\n", ret);
        return ret;
    }
    
    printk(KERN_INFO "minimal-test: Module loaded successfully\n");
    return 0;
}

/**
 * Module exit - just unregister target
 */
static void __exit minimal_exit(void)
{
    printk(KERN_INFO "minimal-test: Unloading module\n");
    dm_unregister_target(&minimal_target);
    printk(KERN_INFO "minimal-test: Module unloaded\n");
}

module_init(minimal_init);
module_exit(minimal_exit);
