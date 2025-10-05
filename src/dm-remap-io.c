/*
 * dm-remap-io.c - Metadata I/O operations for dm-remap v3.0
 *
 * This file implements the actual disk I/O operations for reading and writing
 * metadata to the spare device header.
 *
 * Copyright (C) 2025 dm-remap project
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/slab.h>

#include "dm-remap-metadata.h"

/*
 * I/O completion context for metadata operations
 */
struct dm_remap_metadata_io {
	struct completion completion;
	int error;
	struct dm_remap_metadata *meta;
};

/*
 * Bio completion callback for metadata I/O
 */
static void dm_remap_metadata_io_complete(struct bio *bio)
{
	struct dm_remap_metadata_io *io = bio->bi_private;
	
	io->error = blk_status_to_errno(bio->bi_status);
	complete(&io->completion);
	
	bio_put(bio);
}

/*
 * Perform synchronous metadata I/O operation
 */
static enum dm_remap_metadata_result dm_remap_metadata_io_sync(struct dm_remap_metadata *meta,
							      void *data,
							      size_t size,
							      sector_t sector,
							      int op)
{
	struct bio *bio;
	struct dm_remap_metadata_io io;
	struct page *page;
	
	if (!meta || !data || size == 0)
		return DM_REMAP_META_ERROR_CORRUPT;
		
	/* Initialize completion */
	init_completion(&io.completion);
	io.error = 0;
	io.meta = meta;
	
	/* Check if spare device is accessible */
	if (!meta->spare_bdev) {
		DMREMAP_META_ERROR(meta, "Spare device not available for metadata I/O");
		return DM_REMAP_META_ERROR_IO;
	}

	/* Allocate and setup bio */
	bio = bio_alloc(meta->spare_bdev, 1, op, GFP_KERNEL);
	if (!bio) {
		DMREMAP_META_ERROR(meta, "Failed to allocate bio for metadata I/O");
		return DM_REMAP_META_ERROR_IO;
	}
	
	bio->bi_iter.bi_sector = sector;
	bio->bi_private = &io;
	bio->bi_end_io = dm_remap_metadata_io_complete;
	
	/* Allocate page for data */
	page = alloc_page(GFP_KERNEL);
	if (!page) {
		DMREMAP_META_ERROR(meta, "Failed to allocate page for metadata I/O");
		bio_put(bio);
		return DM_REMAP_META_ERROR_IO;
	}
	
	/* Copy data to page for write operations */
	if (op == REQ_OP_WRITE) {
		memcpy(page_address(page), data, size);
	}
	
	/* Add page to bio */
	if (!bio_add_page(bio, page, size, 0)) {
		DMREMAP_META_ERROR(meta, "Failed to add page to bio");
		__free_page(page);
		bio_put(bio);
		return DM_REMAP_META_ERROR_IO;
	}
	
	/* Submit bio and wait for completion */
	submit_bio(bio);
	wait_for_completion(&io.completion);
	
	/* Copy data from page for read operations */
	if (op == REQ_OP_READ && io.error == 0) {
		memcpy(data, page_address(page), size);
	}
	
	/* Clean up */
	__free_page(page);
	
	/* Check for I/O errors */
	if (io.error) {
		DMREMAP_META_ERROR(meta, "Metadata I/O failed with error %d", io.error);
		return DM_REMAP_META_ERROR_IO;
	}
	
	DMREMAP_META_DEBUG(meta, "Metadata I/O completed successfully: %s %zu bytes at sector %llu",
			  (op == REQ_OP_READ) ? "read" : "write",
			  size, (unsigned long long)sector);
	
	return DM_REMAP_META_SUCCESS;
}

/*
 * Read metadata from spare device
 */
enum dm_remap_metadata_result dm_remap_metadata_read(struct dm_remap_metadata *meta)
{
	enum dm_remap_metadata_result result;
	u32 entry_count;
	size_t entries_size;
	
	if (!meta)
		return DM_REMAP_META_ERROR_CORRUPT;
		
	DMREMAP_META_DEBUG(meta, "Reading metadata from spare device");
	
