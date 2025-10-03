/*
 * dm-remap-metadata.h - Persistent metadata structures for dm-remap v3.0
 *
 * This header defines the on-disk metadata format for persistent remap tables.
 * The metadata is stored in the first 4KB of the spare device.
 *
 * Copyright (C) 2025 dm-remap project
 */

#ifndef DM_REMAP_METADATA_H
#define DM_REMAP_METADATA_H

#include <linux/types.h>
#include <linux/crc32.h>

/*
 * Metadata format version - increment when changing structure
 */
#define DM_REMAP_METADATA_VERSION 1

/*
 * Magic signature for metadata header
 */
#define DM_REMAP_MAGIC "DMREMAP3"
#define DM_REMAP_MAGIC_LEN 8

/*
 * Metadata layout constants
 */
#define DM_REMAP_METADATA_SECTOR_SIZE 512
#define DM_REMAP_METADATA_SECTORS 8  /* 4KB total */
#define DM_REMAP_METADATA_SIZE (DM_REMAP_METADATA_SECTORS * DM_REMAP_METADATA_SECTOR_SIZE)

/*
 * Maximum number of remap entries that fit in metadata
 * Header takes ~64 bytes, rest available for remap entries
 */
#define DM_REMAP_MAX_METADATA_ENTRIES ((DM_REMAP_METADATA_SIZE - 64) / sizeof(struct dm_remap_entry))

/*
 * Metadata states
 */
enum dm_remap_metadata_state {
	DM_REMAP_META_CLEAN = 0,    /* Metadata is consistent */
	DM_REMAP_META_DIRTY,        /* Metadata needs writing */
	DM_REMAP_META_WRITING,      /* Metadata write in progress */
	DM_REMAP_META_ERROR         /* Metadata corruption detected */
};

/*
 * On-disk remap entry (16 bytes)
 * 
 * This structure represents a single sector remap stored in metadata.
 * It maps a main device sector to a spare device sector.
 */
struct dm_remap_entry {
	__le64 main_sector;         /* Original sector on main device */
	__le64 spare_sector;        /* Replacement sector on spare device */
} __packed;

/*
 * On-disk metadata header (64 bytes + padding to 4KB)
 *
 * This header is stored at the beginning of the spare device and contains
 * all information needed to restore the remap table after a reboot.
 */
struct dm_remap_metadata_header {
	/* Identification and versioning */
	char magic[DM_REMAP_MAGIC_LEN];    /* "DMREMAP3" signature */
	__le32 version;                    /* Metadata format version */
	__le32 checksum;                   /* CRC32 of header + entries */
	
	/* Timestamps for tracking */
	__le64 creation_time;              /* When metadata was first created */
	__le64 last_update_time;           /* Last modification timestamp */
	
	/* Remap table information */
	__le32 entry_count;                /* Number of remap entries */
	__le32 max_entries;                /* Maximum entries this metadata can hold */
	
	/* Device identification */
	__le64 main_device_size;           /* Size of main device in sectors */
	__le64 spare_device_size;          /* Size of spare device in sectors */
	
	/* Status and statistics */
	__le32 state;                      /* Current metadata state */
	__le32 generation;                 /* Incremented on each update */
	__le64 total_remaps_created;       /* Lifetime remap count */
	
	/* Reserved for future use */
	__u8 reserved[DM_REMAP_METADATA_SIZE - 64];
} __packed;

/*
 * In-memory metadata context
 *
 * This structure maintains the runtime state of the metadata system.
 * It includes both the on-disk data and runtime management information.
 */
struct dm_remap_metadata {
	/* Device references */
	struct block_device *spare_bdev;   /* Spare device for metadata storage */
	
	/* In-memory copy of metadata */
	struct dm_remap_metadata_header header;
	struct dm_remap_entry *entries;    /* Array of remap entries */
	
	/* Runtime state */
	enum dm_remap_metadata_state state;
	struct mutex metadata_lock;        /* Protects metadata operations */
	
	/* I/O and synchronization */
	struct work_struct write_work;     /* Async metadata write work */
	atomic_t pending_writes;           /* Number of pending write operations */
	
