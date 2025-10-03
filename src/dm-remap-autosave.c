/*
 * dm-remap-autosave.c - Auto-save system for dm-remap v3.0
 *
 * This file implements the background auto-save system using work queues
 * and timers for periodic metadata persistence.
 *
 * Copyright (C) 2025 dm-remap project
 */

#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "dm-remap-metadata.h"

/*
 * Auto-save configuration
 */
static unsigned int dm_remap_autosave_interval = DM_REMAP_DEFAULT_AUTOSAVE_INTERVAL;
module_param(dm_remap_autosave_interval, uint, 0644);
MODULE_PARM_DESC(dm_remap_autosave_interval, "Auto-save interval in seconds (default: 60)");

static bool dm_remap_autosave_enabled = true;
module_param(dm_remap_autosave_enabled, bool, 0644);
MODULE_PARM_DESC(dm_remap_autosave_enabled, "Enable auto-save system (default: true)");

/*
 * Auto-save work function
 */
static void dm_remap_autosave_work(struct work_struct *work)
{
	struct dm_remap_metadata *meta = container_of(work, struct dm_remap_metadata, autosave_work.work);
	enum dm_remap_metadata_result result;
	
	DMREMAP_META_DEBUG(meta, "Auto-save work starting");
	
	/* Check if metadata is dirty and needs saving */
	if (!dm_remap_metadata_is_dirty(meta)) {
		DMREMAP_META_DEBUG(meta, "Metadata clean, skipping auto-save");
		goto reschedule;
	}
	
	/* Perform metadata sync */
	result = dm_remap_metadata_sync(meta);
	if (result == DM_REMAP_META_SUCCESS) {
		atomic64_inc(&meta->autosaves_successful);
		DMREMAP_META_DEBUG(meta, "Auto-save completed successfully");
	} else {
		atomic64_inc(&meta->autosaves_failed);
		DMREMAP_META_ERROR(meta, "Auto-save failed with result %d", result);
	}

reschedule:
	/* Reschedule next auto-save if enabled */
	if (dm_remap_autosave_enabled && meta->autosave_active) {
		queue_delayed_work(meta->autosave_wq, &meta->autosave_work,
				  msecs_to_jiffies(dm_remap_autosave_interval * 1000));
	}
}

/*
 * Initialize auto-save system
 */
int dm_remap_autosave_init(struct dm_remap_metadata *meta)
{
	if (!meta)
		return -EINVAL;
		
	DMREMAP_META_DEBUG(meta, "Initializing auto-save system");
	
	/* Create dedicated workqueue for auto-save operations */
	meta->autosave_wq = alloc_workqueue("dm-remap-autosave", WQ_MEM_RECLAIM, 1);
	if (!meta->autosave_wq) {
		DMREMAP_META_ERROR(meta, "Failed to create auto-save workqueue");
		return -ENOMEM;
	}
	
	/* Initialize delayed work */
	INIT_DELAYED_WORK(&meta->autosave_work, dm_remap_autosave_work);
	
	/* Initialize statistics */
	atomic64_set(&meta->autosaves_successful, 0);
	atomic64_set(&meta->autosaves_failed, 0);
	
	/* Mark as active but don't start yet */
	meta->autosave_active = true;
	
	DMREMAP_META_INFO(meta, "Auto-save system initialized (interval: %u seconds)",
			 dm_remap_autosave_interval);
	
	return 0;
}

/*
 * Start auto-save system
 */
void dm_remap_autosave_start(struct dm_remap_metadata *meta)
{
	if (!meta || !meta->autosave_wq)
		return;
		
	if (!dm_remap_autosave_enabled) {
		DMREMAP_META_INFO(meta, "Auto-save disabled via module parameter");
		return;
	}
	
	DMREMAP_META_DEBUG(meta, "Starting auto-save system");
	
	meta->autosave_active = true;
	
	/* Schedule first auto-save */
	queue_delayed_work(meta->autosave_wq, &meta->autosave_work,
			  msecs_to_jiffies(dm_remap_autosave_interval * 1000));
			  
	DMREMAP_META_INFO(meta, "Auto-save system started");
}

