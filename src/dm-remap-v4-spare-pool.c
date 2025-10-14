// SPDX-License-Identifier: GPL-2.0-only
/*
 * dm-remap-v4-spare-pool.c - External spare device management
 *
 * Copyright (C) 2025 dm-remap development team
 *
 * This module provides simple, manual spare device management for dm-remap.
 * It allows administrators to add external block devices to provide additional
 * remapping capacity when internal drive spare sectors are exhausted.
 *
 * Design Philosophy:
 * - Keep it simple: No auto-expansion, no complex policies
 * - Manual control: Admin decides when/what to add
 * - Reliable: First-fit allocation, bitmap tracking
 * - Integrated: Works with health monitoring and setup reassembly
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include "../include/dm-remap-v4-spare-pool.h"

#define DM_MSG_PREFIX "dm-remap-spare-pool"

/*
 * Initialize spare pool
 */
int spare_pool_init(struct spare_pool *pool, struct dm_target *ti)
{
	if (!pool || !ti)
		return -EINVAL;
	
	memset(pool, 0, sizeof(*pool));
	
	INIT_LIST_HEAD(&pool->spares);
	spin_lock_init(&pool->spares_lock);
	
	pool->allocations = RB_ROOT;
	spin_lock_init(&pool->allocations_lock);
	
	atomic_set(&pool->allocation_count, 0);
	atomic_set(&pool->next_allocation_id, 1);
	atomic_set(&pool->spare_device_count, 0);
	atomic64_set(&pool->total_spare_capacity, 0);
	atomic64_set(&pool->allocated_spare_capacity, 0);
	atomic64_set(&pool->total_allocations_lifetime, 0);
	
	pool->allocation_unit = SPARE_ALLOCATION_UNIT_DEFAULT;
	pool->allow_partial_allocations = true;
	pool->ti = ti;
	
	DMINFO("Spare pool initialized (allocation_unit=%llu sectors)",
	       (unsigned long long)pool->allocation_unit);
	
	return 0;
}
EXPORT_SYMBOL(spare_pool_init);

/*
 * Clean up spare device
 */
static void spare_device_destroy(struct spare_device *spare)
{
	if (!spare)
		return;
	
	if (spare->bdev)
		blkdev_put(spare->bdev, FMODE_READ | FMODE_WRITE);
	
	kfree(spare->allocation_bitmap);
	kfree(spare->dev_path);
	kfree(spare);
}

/*
 * Cleanup spare pool
 */
void spare_pool_exit(struct spare_pool *pool)
{
	struct spare_device *spare, *tmp;
	unsigned long flags;
	
	if (!pool)
		return;
	
	/* Remove all spare devices */
	spin_lock_irqsave(&pool->spares_lock, flags);
	spare_for_each_device_safe(pool, spare, tmp) {
		list_del(&spare->list);
		spin_unlock_irqrestore(&pool->spares_lock, flags);
		
		if (atomic_read(&spare->refcount) > 0) {
			DMWARN("Spare device %s still has %d active allocations",
			       spare->dev_path, atomic_read(&spare->refcount));
		}
		
		spare_device_destroy(spare);
		
		spin_lock_irqsave(&pool->spares_lock, flags);
	}
	spin_unlock_irqrestore(&pool->spares_lock, flags);
	
	DMINFO("Spare pool cleaned up (%llu total allocations)",
	       atomic64_read(&pool->total_allocations_lifetime));
}
EXPORT_SYMBOL(spare_pool_exit);

/*
 * Add spare device to pool
 */