	/* Statistics */
	atomic64_t metadata_reads;         /* Number of metadata read operations */
	atomic64_t metadata_writes;        /* Number of metadata write operations */
	atomic64_t checksum_errors;        /* Number of checksum validation failures */
	
	/* Configuration */
	bool auto_save_enabled;            /* Automatic metadata saving */
	unsigned int save_interval;       /* Seconds between automatic saves */
	struct timer_list save_timer;      /* Timer for periodic saves */
};

/*
 * Metadata operation results
 */
enum dm_remap_metadata_result {
	DM_REMAP_META_SUCCESS = 0,         /* Operation completed successfully */
	DM_REMAP_META_ERROR_IO,            /* I/O error during operation */
	DM_REMAP_META_ERROR_CHECKSUM,      /* Checksum validation failed */
	DM_REMAP_META_ERROR_VERSION,       /* Unsupported metadata version */
	DM_REMAP_META_ERROR_MAGIC,         /* Invalid magic signature */
	DM_REMAP_META_ERROR_FULL,          /* Metadata storage is full */
	DM_REMAP_META_ERROR_CORRUPT        /* Metadata is corrupted */
};

/*
 * Function prototypes for metadata operations
 */

/* Metadata lifecycle */
struct dm_remap_metadata *dm_remap_metadata_create(struct block_device *spare_bdev,
						   sector_t main_size,
						   sector_t spare_size);
void dm_remap_metadata_destroy(struct dm_remap_metadata *meta);

/* Metadata I/O operations */
enum dm_remap_metadata_result dm_remap_metadata_read(struct dm_remap_metadata *meta);
enum dm_remap_metadata_result dm_remap_metadata_write(struct dm_remap_metadata *meta);
enum dm_remap_metadata_result dm_remap_metadata_sync(struct dm_remap_metadata *meta);

/* Remap entry management */
enum dm_remap_metadata_result dm_remap_metadata_add_entry(struct dm_remap_metadata *meta,
							  sector_t main_sector,
							  sector_t spare_sector);
enum dm_remap_metadata_result dm_remap_metadata_remove_entry(struct dm_remap_metadata *meta,
							     sector_t main_sector);
bool dm_remap_metadata_find_entry(struct dm_remap_metadata *meta,
				   sector_t main_sector,
				   sector_t *spare_sector);

/* Validation and recovery */
bool dm_remap_metadata_validate(struct dm_remap_metadata *meta);
enum dm_remap_metadata_result dm_remap_metadata_recover(struct dm_remap_metadata *meta);

/* Utility functions */
void dm_remap_metadata_calculate_checksum(struct dm_remap_metadata *meta);
const char *dm_remap_metadata_result_string(enum dm_remap_metadata_result result);

/*
 * Helper macros for metadata operations
 */
#define dm_remap_metadata_lock(meta) mutex_lock(&(meta)->metadata_lock)
#define dm_remap_metadata_unlock(meta) mutex_unlock(&(meta)->metadata_lock)

#define dm_remap_metadata_is_dirty(meta) ((meta)->state == DM_REMAP_META_DIRTY)
#define dm_remap_metadata_is_clean(meta) ((meta)->state == DM_REMAP_META_CLEAN)
#define dm_remap_metadata_is_error(meta) ((meta)->state == DM_REMAP_META_ERROR)

/*
 * Debug and logging helpers
 */
#define DMREMAP_META_DEBUG(meta, fmt, args...) \
	pr_debug("dm-remap-meta: " fmt "\n", ##args)

#define DMREMAP_META_INFO(meta, fmt, args...) \
	pr_info("dm-remap-meta: " fmt "\n", ##args)

#define DMREMAP_META_WARN(meta, fmt, args...) \
	pr_warn("dm-remap-meta: " fmt "\n", ##args)

#define DMREMAP_META_ERROR(meta, fmt, args...) \
	pr_err("dm-remap-meta: " fmt "\n", ##args)

#endif /* DM_REMAP_METADATA_H */