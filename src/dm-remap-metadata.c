/*
 * dm-remap-metadata.c - Persistent metadata implementation for dm-remap v3.0
 *
 * This file implements the persistent metadata system that stores remap tables
 * in the spare device header, enabling recovery after system reboots.
 *
 * Copyright (C) 2025 dm-remap project
 */

#include <linux/module.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/time64.h>
#include <linux/crc32.h>
#include <linux/vmalloc.h>

#include "dm-remap-metadata.h"

/*
 * Default configuration values
 */
#define DM_REMAP_DEFAULT_SAVE_INTERVAL 60  /* Auto-save every 60 seconds */

/*
 * Create and initialize metadata context
 */
struct dm_remap_metadata *dm_remap_metadata_create(struct block_device *spare_bdev,
						   sector_t main_size,
						   sector_t spare_size)
{
	struct dm_remap_metadata *meta;
	
	DMREMAP_META_DEBUG(NULL, "Creating metadata context for spare device");
	
	/* Allocate metadata context */
	meta = kzalloc(sizeof(*meta), GFP_KERNEL);
	if (!meta) {
		DMREMAP_META_ERROR(NULL, "Failed to allocate metadata context");
		return NULL;
	}
	
	/* Initialize device references */
	meta->spare_bdev = spare_bdev;
	
	/* Allocate entry array */
	meta->entries = vzalloc(DM_REMAP_MAX_METADATA_ENTRIES * sizeof(struct dm_remap_entry));
	if (!meta->entries) {
		DMREMAP_META_ERROR(meta, "Failed to allocate entries array");
		kfree(meta);
		return NULL;
	}
	
	/* Initialize header */
	memcpy(meta->header.magic, DM_REMAP_MAGIC, DM_REMAP_MAGIC_LEN);
	meta->header.version = cpu_to_le32(DM_REMAP_METADATA_VERSION);
	meta->header.creation_time = cpu_to_le64(ktime_get_real_seconds());
	meta->header.last_update_time = meta->header.creation_time;
	meta->header.main_device_size = cpu_to_le64(main_size);
	meta->header.spare_device_size = cpu_to_le64(spare_size);
	meta->header.entry_count = 0;
	meta->header.max_entries = cpu_to_le32(DM_REMAP_MAX_METADATA_ENTRIES);
	meta->header.state = cpu_to_le32(DM_REMAP_META_CLEAN);
	meta->header.generation = 0;
	meta->header.total_remaps_created = 0;
	
	/* Initialize runtime state */
	meta->state = DM_REMAP_META_CLEAN;
	mutex_init(&meta->metadata_lock);
	
	/* Initialize statistics */
	atomic64_set(&meta->metadata_reads, 0);
	atomic64_set(&meta->metadata_writes, 0);
	atomic64_set(&meta->checksum_errors, 0);
	atomic_set(&meta->pending_writes, 0);
	
	/* Initialize auto-save system via separate function */
	if (dm_remap_autosave_init(meta) != 0) {
		DMREMAP_META_ERROR(meta, "Failed to initialize auto-save system");
		kfree(meta->entries);
		kfree(meta);
		return NULL;
	}
	
	/* Calculate initial checksum */
	dm_remap_metadata_calculate_checksum(meta);
	
	DMREMAP_META_INFO(meta, "Metadata context created successfully");
	return meta;
}

/*
 * Destroy metadata context and free resources
 */
void dm_remap_metadata_destroy(struct dm_remap_metadata *meta)
{
	if (!meta)
		return;
		
	DMREMAP_META_DEBUG(meta, "Destroying metadata context");
	
	/* Clean up auto-save system */
	dm_remap_autosave_cleanup(meta);
	
	/* Free resources */
	vfree(meta->entries);
	kfree(meta);
	
	DMREMAP_META_DEBUG(NULL, "Metadata context destroyed");
}

/*
 * Calculate CRC32 checksum for metadata
 */