int spare_pool_add_device(struct spare_pool *pool, const char *dev_path)
{
	struct spare_device *spare;
	struct block_device *bdev;
	unsigned long flags;
	sector_t sectors;
	size_t bitmap_longs;
	int ret = 0;
	
	if (!pool || !dev_path)
		return -EINVAL;
	
	/* Open block device */
	bdev = blkdev_get_by_path(dev_path, FMODE_READ | FMODE_WRITE, pool);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		DMERR("Failed to open spare device %s: %d", dev_path, ret);
		return ret;
	}
	
	sectors = get_capacity(bdev->bd_disk);
	if (sectors == 0) {
		DMERR("Spare device %s has zero capacity", dev_path);
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return -EINVAL;
	}
	
	/* Allocate spare device structure */
	spare = kzalloc(sizeof(*spare), GFP_KERNEL);
	if (!spare) {
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return -ENOMEM;
	}
	
	spare->dev_path = kstrdup(dev_path, GFP_KERNEL);
	if (!spare->dev_path) {
		kfree(spare);
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return -ENOMEM;
	}
	
	/* Initialize allocation bitmap */
	bitmap_longs = BITS_TO_LONGS(sectors / pool->allocation_unit);
	spare->allocation_bitmap = kcalloc(bitmap_longs, sizeof(unsigned long),
					   GFP_KERNEL);
	if (!spare->allocation_bitmap) {
		kfree(spare->dev_path);
		kfree(spare);
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE);
		return -ENOMEM;
	}
	
	/* Initialize spare device */
	spare->bdev = bdev;
	spare->dev = bdev->bd_dev;
	spare->total_sectors = sectors;
	spare->allocated_sectors = 0;
	spare->free_sectors = sectors;
	spare->state = SPARE_STATE_AVAILABLE;
	spare->bitmap_size = bitmap_longs;
	spare->allocation_unit = pool->allocation_unit;
	spare->added_at = ktime_get();
	
	atomic_set(&spare->refcount, 0);
	atomic64_set(&spare->total_allocations, 0);
	atomic64_set(&spare->current_allocations, 0);
	spin_lock_init(&spare->lock);
	
	/* Add to pool */
	spin_lock_irqsave(&pool->spares_lock, flags);
	list_add_tail(&spare->list, &pool->spares);
	atomic_inc(&pool->spare_device_count);
	atomic64_add(sectors, &pool->total_spare_capacity);
	spin_unlock_irqrestore(&pool->spares_lock, flags);
	
	DMINFO("Added spare device %s (%llu MB, %llu sectors)",
	       dev_path,
	       (unsigned long long)(sectors >> 11), /* Sectors to MB */
	       (unsigned long long)sectors);
	
	return 0;
}
EXPORT_SYMBOL(spare_pool_add_device);

/*
 * Remove spare device from pool
 */