	dm_remap_metadata_lock(meta);
	
	/* Read metadata header */
	result = dm_remap_metadata_io_sync(meta, &meta->header, sizeof(meta->header), 0, REQ_OP_READ);
	if (result != DM_REMAP_META_SUCCESS) {
		DMREMAP_META_ERROR(meta, "Failed to read metadata header");
		goto out_unlock;
	}
	
	atomic64_inc(&meta->metadata_reads);
	
	/* Validate header magic and version */
	if (memcmp(meta->header.magic, DM_REMAP_MAGIC, DM_REMAP_MAGIC_LEN) != 0) {
		DMREMAP_META_ERROR(meta, "Invalid metadata magic signature");
		result = DM_REMAP_META_ERROR_MAGIC;
		goto out_unlock;
	}
	
	if (le32_to_cpu(meta->header.version) != DM_REMAP_METADATA_VERSION) {
		DMREMAP_META_ERROR(meta, "Unsupported metadata version: %u",
				  le32_to_cpu(meta->header.version));
		result = DM_REMAP_META_ERROR_VERSION;
		goto out_unlock;
	}
	
	/* Get entry count and validate bounds */
	entry_count = le32_to_cpu(meta->header.entry_count);
	if (entry_count > DM_REMAP_MAX_METADATA_ENTRIES) {
		DMREMAP_META_ERROR(meta, "Entry count %u exceeds maximum %zu",
				  entry_count, DM_REMAP_MAX_METADATA_ENTRIES);
		result = DM_REMAP_META_ERROR_CORRUPT;
		goto out_unlock;
	}
	
	/* Read remap entries if any exist */
	if (entry_count > 0) {
		entries_size = entry_count * sizeof(struct dm_remap_entry);
		
		/* Read entries starting after the header */
		result = dm_remap_metadata_io_sync(meta, meta->entries, entries_size,
						  sizeof(meta->header) / DM_REMAP_METADATA_SECTOR_SIZE,
						  REQ_OP_READ);
		if (result != DM_REMAP_META_SUCCESS) {
			DMREMAP_META_ERROR(meta, "Failed to read remap entries");
			goto out_unlock;
		}
	}
	
	/* Validate checksum */
	if (!dm_remap_metadata_validate(meta)) {
		DMREMAP_META_ERROR(meta, "Metadata checksum validation failed");
		result = DM_REMAP_META_ERROR_CHECKSUM;
		goto out_unlock;
	}
	
	/* Update state */
	meta->state = DM_REMAP_META_CLEAN;
	
	DMREMAP_META_INFO(meta, "Successfully read metadata: %u entries, generation %u",
			 entry_count, le32_to_cpu(meta->header.generation));
	
	result = DM_REMAP_META_SUCCESS;

out_unlock:
	dm_remap_metadata_unlock(meta);
	return result;
}

/*
 * Write metadata to spare device
 */
enum dm_remap_metadata_result dm_remap_metadata_write(struct dm_remap_metadata *meta)
{
	enum dm_remap_metadata_result result;
	u32 entry_count;
	size_t entries_size;
	
	if (!meta)
		return DM_REMAP_META_ERROR_CORRUPT;
		
	DMREMAP_META_DEBUG(meta, "Writing metadata to spare device");
	
	dm_remap_metadata_lock(meta);
	
	/* Mark as writing state */
	meta->state = DM_REMAP_META_WRITING;
	meta->header.state = cpu_to_le32(DM_REMAP_META_WRITING);
	
	/* Update timestamps */
	meta->header.last_update_time = cpu_to_le64(ktime_get_real_seconds());
	
	/* Recalculate checksum */
	dm_remap_metadata_calculate_checksum(meta);
	
	/* Write metadata header */
	atomic_inc(&meta->pending_writes);
	result = dm_remap_metadata_io_sync(meta, &meta->header, sizeof(meta->header), 0, REQ_OP_WRITE);
	atomic_dec(&meta->pending_writes);
	