void dm_remap_metadata_calculate_checksum(struct dm_remap_metadata *meta)
{
	u32 checksum = 0;
	u32 entry_count;
	
	if (!meta)
		return;
		
	entry_count = le32_to_cpu(meta->header.entry_count);
	
	/* Calculate checksum of header (excluding checksum field) */
	checksum = crc32(checksum, (u8*)&meta->header + sizeof(meta->header.checksum),
			sizeof(meta->header) - sizeof(meta->header.checksum));
	
	/* Add entries to checksum */
	if (entry_count > 0) {
		checksum = crc32(checksum, (u8*)meta->entries,
				entry_count * sizeof(struct dm_remap_entry));
	}
	
	meta->header.checksum = cpu_to_le32(checksum);
	
	DMREMAP_META_DEBUG(meta, "Calculated checksum: 0x%08x for %u entries",
			  checksum, entry_count);
}

/*
 * Validate metadata checksum and structure
 */
bool dm_remap_metadata_validate(struct dm_remap_metadata *meta)
{
	u32 stored_checksum, calculated_checksum = 0;
	u32 entry_count, version;
	
	if (!meta)
		return false;
		
	/* Check magic signature */
	if (memcmp(meta->header.magic, DM_REMAP_MAGIC, DM_REMAP_MAGIC_LEN) != 0) {
		DMREMAP_META_ERROR(meta, "Invalid magic signature");
		return false;
	}
	
	/* Check version */
	version = le32_to_cpu(meta->header.version);
	if (version != DM_REMAP_METADATA_VERSION) {
		DMREMAP_META_ERROR(meta, "Unsupported metadata version: %u", version);
		return false;
	}
	
	/* Check entry count bounds */
	entry_count = le32_to_cpu(meta->header.entry_count);
	if (entry_count > DM_REMAP_MAX_METADATA_ENTRIES) {
		DMREMAP_META_ERROR(meta, "Entry count %u exceeds maximum %zu",
				  entry_count, DM_REMAP_MAX_METADATA_ENTRIES);
		return false;
	}
	
	/* Validate checksum */
	stored_checksum = le32_to_cpu(meta->header.checksum);
	
	/* Calculate expected checksum */
	calculated_checksum = crc32(calculated_checksum,
				   (u8*)&meta->header + sizeof(meta->header.checksum),
				   sizeof(meta->header) - sizeof(meta->header.checksum));
	
	if (entry_count > 0) {
		calculated_checksum = crc32(calculated_checksum, (u8*)meta->entries,
					   entry_count * sizeof(struct dm_remap_entry));
	}
	
	if (stored_checksum != calculated_checksum) {
		DMREMAP_META_ERROR(meta, "Checksum mismatch: stored=0x%08x calculated=0x%08x",
				  stored_checksum, calculated_checksum);
		atomic64_inc(&meta->checksum_errors);
		return false;
	}
	
	DMREMAP_META_DEBUG(meta, "Metadata validation successful: %u entries, checksum=0x%08x",
			  entry_count, stored_checksum);
	return true;
}

/*
 * Add a remap entry to metadata
 */
