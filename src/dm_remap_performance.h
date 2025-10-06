/* dm-remap v4.0 Performance Optimization - Header
 *
 * Performance optimizations for allocation algorithms
 */

#ifndef DM_REMAP_PERFORMANCE_H
#define DM_REMAP_PERFORMANCE_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include "dm-remap-core.h"

/* Forward declarations */
struct remap_c;

/* Forward declaration - full definition in dm_remap_performance.c */
struct dmr_allocation_cache;

/* Performance optimization functions */
int dmr_init_allocation_cache(struct remap_c *rc);
void dmr_cleanup_allocation_cache(struct remap_c *rc);
sector_t dmr_allocate_spare_sector_optimized(struct remap_c *rc);
void dmr_get_performance_stats(struct remap_c *rc, char *stats, size_t max_len);

#endif /* DM_REMAP_PERFORMANCE_H */