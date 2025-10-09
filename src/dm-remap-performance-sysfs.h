/*
 * dm-remap-performance-sysfs.h - Performance Profiler Sysfs Interface
 * 
 * Header file for the performance profiler sysfs interface.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_PERFORMANCE_SYSFS_H
#define DM_REMAP_PERFORMANCE_SYSFS_H

struct remap_c;

/*
 * Function declarations
 */
int dmr_perf_sysfs_create(struct remap_c *rc);
void dmr_perf_sysfs_remove(struct remap_c *rc);

#endif /* DM_REMAP_PERFORMANCE_SYSFS_H */