enum dm_remap_metadata_result dm_remap_metadata_add_entry(struct dm_remap_metadata *meta,
							  sector_t main_sector,
							  sector_t spare_sector)
{
	u32 entry_count;
	struct dm_remap_entry *entry;
	
	if (!meta) {
		DMREMAP_META_ERROR(NULL, "Add entry failed: meta=NULL (main_sector=%llu, spare_sector=%llu)",
						  (unsigned long long)main_sector, (unsigned long long)spare_sector);
		return DM_REMAP_META_ERROR_CORRUPT;
	}
		
	dm_remap_metadata_lock(meta);
	
	entry_count = le32_to_cpu(meta->header.entry_count);
	
	/* Check if metadata is full */
	if (entry_count >= DM_REMAP_MAX_METADATA_ENTRIES) {
		DMREMAP_META_WARN(meta, "Metadata full: cannot add entry for sector %llu",
				 (unsigned long long)main_sector);
		dm_remap_metadata_unlock(meta);
		return DM_REMAP_META_ERROR_FULL;
	}
	
	/* Check for duplicate entries */
	for (u32 i = 0; i < entry_count; i++) {
		if (le64_to_cpu(meta->entries[i].main_sector) == main_sector) {
			DMREMAP_META_ERROR(meta, "Duplicate entry for sector %llu (spare=%llu)",
							  (unsigned long long)main_sector,
							  (unsigned long long)spare_sector);
			dm_remap_metadata_unlock(meta);
			return DM_REMAP_META_ERROR_CORRUPT;
		}
	}
	
	/* Add new entry */
	entry = &meta->entries[entry_count];
	entry->main_sector = cpu_to_le64(main_sector);
	entry->spare_sector = cpu_to_le64(spare_sector);
	
	/* Update header */
	meta->header.entry_count = cpu_to_le32(entry_count + 1);
	meta->header.last_update_time = cpu_to_le64(ktime_get_real_seconds());
	meta->header.generation = cpu_to_le32(le32_to_cpu(meta->header.generation) + 1);
	meta->header.total_remaps_created = cpu_to_le64(le64_to_cpu(meta->header.total_remaps_created) + 1);
	
	/* Mark as dirty */
	meta->state = DM_REMAP_META_DIRTY;
	meta->header.state = cpu_to_le32(DM_REMAP_META_DIRTY);
	
	/* Recalculate checksum */
	dm_remap_metadata_calculate_checksum(meta);
	
	DMREMAP_META_DEBUG(meta, "Added remap entry: %llu -> %llu (total: %u)",
			  (unsigned long long)main_sector,
			  (unsigned long long)spare_sector,
			  entry_count + 1);
	
	dm_remap_metadata_unlock(meta);
	
	/* Schedule async write if auto-save is enabled */
	if (meta->auto_save_enabled) {
		queue_work(system_wq, &meta->write_work);
	}
	
	return DM_REMAP_META_SUCCESS;
}

/*
 * Find a remap entry in metadata
 */
bool dm_remap_metadata_find_entry(struct dm_remap_metadata *meta,
				   sector_t main_sector,
				   sector_t *spare_sector)
{
	u32 entry_count;
	bool found = false;
	
	if (!meta || !spare_sector)
		return false;
		
	dm_remap_metadata_lock(meta);
	
	entry_count = le32_to_cpu(meta->header.entry_count);
	
	/* Linear search through entries */
	for (u32 i = 0; i < entry_count; i++) {
		if (le64_to_cpu(meta->entries[i].main_sector) == main_sector) {
			*spare_sector = le64_to_cpu(meta->entries[i].spare_sector);
			found = true;
			break;
		}
	}
	
	dm_remap_metadata_unlock(meta);
	
	DMREMAP_META_DEBUG(meta, "Entry lookup for sector %llu: %s",
			  (unsigned long long)main_sector,
			  found ? "found" : "not found");
	
	return found;
}

/*
 * Convert metadata result to human-readable string
 */
const char *dm_remap_metadata_result_string(enum dm_remap_metadata_result result)
{
	switch (result) {
	case DM_REMAP_META_SUCCESS:        return "Success";
	case DM_REMAP_META_ERROR_IO:       return "I/O Error";
	case DM_REMAP_META_ERROR_CHECKSUM: return "Checksum Error";
	case DM_REMAP_META_ERROR_VERSION:  return "Version Error";
	case DM_REMAP_META_ERROR_MAGIC:    return "Magic Error";
	case DM_REMAP_META_ERROR_FULL:     return "Metadata Full";
	case DM_REMAP_META_ERROR_CORRUPT:  return "Metadata Corrupt";
	default:                           return "Unknown Error";
	}
}

/*
 * End of dm-remap-metadata.c
 * I/O operations are implemented in dm-remap-io.c
 * Auto-save system is implemented in dm-remap-autosave.c
 */