	if (result != DM_REMAP_META_SUCCESS) {
		DMREMAP_META_ERROR(meta, "Failed to write metadata header");
		meta->state = DM_REMAP_META_ERROR;
		goto out_unlock;
	}
	
	atomic64_inc(&meta->metadata_writes);
	
	/* Write remap entries if any exist */
	entry_count = le32_to_cpu(meta->header.entry_count);
	if (entry_count > 0) {
		entries_size = entry_count * sizeof(struct dm_remap_entry);
		
		atomic_inc(&meta->pending_writes);
		result = dm_remap_metadata_io_sync(meta, meta->entries, entries_size,
						  sizeof(meta->header) / DM_REMAP_METADATA_SECTOR_SIZE,
						  REQ_OP_WRITE);
		atomic_dec(&meta->pending_writes);
		
		if (result != DM_REMAP_META_SUCCESS) {
			DMREMAP_META_ERROR(meta, "Failed to write remap entries");
			meta->state = DM_REMAP_META_ERROR;
			goto out_unlock;
		}
	}
	
	/* Mark as clean */
	meta->state = DM_REMAP_META_CLEAN;
	meta->header.state = cpu_to_le32(DM_REMAP_META_CLEAN);
	
	/* Write final header with clean state */
	result = dm_remap_metadata_io_sync(meta, &meta->header, sizeof(meta->header), 0, REQ_OP_WRITE);
	if (result != DM_REMAP_META_SUCCESS) {
		DMREMAP_META_ERROR(meta, "Failed to write final clean state");
		meta->state = DM_REMAP_META_ERROR;
		goto out_unlock;
	}
	
	DMREMAP_META_INFO(meta, "Successfully wrote metadata: %u entries, generation %u",
			 entry_count, le32_to_cpu(meta->header.generation));
	
	result = DM_REMAP_META_SUCCESS;

out_unlock:
	dm_remap_metadata_unlock(meta);
	return result;
}

/*
 * Synchronize metadata to disk (force write if dirty)
 */
enum dm_remap_metadata_result dm_remap_metadata_sync(struct dm_remap_metadata *meta)
{
	if (!meta)
		return DM_REMAP_META_ERROR_CORRUPT;
		
	DMREMAP_META_DEBUG(meta, "Synchronizing metadata");
	
	if (dm_remap_metadata_is_dirty(meta)) {
		return dm_remap_metadata_write(meta);
	}
	
	DMREMAP_META_DEBUG(meta, "Metadata already clean, no sync needed");
	return DM_REMAP_META_SUCCESS;
}

/*
 * Attempt to recover corrupted metadata
 */
enum dm_remap_metadata_result dm_remap_metadata_recover(struct dm_remap_metadata *meta)
{
	if (!meta)
		return DM_REMAP_META_ERROR_CORRUPT;
		
	DMREMAP_META_INFO(meta, "Attempting metadata recovery");
	
	/* For now, just reinitialize metadata to clean state */
	dm_remap_metadata_lock(meta);
	
	/* Reset header to initial state */
	memcpy(meta->header.magic, DM_REMAP_MAGIC, DM_REMAP_MAGIC_LEN);
	meta->header.version = cpu_to_le32(DM_REMAP_METADATA_VERSION);
	meta->header.creation_time = cpu_to_le64(ktime_get_real_seconds());
	meta->header.last_update_time = meta->header.creation_time;
	meta->header.entry_count = 0;
	meta->header.state = cpu_to_le32(DM_REMAP_META_CLEAN);
	meta->header.generation = cpu_to_le32(le32_to_cpu(meta->header.generation) + 1);
	
	/* Clear entries */
	memset(meta->entries, 0, DM_REMAP_MAX_METADATA_ENTRIES * sizeof(struct dm_remap_entry));
	
	/* Update state */
	meta->state = DM_REMAP_META_DIRTY;
	
	/* Recalculate checksum */
	dm_remap_metadata_calculate_checksum(meta);
	
	dm_remap_metadata_unlock(meta);
	
	DMREMAP_META_INFO(meta, "Metadata recovery completed - reset to clean state");
	
	/* Write recovered metadata */
	return dm_remap_metadata_write(meta);
}