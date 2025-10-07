/*
 * dm-remap-hotpath-sysfs.h - Sysfs Interface for Hotpath Optimization
 * 
 * Week 9-10 Advanced Optimization: Header file for sysfs interface providing
 * monitoring and control of hotpath I/O optimization performance.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_HOTPATH_SYSFS_H
#define DM_REMAP_HOTPATH_SYSFS_H

#include "dm-remap-core.h"

/*
 * Function declarations for hotpath sysfs interface
 */

/**
 * dmr_hotpath_sysfs_create - Create hotpath sysfs interfaces
 * @rc: Remap context
 * 
 * Creates sysfs attributes for monitoring and controlling hotpath optimization:
 * - hotpath_stats: Real-time performance statistics
 * - hotpath_reset: Reset statistics counters
 * - hotpath_batch_size: Current batch processing size
 * - hotpath_prefetch_distance: Prefetch distance setting
 * - hotpath_efficiency: Overall fast-path efficiency percentage
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_hotpath_sysfs_create(struct remap_c *rc);

/**
 * dmr_hotpath_sysfs_remove - Remove hotpath sysfs interfaces
 * @rc: Remap context
 * 
 * Cleans up all hotpath-related sysfs attributes and groups.
 */
void dmr_hotpath_sysfs_remove(struct remap_c *rc);

/*
 * Sysfs attribute paths (relative to dm-remap device):
 * - /sys/block/dm-X/dm-remap/hotpath/hotpath_stats
 * - /sys/block/dm-X/dm-remap/hotpath/hotpath_reset
 * - /sys/block/dm-X/dm-remap/hotpath/hotpath_batch_size
 * - /sys/block/dm-X/dm-remap/hotpath/hotpath_prefetch_distance
 * - /sys/block/dm-X/dm-remap/hotpath/hotpath_efficiency
 */

#endif /* DM_REMAP_HOTPATH_SYSFS_H */