/*
 * Stop auto-save system
 */
void dm_remap_autosave_stop(struct dm_remap_metadata *meta)
{
	if (!meta || !meta->autosave_wq)
		return;
		
	DMREMAP_META_DEBUG(meta, "Stopping auto-save system");
	
	meta->autosave_active = false;
	
	/* Cancel any pending auto-save work */
	cancel_delayed_work_sync(&meta->autosave_work);
	
	DMREMAP_META_INFO(meta, "Auto-save system stopped");
}

/*
 * Clean up auto-save system
 */
void dm_remap_autosave_cleanup(struct dm_remap_metadata *meta)
{
	if (!meta)
		return;
		
	DMREMAP_META_DEBUG(meta, "Cleaning up auto-save system");
	
	/* Stop auto-save first */
	dm_remap_autosave_stop(meta);
	
	/* Destroy workqueue */
	if (meta->autosave_wq) {
		destroy_workqueue(meta->autosave_wq);
		meta->autosave_wq = NULL;
	}
	
	DMREMAP_META_INFO(meta, "Auto-save system cleaned up");
}

/*
 * Force immediate auto-save (for critical operations)
 */
enum dm_remap_metadata_result dm_remap_autosave_force(struct dm_remap_metadata *meta)
{
	if (!meta || !meta->autosave_wq)
		return DM_REMAP_META_ERROR_CORRUPT;
		
	DMREMAP_META_DEBUG(meta, "Forcing immediate auto-save");
	
	/* Cancel any pending auto-save */
	cancel_delayed_work(&meta->autosave_work);
	
	/* Execute save immediately in current context */
	return dm_remap_metadata_sync(meta);
}

/*
 * Trigger auto-save (mark dirty and optionally force immediate save)
 */
void dm_remap_autosave_trigger(struct dm_remap_metadata *meta, bool immediate)
{
	if (!meta)
		return;
		
	DMREMAP_META_DEBUG(meta, "Auto-save triggered (immediate: %s)", immediate ? "yes" : "no");
	
	/* Mark metadata as dirty */
	dm_remap_metadata_mark_dirty(meta);
	
	if (immediate) {
		/* Force immediate save */
		dm_remap_autosave_force(meta);
	} else if (meta->autosave_active && meta->autosave_wq) {
		/* Reschedule auto-save to happen sooner */
		cancel_delayed_work(&meta->autosave_work);
		queue_delayed_work(meta->autosave_wq, &meta->autosave_work,
				  msecs_to_jiffies(1000)); /* Save in 1 second */
	}
}

/*
 * Get auto-save statistics
 */
void dm_remap_autosave_stats(struct dm_remap_metadata *meta,
			    u64 *successful, u64 *failed, bool *active)
{
	if (!meta)
		return;
		
	if (successful)
		*successful = atomic64_read(&meta->autosaves_successful);
	if (failed)
		*failed = atomic64_read(&meta->autosaves_failed);
	if (active)
		*active = meta->autosave_active;
}

/*
 * Update auto-save interval (runtime configuration)
 */
void dm_remap_autosave_set_interval(unsigned int interval_seconds)
{
	if (interval_seconds < 1)
		interval_seconds = 1;
	if (interval_seconds > 3600) /* Max 1 hour */
		interval_seconds = 3600;
		
	dm_remap_autosave_interval = interval_seconds;
	
	printk(KERN_INFO "dm-remap: Auto-save interval updated to %u seconds\n",
	       interval_seconds);
}

/*
 * Enable/disable auto-save system globally
 */
void dm_remap_autosave_set_enabled(bool enabled)
{
	dm_remap_autosave_enabled = enabled;
	
	printk(KERN_INFO "dm-remap: Auto-save system %s\n",
	       enabled ? "enabled" : "disabled");
}