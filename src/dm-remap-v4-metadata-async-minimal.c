/**
 * Minimal async metadata write - trying to avoid kernel crash
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>

#include "dm-remap-v4.h"

/* Simple test function with NO local variables */
int dm_remap_write_metadata_v4_async_test(struct block_device *bdev,
                                          struct dm_remap_metadata_v4 *metadata,
                                          struct dm_remap_async_metadata_context *context)
{
	if (!bdev || !metadata || !context)
		return -EINVAL;
	
	printk(KERN_CRIT "ASYNC TEST: Function entered successfully!\n");
	
	return -ENOSYS; /* Not implemented - just testing if we can enter */
}
EXPORT_SYMBOL(dm_remap_write_metadata_v4_async_test);
