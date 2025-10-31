/*
 * dm-remap v4.0 - Statistics Export API
 * 
 * Simple API for dm-remap modules to update statistics
 * that get exported via sysfs for monitoring tools.
 */

#ifndef DM_REMAP_V4_STATS_H
#define DM_REMAP_V4_STATS_H

#include <linux/types.h>

/*
 * Update functions called by dm-remap-v4-real.c
 */
void dm_remap_stats_inc_reads(void);
void dm_remap_stats_inc_writes(void);
void dm_remap_stats_inc_remaps(void);
void dm_remap_stats_inc_errors(void);
void dm_remap_stats_set_active_mappings(unsigned int count);
void dm_remap_stats_update_latency(u64 latency_ns);
void dm_remap_stats_update_health_score(unsigned int score);

#endif /* DM_REMAP_V4_STATS_H */
