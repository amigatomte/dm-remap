/*
 * dm-remap-recovery.c - Recovery system for dm-remap v3.0
 *
 * This file implements device activation recovery and remap table restoration
 * from persistent metadata stored on the spare device.
 *
 * Copyright (C) 2025 dm-remap project
 */

#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "dm-remap-metadata.h"
#include "dm-remap-core.h"

/*
 * Restore remap table from metadata during device activation
 */
int dm_remap_recovery_restore_table(struct remap_c *rc)
{
	u32 entry_count, i;
	struct dm_remap_entry *meta_entry;
	struct remap_entry *table_entry;
	sector_t spare_offset;
	int restored_count = 0;
	
	if (!rc || !rc->metadata) {
		return -EINVAL;
	}
	
	entry_count = le32_to_cpu(rc->metadata->header.entry_count);
	if (entry_count == 0) {
		DMR_DEBUG(1, "No remap entries found in metadata");
		return 0;
	}
	
	DMR_DEBUG(1, "Restoring %u remap entries from metadata", entry_count);
	
	/* Process each metadata entry */
	for (i = 0; i < entry_count; i++) {
		meta_entry = &rc->metadata->entries[i];
		
		/* Skip invalid or empty entries */
		if (le64_to_cpu(meta_entry->main_sector) == (u64)-1) {
			continue;
		}
		
		/* Calculate spare sector offset within our spare area */
		spare_offset = le64_to_cpu(meta_entry->spare_sector) - rc->spare_start;
		
		/* Validate spare sector is within our range */
		if (spare_offset >= rc->spare_len) {
			DMR_DEBUG(0, "Invalid spare sector %llu in metadata (offset %llu >= %llu)",
				 (unsigned long long)le64_to_cpu(meta_entry->spare_sector),
				 (unsigned long long)spare_offset,
				 (unsigned long long)rc->spare_len);
			continue;
		}
		
		/* Get corresponding table entry */
		table_entry = &rc->table[spare_offset];
		
		/* Check if table slot is already used */
		if (table_entry->main_lba != (sector_t)-1) {
			DMR_DEBUG(0, "Table slot %llu already in use during recovery",
				 (unsigned long long)spare_offset);
			continue;
		}
		
		/* Restore the remap entry */
		table_entry->main_lba = le64_to_cpu(meta_entry->main_sector);
		table_entry->spare_lba = rc->spare_start + spare_offset;
		table_entry->error_count = 0;          /* Reset error tracking */
		table_entry->access_count = 0;         /* Reset access tracking */
		table_entry->last_error_time = 0;      /* Reset error time */
		table_entry->remap_reason = DMR_REMAP_MANUAL;  /* Mark as restored */
		table_entry->health_status = DMR_HEALTH_REMAPPED;
		table_entry->reserved = 0;
		
		restored_count++;
		
		DMR_DEBUG(2, "Restored remap: main_sector=%llu -> spare_sector=%llu (slot=%llu)",
			 (unsigned long long)table_entry->main_lba,
			 (unsigned long long)table_entry->spare_lba,
			 (unsigned long long)spare_offset);
	}
	
	/* Update spare usage counter */
	rc->spare_used = restored_count;
	
	/* Update statistics */
	rc->manual_remaps = restored_count;  /* Count as manual remaps */
	
	DMR_DEBUG(1, "Recovery complete: restored %d remap entries", restored_count);
	
	return restored_count;
}

/*
 * Synchronize current remap table to metadata
 */
int dm_remap_recovery_sync_metadata(struct remap_c *rc)
{
	u32 entry_count = 0;
	int i;
	struct remap_entry *table_entry;
	struct dm_remap_entry *meta_entry;
	enum dm_remap_metadata_result result;
	
	if (!rc || !rc->metadata) {
		return -EINVAL;
	}
	
	DMR_DEBUG(2, "Synchronizing remap table to metadata");
	
	/* Count active entries and update metadata */
	for (i = 0; i < rc->spare_len; i++) {
		table_entry = &rc->table[i];
		
		/* Skip unused entries */
		if (table_entry->main_lba == (sector_t)-1) {
			continue;
		}
		
		/* Validate entry count doesn't exceed metadata capacity */
		if (entry_count >= DM_REMAP_MAX_METADATA_ENTRIES) {
			DMR_DEBUG(0, "Too many entries for metadata storage (max %zu)",
				 DM_REMAP_MAX_METADATA_ENTRIES);
			break;
		}
		
		/* Get metadata entry */
		meta_entry = &rc->metadata->entries[entry_count];
		
		/* Copy table entry to metadata */
		meta_entry->main_sector = cpu_to_le64(table_entry->main_lba);
		meta_entry->spare_sector = cpu_to_le64(table_entry->spare_lba);
		
		entry_count++;
		
		DMR_DEBUG(3, "Synced entry %u: main=%llu -> spare=%llu",
			 entry_count - 1,
			 (unsigned long long)table_entry->main_lba,
			 (unsigned long long)table_entry->spare_lba);
	}
	
	/* Update metadata header */
	rc->metadata->header.entry_count = cpu_to_le32(entry_count);
	rc->metadata->header.generation = cpu_to_le32(
		le32_to_cpu(rc->metadata->header.generation) + 1);
	
	/* Mark metadata as dirty and trigger save */
	dm_remap_metadata_mark_dirty(rc->metadata);
	
	/* Force immediate save for critical operations */
	result = dm_remap_autosave_force(rc->metadata);
	if (result != DM_REMAP_META_SUCCESS) {
		DMR_DEBUG(0, "Failed to save metadata after sync: %d", result);
		return -EIO;
	}
	
	DMR_DEBUG(1, "Successfully synced %u entries to metadata", entry_count);
	
	return entry_count;
}

