/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * dm-remap-v4-spare-pool.h - External spare device management for dm-remap v4.0
 *
 * Copyright (C) 2025 dm-remap development team
 *
 * This header defines the interface for managing external spare devices.
 * Spare devices provide additional remapping capacity when internal drive
 * spare sectors are exhausted or unavailable.
 *
 * Key Features:
 * - Manual spare device addition/removal
 * - First-fit allocation strategy
 * - Health monitoring integration (Priority 1)
 * - Persistent configuration (Priority 6)
 * - Simple, robust design
 */

#ifndef DM_REMAP_V4_SPARE_POOL_H
#define DM_REMAP_V4_SPARE_POOL_H

#include <linux/device-mapper.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/rbtree.h>

/*
 * Spare device states
 */
enum spare_device_state {
	SPARE_STATE_AVAILABLE = 0,	/* Ready for allocation */
	SPARE_STATE_IN_USE,		/* Has active allocations */
	SPARE_STATE_FULL,		/* No free space remaining */
	SPARE_STATE_FAILED,		/* Device failed/removed */
};

/*
 * Represents a single external spare device
 */
struct spare_device {
	struct list_head list;		/* List of all spares */
	
	/* Device information */
	struct block_device *bdev;	/* Block device handle */
	void *bdev_handle;		/* For kernel 6.5+ bdev_open_by_path */
	char *dev_path;			/* Device path (e.g., "/dev/sdc") */
	dev_t dev;			/* Device number */
	
	/* Capacity tracking */
	sector_t total_sectors;		/* Total device capacity */
	sector_t allocated_sectors;	/* Currently allocated */
	sector_t free_sectors;		/* Available for allocation */
	
	/* State */
	enum spare_device_state state;
	atomic_t refcount;		/* Number of active allocations */
	
	/* Allocation tracking (simple bitmap) */
	unsigned long *allocation_bitmap; /* Tracks allocated sectors */
	size_t bitmap_size;		/* Size of bitmap in longs */
	sector_t allocation_unit;	/* Granularity (e.g., 8 sectors) */
	
	/* Statistics */
	atomic64_t total_allocations;	/* Lifetime allocation count */
	atomic64_t current_allocations;	/* Active allocations */
	ktime_t added_at;		/* When spare was added */
	
	/* Lock for allocation operations */
	spinlock_t lock;
};

/*
 * Represents an allocation from a spare device
 */
struct spare_allocation {
	struct rb_node node;		/* Red-black tree node */
	
	/* Allocation details */
	sector_t original_sector;	/* Original failing sector */
	struct spare_device *spare;	/* Which spare device */
	sector_t spare_sector;		/* Location in spare device */
	u32 sector_count;		/* Number of sectors */
	
	/* Ownership */
	struct dm_target *ti;		/* dm-remap target using this */
	
	/* Metadata */
	ktime_t allocated_at;
	u32 allocation_id;		/* Unique ID for tracking */
};

/*
 * Spare pool manager - one per dm-remap instance
 */
struct spare_pool {
	/* List of all spare devices */
	struct list_head spares;
	spinlock_t spares_lock;
	
	/* Allocation tracking */
	struct rb_root allocations;	/* All active allocations */
	spinlock_t allocations_lock;
	atomic_t allocation_count;
	atomic_t next_allocation_id;
	
	/* Statistics */
	atomic_t spare_device_count;
	atomic64_t total_spare_capacity;
	atomic64_t allocated_spare_capacity;
	atomic64_t total_allocations_lifetime;
	
	/* Configuration */
	sector_t allocation_unit;	/* Default: 8 sectors (4KB) */
	bool allow_partial_allocations; /* Allow smaller than requested */
	
	/* Back-reference to dm-remap target */
	struct dm_target *ti;
};

/*
 * Initialization and cleanup
 */
int spare_pool_init(struct spare_pool *pool, struct dm_target *ti);
void spare_pool_exit(struct spare_pool *pool);

/*
 * Spare device management
 */
int spare_pool_add_device(struct spare_pool *pool, const char *dev_path);
int spare_pool_remove_device(struct spare_pool *pool, const char *dev_path);
struct spare_device *spare_pool_get_device(struct spare_pool *pool, 
					    const char *dev_path);
void spare_pool_put_device(struct spare_device *spare);

/*
 * Allocation operations
 */
struct spare_allocation *spare_pool_allocate(struct spare_pool *pool,
					      sector_t original_sector,
					      u32 sector_count);
int spare_pool_free(struct spare_pool *pool, 
		    struct spare_allocation *allocation);
struct spare_allocation *spare_pool_lookup_allocation(struct spare_pool *pool,
						       sector_t original_sector);

/*
 * I/O operations
 */
int spare_pool_read_sector(struct spare_allocation *alloc, 
			   struct bio *bio, sector_t offset);
int spare_pool_write_sector(struct spare_allocation *alloc,
			    struct bio *bio, sector_t offset);

/*
 * Statistics and monitoring
 */
struct spare_pool_stats {
	u32 spare_device_count;
	sector_t total_capacity;
	sector_t allocated_capacity;
	sector_t free_capacity;
	u32 active_allocations;
	u64 lifetime_allocations;
	u32 spares_available;
	u32 spares_in_use;
	u32 spares_full;
	u32 spares_failed;
};

void spare_pool_get_stats(struct spare_pool *pool, 
			  struct spare_pool_stats *stats);
void spare_pool_print_stats(struct spare_pool *pool);

/*
 * Integration with Priority 6 (setup reassembly)
 */
int spare_pool_save_metadata(struct spare_pool *pool, void *buffer, 
			      size_t buffer_size);
int spare_pool_load_metadata(struct spare_pool *pool, void *buffer,
			      size_t buffer_size);

/*
 * dmsetup message interface
 */
int spare_pool_message(struct spare_pool *pool, unsigned int argc, char **argv);

/*
 * Helper macros
 */
#define SPARE_ALLOCATION_UNIT_DEFAULT	8	/* 8 sectors = 4KB */
#define SPARE_ALLOCATION_UNIT_MIN	1	/* Minimum 1 sector */
#define SPARE_ALLOCATION_UNIT_MAX	256	/* Maximum 128KB */

#define spare_for_each_device(pool, spare) \
	list_for_each_entry(spare, &(pool)->spares, list)

#define spare_for_each_device_safe(pool, spare, tmp) \
	list_for_each_entry_safe(spare, tmp, &(pool)->spares, list)

/*
 * Inline helpers
 */
static inline bool spare_device_is_available(struct spare_device *spare)
{
	return spare->state == SPARE_STATE_AVAILABLE && 
	       spare->free_sectors > 0;
}

static inline bool spare_device_is_full(struct spare_device *spare)
{
	return spare->free_sectors == 0 || 
	       spare->state == SPARE_STATE_FULL;
}

static inline sector_t spare_pool_total_free_capacity(struct spare_pool *pool)
{
	struct spare_device *spare;
	sector_t total = 0;
	unsigned long flags;
	
	spin_lock_irqsave(&pool->spares_lock, flags);
	spare_for_each_device(pool, spare) {
		if (spare_device_is_available(spare))
			total += spare->free_sectors;
	}
	spin_unlock_irqrestore(&pool->spares_lock, flags);
	
	return total;
}

#endif /* DM_REMAP_V4_SPARE_POOL_H */