int spare_pool_remove_device(struct spare_pool *pool, const char *dev_path)
{
	struct spare_device *spare;
	unsigned long flags;
	bool found = false;
	
	if (!pool || !dev_path)
		return -EINVAL;
	
	spin_lock_irqsave(&pool->spares_lock, flags);
	spare_for_each_device(pool, spare) {
		if (strcmp(spare->dev_path, dev_path) == 0) {
			/* Check if spare is in use */
			if (atomic_read(&spare->refcount) > 0) {
				spin_unlock_irqrestore(&pool->spares_lock, flags);
				DMERR("Cannot remove spare %s: %d active allocations",
				      dev_path, atomic_read(&spare->refcount));
				return -EBUSY;
			}
			
			list_del(&spare->list);
			atomic_dec(&pool->spare_device_count);
			atomic64_sub(spare->total_sectors, &pool->total_spare_capacity);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&pool->spares_lock, flags);
	
	if (!found) {
		DMERR("Spare device %s not found in pool", dev_path);
		return -ENOENT;
	}
	
	DMINFO("Removed spare device %s (%llu total allocations)",
	       dev_path, atomic64_read(&spare->total_allocations));
	
	spare_device_destroy(spare);
	return 0;
}
EXPORT_SYMBOL(spare_pool_remove_device);

/*
 * Find first available sector in spare device using bitmap
 */
static sector_t spare_device_find_free_sector(struct spare_device *spare,
					       u32 sector_count)
{
	unsigned long bit, start_bit = 0;
	u32 required_bits;
	u32 consecutive = 0;
	sector_t start_sector = 0;
	
	required_bits = (sector_count + spare->allocation_unit - 1) / 
			spare->allocation_unit;
	
	/* Simple first-fit allocation */
	for (bit = 0; bit < spare->bitmap_size * BITS_PER_LONG; bit++) {
		if (!test_bit(bit, spare->allocation_bitmap)) {
			if (consecutive == 0)
				start_bit = bit;
			consecutive++;
			
			if (consecutive >= required_bits) {
				start_sector = start_bit * spare->allocation_unit;
				return start_sector;
			}
		} else {
			consecutive = 0;
		}
	}
	
	return (sector_t)-1; /* No space found */
}

/*
 * Mark sectors as allocated in bitmap
 */
static void spare_device_mark_allocated(struct spare_device *spare,
					sector_t start_sector,
					u32 sector_count)
{
	unsigned long start_bit = start_sector / spare->allocation_unit;
	u32 required_bits = (sector_count + spare->allocation_unit - 1) /
			    spare->allocation_unit;
	unsigned long bit;
	
	for (bit = start_bit; bit < start_bit + required_bits; bit++) {
		set_bit(bit, spare->allocation_bitmap);
	}
	
	spare->allocated_sectors += sector_count;
	spare->free_sectors -= sector_count;
	
	if (spare->free_sectors == 0)
		spare->state = SPARE_STATE_FULL;
	else if (spare->state == SPARE_STATE_AVAILABLE)
		spare->state = SPARE_STATE_IN_USE;
}

/*
 * Mark sectors as free in bitmap
 */
static void spare_device_mark_free(struct spare_device *spare,
				   sector_t start_sector,
				   u32 sector_count)
{
	unsigned long start_bit = start_sector / spare->allocation_unit;
	u32 required_bits = (sector_count + spare->allocation_unit - 1) /
			    spare->allocation_unit;
	unsigned long bit;
	
	for (bit = start_bit; bit < start_bit + required_bits; bit++) {
		clear_bit(bit, spare->allocation_bitmap);
	}
	
	spare->allocated_sectors -= sector_count;
	spare->free_sectors += sector_count;
	
	if (spare->state == SPARE_STATE_FULL)
		spare->state = SPARE_STATE_IN_USE;
	
	if (spare->allocated_sectors == 0)
		spare->state = SPARE_STATE_AVAILABLE;
}

/*
 * Insert allocation into red-black tree
 */
static void spare_allocation_insert(struct spare_pool *pool,
				    struct spare_allocation *alloc)
{
	struct rb_node **new = &pool->allocations.rb_node;
	struct rb_node *parent = NULL;
	struct spare_allocation *this;
	
	while (*new) {
		this = rb_entry(*new, struct spare_allocation, node);
		parent = *new;
		
		if (alloc->original_sector < this->original_sector)
			new = &(*new)->rb_left;
		else if (alloc->original_sector > this->original_sector)
			new = &(*new)->rb_right;
		else
			return; /* Duplicate - shouldn't happen */
	}
	
	rb_link_node(&alloc->node, parent, new);
	rb_insert_color(&alloc->node, &pool->allocations);
}

/*
 * Allocate sectors from spare pool
 */
struct spare_allocation *spare_pool_allocate(struct spare_pool *pool,
					      sector_t original_sector,
					      u32 sector_count)
{
	struct spare_device *spare;
	struct spare_allocation *alloc;
	unsigned long flags;
	sector_t spare_sector;
	bool allocated = false;
	
	if (!pool || sector_count == 0)
		return ERR_PTR(-EINVAL);
	
	/* Allocate allocation structure */
	alloc = kzalloc(sizeof(*alloc), GFP_NOIO);
	if (!alloc)
		return ERR_PTR(-ENOMEM);
	
	/* Try to allocate from first available spare */
	spin_lock_irqsave(&pool->spares_lock, flags);
	spare_for_each_device(pool, spare) {
		if (!spare_device_is_available(spare))
			continue;
		
		spin_lock(&spare->lock);
		spare_sector = spare_device_find_free_sector(spare, sector_count);
		if (spare_sector != (sector_t)-1) {
			spare_device_mark_allocated(spare, spare_sector, sector_count);
			atomic_inc(&spare->refcount);
			atomic64_inc(&spare->total_allocations);
			atomic64_inc(&spare->current_allocations);
			allocated = true;
			spin_unlock(&spare->lock);
			break;
		}
		spin_unlock(&spare->lock);
	}
	spin_unlock_irqrestore(&pool->spares_lock, flags);
	
	if (!allocated) {
		kfree(alloc);
		DMWARN("No spare capacity available for allocation (%u sectors)",
		       sector_count);
		return ERR_PTR(-ENOSPC);
	}
	
	/* Initialize allocation */
	alloc->original_sector = original_sector;
	alloc->spare = spare;
	alloc->spare_sector = spare_sector;
	alloc->sector_count = sector_count;
	alloc->ti = pool->ti;
	alloc->allocated_at = ktime_get();
	alloc->allocation_id = atomic_inc_return(&pool->next_allocation_id);
	
	/* Add to allocation tree */
	spin_lock_irqsave(&pool->allocations_lock, flags);
	spare_allocation_insert(pool, alloc);
	atomic_inc(&pool->allocation_count);
	atomic64_add(sector_count, &pool->allocated_spare_capacity);
	atomic64_inc(&pool->total_allocations_lifetime);
	spin_unlock_irqrestore(&pool->allocations_lock, flags);
	
	DMINFO("Allocated %u sectors from spare %s (sector %llu) for original sector %llu",
	       sector_count, spare->dev_path,
	       (unsigned long long)spare_sector,
	       (unsigned long long)original_sector);
	
	return alloc;
}
EXPORT_SYMBOL(spare_pool_allocate);

/*
 * Free allocation
 */
int spare_pool_free(struct spare_pool *pool, struct spare_allocation *alloc)
{
	struct spare_device *spare;
	unsigned long flags;
	
	if (!pool || !alloc)
		return -EINVAL;
	
	spare = alloc->spare;
	
	/* Remove from allocation tree */
	spin_lock_irqsave(&pool->allocations_lock, flags);
	rb_erase(&alloc->node, &pool->allocations);
	atomic_dec(&pool->allocation_count);
	atomic64_sub(alloc->sector_count, &pool->allocated_spare_capacity);
	spin_unlock_irqrestore(&pool->allocations_lock, flags);
	
	/* Free sectors in spare device */
	spin_lock_irqsave(&spare->lock, flags);
	spare_device_mark_free(spare, alloc->spare_sector, alloc->sector_count);
	atomic_dec(&spare->refcount);
	atomic64_dec(&spare->current_allocations);
	spin_unlock_irqrestore(&spare->lock, flags);
	
	DMINFO("Freed allocation #%u (%u sectors from spare %s)",
	       alloc->allocation_id, alloc->sector_count,
	       spare->dev_path);
	
	kfree(alloc);
	return 0;
}
EXPORT_SYMBOL(spare_pool_free);

/*
 * Lookup allocation by original sector
 */
struct spare_allocation *spare_pool_lookup_allocation(struct spare_pool *pool,
						       sector_t original_sector)
{
	struct rb_node *node;
	struct spare_allocation *alloc;
	unsigned long flags;
	
	if (!pool)
		return NULL;
	
	spin_lock_irqsave(&pool->allocations_lock, flags);
	node = pool->allocations.rb_node;
	
	while (node) {
		alloc = rb_entry(node, struct spare_allocation, node);
		
		if (original_sector < alloc->original_sector)
			node = node->rb_left;
		else if (original_sector > alloc->original_sector)
			node = node->rb_right;
		else {
			spin_unlock_irqrestore(&pool->allocations_lock, flags);
			return alloc;
		}
	}
	
	spin_unlock_irqrestore(&pool->allocations_lock, flags);
	return NULL;
}
EXPORT_SYMBOL(spare_pool_lookup_allocation);

/*
 * Get pool statistics
 */
void spare_pool_get_stats(struct spare_pool *pool,
			  struct spare_pool_stats *stats)
{
	struct spare_device *spare;
	unsigned long flags;
	
	if (!pool || !stats)
		return;
	
	memset(stats, 0, sizeof(*stats));
	
	stats->spare_device_count = atomic_read(&pool->spare_device_count);
	stats->total_capacity = atomic64_read(&pool->total_spare_capacity);
	stats->allocated_capacity = atomic64_read(&pool->allocated_spare_capacity);
	stats->free_capacity = stats->total_capacity - stats->allocated_capacity;
	stats->active_allocations = atomic_read(&pool->allocation_count);
	stats->lifetime_allocations = atomic64_read(&pool->total_allocations_lifetime);
	
	spin_lock_irqsave(&pool->spares_lock, flags);
	spare_for_each_device(pool, spare) {
		switch (spare->state) {
		case SPARE_STATE_AVAILABLE:
			stats->spares_available++;
			break;
		case SPARE_STATE_IN_USE:
			stats->spares_in_use++;
			break;
		case SPARE_STATE_FULL:
			stats->spares_full++;
			break;
		case SPARE_STATE_FAILED:
			stats->spares_failed++;
			break;
		}
	}
	spin_unlock_irqrestore(&pool->spares_lock, flags);
}
EXPORT_SYMBOL(spare_pool_get_stats);

/*
 * Print pool statistics
 */
void spare_pool_print_stats(struct spare_pool *pool)
{
	struct spare_pool_stats stats;
	
	spare_pool_get_stats(pool, &stats);
	
	DMINFO("Spare Pool Statistics:");
	DMINFO("  Devices: %u total (%u available, %u in-use, %u full, %u failed)",
	       stats.spare_device_count, stats.spares_available,
	       stats.spares_in_use, stats.spares_full, stats.spares_failed);
	DMINFO("  Capacity: %llu MB total, %llu MB allocated, %llu MB free",
	       (unsigned long long)(stats.total_capacity >> 11),
	       (unsigned long long)(stats.allocated_capacity >> 11),
	       (unsigned long long)(stats.free_capacity >> 11));
	DMINFO("  Allocations: %u active, %llu lifetime",
	       stats.active_allocations, stats.lifetime_allocations);
}
EXPORT_SYMBOL(spare_pool_print_stats);

/*
 * Handle dmsetup messages
 */
int spare_pool_message(struct spare_pool *pool, unsigned int argc, char **argv)
{
	if (!pool || argc < 1)
		return -EINVAL;
	
	if (strcmp(argv[0], "spare_add") == 0) {
		if (argc != 2)
			return -EINVAL;
		return spare_pool_add_device(pool, argv[1]);
	}
	
	if (strcmp(argv[0], "spare_remove") == 0) {
		if (argc != 2)
			return -EINVAL;
		return spare_pool_remove_device(pool, argv[1]);
	}
	
	if (strcmp(argv[0], "spare_stats") == 0) {
		spare_pool_print_stats(pool);
		return 0;
	}
	
	return -EINVAL;
}
EXPORT_SYMBOL(spare_pool_message);

MODULE_AUTHOR("dm-remap development team");
MODULE_DESCRIPTION("External spare device management for dm-remap v4.0");
MODULE_LICENSE("GPL");
