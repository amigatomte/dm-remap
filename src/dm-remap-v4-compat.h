/**
 * dm-remap-v4-compat.h - Kernel API Compatibility Layer
 * 
 * This header provides compatibility functions for different kernel versions
 * to make v4.0 code compile across kernel versions.
 */

#ifndef DM_REMAP_V4_COMPAT_H
#define DM_REMAP_V4_COMPAT_H

#include <linux/version.h>
#include <linux/blkdev.h>

/* Compatibility wrapper for bdev_name() */
static inline const char *dm_remap_bdev_name(struct block_device *bdev)
{
    if (bdev && bdev->bd_disk) {
        return bdev->bd_disk->disk_name;
    }
    return "unknown";
}

/* Compatibility wrapper for device opening */
static inline struct block_device *dm_remap_open_bdev(const char *path, fmode_t mode, void *holder)
{
    /* For this kernel version, lookup_bdev takes different parameters */
    dev_t dev;
    int ret;
    
    /* Try to resolve the device path to a dev_t */
    ret = lookup_bdev(path, &dev);
    if (ret) {
        return ERR_PTR(ret);
    }
    
    /* For now, just return an error indicating we need a different approach */
    /* In a real implementation, we'd use blkdev_get_by_dev or similar */
    return ERR_PTR(-ENODEV);
}

/* Compatibility wrapper for device closing */
static inline void dm_remap_close_bdev(struct block_device *bdev, fmode_t mode)
{
    /* For lookup_bdev, we don't need to explicitly close */
    /* The bdev reference is managed by the kernel */
}

/* Missing constants that might not be defined */
#ifndef DM_REMAP_MIN_DEVICE_SECTORS
#define DM_REMAP_MIN_DEVICE_SECTORS 2048
#endif

/* Debug macro compatibility */
#ifndef DMR_DEBUG
/* Debug macros */
#define DMR_DEBUG(level, fmt, args...) \
    do { \
        if (dm_remap_debug >= (level)) { \
            printk(KERN_INFO "dm-remap v4.0: " fmt "\n", ##args); \
        } \
    } while (0)
#endif

/* Declare external debug variable */
extern int dm_remap_debug;

#endif /* DM_REMAP_V4_COMPAT_H */