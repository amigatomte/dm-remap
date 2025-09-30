/*
 * dm-remap-sysfs.h - Sysfs interface definitions for dm-remap v2.0
 * 
 * This header defines the sysfs interface for proper userspace
 * communication of v2.0 features.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_SYSFS_H
#define DM_REMAP_SYSFS_H

#include <linux/kobject.h>

/* Forward declarations */
struct remap_c;

/*
 * External kobject type declaration (defined in .c file)
 */
extern struct kobj_type ktype_dmr_target;

/*
 * Function prototypes for sysfs operations
 */

/* Global sysfs interface */
int dmr_sysfs_init(void);
void dmr_sysfs_exit(void);

/* Per-target sysfs interface */
int dmr_sysfs_create_target(struct remap_c *rc, const char *target_name);
void dmr_sysfs_remove_target(struct remap_c *rc);

/*
 * Sysfs paths for v2.0 features:
 * 
 * Global: /sys/kernel/dm_remap/
 *   - version: Version information
 *   - targets: List of active targets
 * 
 * Per-target: /sys/kernel/dm_remap/<target_name>/
 *   - health: Overall device health status
 *   - stats: Comprehensive statistics
 *   - scan: Detailed sector scan results
 *   - auto_remap: Enable/disable automatic remapping
 *   - error_threshold: Error threshold for auto-remapping
 */

#endif /* DM_REMAP_SYSFS_H */