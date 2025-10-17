/*
 * dm-bio-error - Simple device-mapper target for bio error injection
 *
 * This module provides a device-mapper target that can inject bio errors
 * at specific sectors without the hanging issues of dm-flakey's error_reads.
 *
 * Key differences from dm-flakey:
 *  • Completes bios asynchronously (via bio_endio) instead of returning errors
 *  • Allows precise sector-level control
 *  • Doesn't cause kernel hangs on mount/direct I/O
 *
 * Usage:
 *   echo "0 <size> bio-error <dev> <error_start> <error_end>" | dmsetup create test
 *
 * Parameters:
 *   <dev>          - Underlying block device
 *   <error_start>  - First sector that should return errors
 *   <error_end>    - Last sector that should return errors (inclusive)
 *
 * Example:
 *   # Create a device where sectors 100-199 return I/O errors
 *   echo "0 204800 bio-error /dev/loop0 100 199" | dmsetup create error_test
 *
 * Copyright (C) 2025 dm-remap project
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#define DM_MSG_PREFIX "bio-error"

struct bio_error_c {
	struct dm_dev *dev;
	sector_t error_start;
	sector_t error_end;
	sector_t start;
};

/*
 * Construct a bio-error mapping
 */
static int bio_error_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct bio_error_c *bc;
	unsigned long long tmp;
	char dummy;
	int ret;

	if (argc != 3) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	bc = kzalloc(sizeof(*bc), GFP_KERNEL);
	if (!bc) {
		ti->error = "Cannot allocate context";
		return -ENOMEM;
	}

	/* Get underlying device */
	ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &bc->dev);
	if (ret) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	/* Parse error_start */
	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1 || tmp != (sector_t)tmp) {
		ti->error = "Invalid error_start";
		ret = -EINVAL;
		goto bad;
	}
	bc->error_start = tmp;

	/* Parse error_end */
	if (sscanf(argv[2], "%llu%c", &tmp, &dummy) != 1 || tmp != (sector_t)tmp) {
		ti->error = "Invalid error_end";
		ret = -EINVAL;
		goto bad;
	}
	bc->error_end = tmp;

	if (bc->error_end < bc->error_start) {
		ti->error = "error_end must be >= error_start";
		ret = -EINVAL;
		goto bad;
	}

	bc->start = ti->begin;
	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->per_io_data_size = 0;
	ti->private = bc;

	return 0;

bad:
	if (bc->dev)
		dm_put_device(ti, bc->dev);
	kfree(bc);
	return ret;
}

/*
 * Destroy a bio-error mapping
 */
static void bio_error_dtr(struct dm_target *ti)
{
	struct bio_error_c *bc = ti->private;

	dm_put_device(ti, bc->dev);
	kfree(bc);
}

/*
 * Check if this bio should get an error
 */
static bool should_inject_error(struct bio_error_c *bc, struct bio *bio)
{
	sector_t bio_start = bio->bi_iter.bi_sector;
	sector_t bio_end = bio_start + bio_sectors(bio) - 1;

	/* Check if bio overlaps with error range */
	return (bio_start <= bc->error_end && bio_end >= bc->error_start);
}

/*
 * Handle bio completion - this is called when the underlying device completes the I/O
 */
static void bio_error_endio(struct bio *bio)
{
	struct bio *original_bio = bio->bi_private;
	
	/* Complete the original bio with whatever status the clone had */
	bio_endio(original_bio);
	bio_put(bio);
}

/*
 * Main bio mapping function
 */
static int bio_error_map(struct dm_target *ti, struct bio *bio)
{
	struct bio_error_c *bc = ti->private;
	struct bio *clone;

	/* Check if we should inject an error for this bio */
	if (should_inject_error(bc, bio)) {
		pr_info("dm-bio-error: Injecting error for sector %llu\n",
			(unsigned long long)bio->bi_iter.bi_sector);
		
		/* Return error asynchronously by completing with error status */
		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
		return DM_MAPIO_SUBMITTED;
	}

	/* Not in error range - pass through to underlying device */
	clone = bio_alloc_clone(bc->dev->bdev, bio, GFP_NOIO, &fs_bio_set);
	clone->bi_private = bio;
	clone->bi_end_io = bio_error_endio;
	
	submit_bio_noacct(clone);
	return DM_MAPIO_SUBMITTED;
}

/*
 * Status function - report configuration
 */
static void bio_error_status(struct dm_target *ti, status_type_t type,
			      unsigned int status_flags, char *result,
			      unsigned int maxlen)
{
	struct bio_error_c *bc = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		snprintf(result, maxlen, "error_range=%llu-%llu",
			 (unsigned long long)bc->error_start,
			 (unsigned long long)bc->error_end);
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s %llu %llu",
			 bc->dev->name,
			 (unsigned long long)bc->error_start,
			 (unsigned long long)bc->error_end);
		break;
	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}
}

static int bio_error_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct bio_error_c *bc = ti->private;
	struct dm_dev *dev = bc->dev;

	*bdev = dev->bdev;

	/* Only pass ioctls through if the target is ready */
	return 0;
}

static struct target_type bio_error_target = {
	.name = "bio-error",
	.version = {1, 0, 0},
	.features = DM_TARGET_PASSES_INTEGRITY,
	.module = THIS_MODULE,
	.ctr = bio_error_ctr,
	.dtr = bio_error_dtr,
	.map = bio_error_map,
	.status = bio_error_status,
	.prepare_ioctl = bio_error_prepare_ioctl,
};

static int __init dm_bio_error_init(void)
{
	int r = dm_register_target(&bio_error_target);

	if (r < 0)
		DMERR("register failed %d", r);
	else
		DMINFO("version 1.0.0 loaded");

	return r;
}

static void __exit dm_bio_error_exit(void)
{
	dm_unregister_target(&bio_error_target);
	DMINFO("unloaded");
}

module_init(dm_bio_error_init);
module_exit(dm_bio_error_exit);

MODULE_AUTHOR("dm-remap project");
MODULE_DESCRIPTION("Device-mapper target for bio error injection");
MODULE_LICENSE("GPL");