/*
 * Add a new remap entry and update metadata
 */
int dm_remap_recovery_add_remap(struct remap_c *rc, sector_t main_sector, 
				sector_t spare_sector)
{
	enum dm_remap_metadata_result result;
	
	if (!rc || !rc->metadata) {
		return -EINVAL;
	}
	
	DMR_DEBUG(2, "Adding remap to metadata: main=%llu -> spare=%llu",
		 (unsigned long long)main_sector,
		 (unsigned long long)spare_sector);
	
	/* Add entry to metadata */
	result = dm_remap_metadata_add_entry(rc->metadata, main_sector, spare_sector);
	if (result != DM_REMAP_META_SUCCESS) {
		DMR_DEBUG(0, "Failed to add remap entry to metadata: %d", result);
		return -EIO;
	}
	
	/* Trigger auto-save (non-blocking) */
	dm_remap_autosave_trigger(rc->metadata, false);
	
	DMR_DEBUG(2, "Successfully added remap entry to metadata");
	
	return 0;
}

/*
 * Remove a remap entry from metadata (simplified for Phase 3)
 * For now, just trigger a full sync to remove stale entries
 */
int dm_remap_recovery_remove_remap(struct remap_c *rc, sector_t main_sector)
{
	if (!rc || !rc->metadata) {
		return -EINVAL;
	}
	
	DMR_DEBUG(2, "Removing remap from metadata via sync: main=%llu",
		 (unsigned long long)main_sector);
	
	/* Trigger full sync to remove stale entries */
	return dm_remap_recovery_sync_metadata(rc);
}

/*
 * Validate metadata consistency with current remap table
 */
int dm_remap_recovery_validate_consistency(struct remap_c *rc)
{
	u32 metadata_count, table_count = 0;
	int i, inconsistencies = 0;
	struct remap_entry *table_entry;
	sector_t spare_sector;
	
	if (!rc || !rc->metadata) {
		return -EINVAL;
	}
	
	metadata_count = le32_to_cpu(rc->metadata->header.entry_count);
	
	/* Count active table entries */
	for (i = 0; i < rc->spare_len; i++) {
		table_entry = &rc->table[i];
		if (table_entry->main_lba != (sector_t)-1) {
			table_count++;
		}
	}
	
	DMR_DEBUG(2, "Validating consistency: metadata has %u entries, table has %u",
		 metadata_count, table_count);
	
	/* Check each table entry exists in metadata */
	for (i = 0; i < rc->spare_len; i++) {
		table_entry = &rc->table[i];
		
		if (table_entry->main_lba == (sector_t)-1) {
			continue;
		}
		
		/* Check if this entry exists in metadata */
		if (!dm_remap_metadata_find_entry(rc->metadata, 
						  table_entry->main_lba, 
						  &spare_sector)) {
			DMR_DEBUG(0, "Table entry main=%llu not found in metadata",
				 (unsigned long long)table_entry->main_lba);
			inconsistencies++;
		} else if (spare_sector != table_entry->spare_lba) {
			DMR_DEBUG(0, "Spare sector mismatch for main=%llu: table=%llu, metadata=%llu",
				 (unsigned long long)table_entry->main_lba,
				 (unsigned long long)table_entry->spare_lba,
				 (unsigned long long)spare_sector);
			inconsistencies++;
		}
	}
	
	if (inconsistencies > 0) {
		DMR_DEBUG(0, "Found %d consistency issues between table and metadata",
			 inconsistencies);
		return inconsistencies;
	}
	
	DMR_DEBUG(2, "Consistency validation passed");
	return 0;
}

/*
 * Recovery system statistics
 */
void dm_remap_recovery_get_stats(struct remap_c *rc, 
				u64 *successful_saves, u64 *failed_saves,
				bool *autosave_active)
{
	if (!rc || !rc->metadata) {
		if (successful_saves) *successful_saves = 0;
		if (failed_saves) *failed_saves = 0;
		if (autosave_active) *autosave_active = false;
		return;
	}
	
	dm_remap_autosave_stats(rc->metadata, successful_saves, failed_saves, autosave_active);
}