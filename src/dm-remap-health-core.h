/*
 * dm-remap-health-core.h - Core health scanning data structures and definitions
 * 
 * Background Health Scanning System for dm-remap v4.0
 * Provides proactive storage health monitoring with predictive failure analysis
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_HEALTH_CORE_H
#define DM_REMAP_HEALTH_CORE_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/time.h>

/* Forward declarations */
struct remap_c;

/* Health scanning configuration constants */
#define DMR_HEALTH_SCAN_DEFAULT_INTERVAL_MS    60000    /* 60 seconds */
#define DMR_HEALTH_SCAN_MIN_INTERVAL_MS        5000     /* 5 seconds */
#define DMR_HEALTH_SCAN_MAX_INTERVAL_MS        3600000  /* 1 hour */

#define DMR_HEALTH_SECTORS_PER_SCAN_DEFAULT    1000     /* 1000 sectors per scan */
#define DMR_HEALTH_SECTORS_PER_SCAN_MIN        100      /* Minimum sectors per scan */
#define DMR_HEALTH_SECTORS_PER_SCAN_MAX        10000    /* Maximum sectors per scan */

#define DMR_HEALTH_SCAN_INTENSITY_DEFAULT      3        /* Medium intensity */
#define DMR_HEALTH_SCAN_INTENSITY_MIN          1        /* Light scanning */
#define DMR_HEALTH_SCAN_INTENSITY_MAX          10       /* Aggressive scanning */

#define DMR_HEALTH_SCORE_PERFECT               1000     /* Perfect health score */
#define DMR_HEALTH_SCORE_WARNING_THRESHOLD     700      /* Warning threshold */
#define DMR_HEALTH_SCORE_DANGER_THRESHOLD      300      /* Danger threshold */

/* Health risk levels */
enum dmr_health_risk_level {
    DMR_HEALTH_RISK_SAFE = 0,        /* Sector is healthy */
    DMR_HEALTH_RISK_MONITOR = 1,     /* Monitor for changes */
    DMR_HEALTH_RISK_CAUTION = 2,     /* Caution - degrading */
    DMR_HEALTH_RISK_DANGER = 3       /* Danger - likely to fail */
};

/* Health trend indicators */
enum dmr_health_trend {
    DMR_HEALTH_TREND_IMPROVING = 0,  /* Health improving */
    DMR_HEALTH_TREND_STABLE = 1,     /* Health stable */
    DMR_HEALTH_TREND_DECLINING = 2   /* Health declining */
};

/* Scanner state */
enum dmr_scanner_state {
    DMR_SCANNER_STOPPED = 0,         /* Scanner not running */
    DMR_SCANNER_STARTING = 1,        /* Scanner starting up */
    DMR_SCANNER_RUNNING = 2,         /* Scanner actively running */
    DMR_SCANNER_PAUSED = 3,          /* Scanner paused */
    DMR_SCANNER_STOPPING = 4         /* Scanner shutting down */
};

/**
 * struct dmr_sector_health - Per-sector health tracking information
 * 
 * This structure tracks the health status and history of individual sectors
 * on the main storage device for predictive failure analysis.
 */
struct dmr_sector_health {
    u16 health_score;            /* Current health score (0-1000) */
    u16 read_errors;            /* Cumulative read error count */
    u16 write_errors;           /* Cumulative write error count */
    u32 access_count;           /* Total access attempts */
    u64 last_scan_time;         /* Last health scan timestamp (jiffies) */
    u64 last_access_time;       /* Last I/O access timestamp (jiffies) */
    u8 trend;                   /* Health trend (dmr_health_trend) */
    u8 risk_level;              /* Risk level (dmr_health_risk_level) */
    u8 scan_count;              /* Number of scans performed */
    u8 reserved;                /* Reserved for future use */
} __packed;

/**
 * struct dmr_health_map - Health tracking map for all sectors
 * 
 * Manages health information for all sectors on the main device.
 * Uses a sparse representation to minimize memory usage.
 */
struct dmr_health_map {
    sector_t total_sectors;      /* Total sectors being monitored */
    sector_t tracked_sectors;    /* Number of sectors with health data */
    struct dmr_sector_health *health_data;  /* Health data array */
    unsigned long *tracking_bitmap;         /* Bitmap of tracked sectors */
    spinlock_t health_lock;      /* Protects health data updates */
    atomic_t updates_pending;    /* Pending health updates */
};

/**
 * struct dmr_failure_prediction - Failure prediction information
 * 
 * Contains predictive analysis results for potential sector failures.
 */
struct dmr_failure_prediction {
    u32 failure_probability;    /* Failure probability (0-100%) */
    u64 estimated_failure_time; /* Predicted failure time (jiffies) */
    u8 confidence_level;        /* Prediction confidence (0-100%) */
    u8 severity;               /* Failure severity (1-10) */
    char reason[64];           /* Human-readable failure reason */
};

/**
 * struct dmr_health_stats - Health scanning statistics
 * 
 * Tracks statistics and performance metrics for the health scanning system.
 */
struct dmr_health_stats {
    atomic64_t total_scans;           /* Total scans completed */
    atomic64_t sectors_scanned;       /* Total sectors scanned */
    atomic64_t warnings_issued;       /* Health warnings issued */
    atomic64_t predictions_made;      /* Failure predictions made */
    atomic64_t scan_time_total_ns;    /* Total time spent scanning (ns) */
    atomic_t active_warnings;         /* Currently active warnings */
    atomic_t high_risk_sectors;       /* Sectors at high risk */
    u64 last_full_scan_time;         /* Last complete scan timestamp */
    u32 scan_coverage_percent;       /* Percentage of device scanned */
};

/**
 * struct dmr_health_scanner - Main health scanning engine
 * 
 * Manages background health scanning operations and coordinates with
 * the existing dm-remap infrastructure.
 */
struct dmr_health_scanner {
    /* Core infrastructure */
    struct remap_c *rc;              /* Parent remap context */
    struct workqueue_struct *scan_workqueue;  /* Dedicated scan workqueue */
    struct delayed_work scan_work;   /* Periodic scanning work */
    
    /* Scanner configuration */
    unsigned long scan_interval_ms;  /* Scanning frequency */
    sector_t sectors_per_scan;       /* Sectors scanned per cycle */
    u8 scan_intensity;               /* Scanning intensity (1-10) */
    u8 scanner_state;                /* Current scanner state */
    bool enabled;                    /* Scanner enable/disable flag */
    
    /* Health tracking */
    struct dmr_health_map *health_map;     /* Sector health map */
    struct dmr_health_stats stats;         /* Scanning statistics */
    
    /* Scanning progress */
    sector_t scan_cursor;            /* Current scan position */
    sector_t scan_start_sector;      /* Scan range start */
    sector_t scan_end_sector;        /* Scan range end */
    
    /* Performance monitoring */
    ktime_t last_scan_start;         /* Last scan start time */
    ktime_t last_scan_end;           /* Last scan end time */
    u64 io_overhead_ns;              /* I/O overhead measurement */
    
    /* Synchronization */
    spinlock_t scanner_lock;         /* Protects scanner state */
    struct mutex config_mutex;       /* Configuration changes mutex */
    
    /* Sysfs integration */
    struct kobject *health_kobj;     /* Health sysfs directory */
};

/* Function declarations */
int dmr_health_scanner_init(struct remap_c *rc);
void dmr_health_scanner_cleanup(struct remap_c *rc);
int dmr_health_scanner_start(struct dmr_health_scanner *scanner);
int dmr_health_scanner_stop(struct dmr_health_scanner *scanner);
int dmr_health_scanner_pause(struct dmr_health_scanner *scanner);
int dmr_health_scanner_resume(struct dmr_health_scanner *scanner);

/* Health assessment functions */
u16 dmr_calculate_health_score(struct dmr_sector_health *health);
int dmr_health_update_sector(struct dmr_health_scanner *scanner, 
                            sector_t sector, bool read_success, bool write_success);
int dmr_predict_sector_failure(struct dmr_health_scanner *scanner,
                              sector_t sector, struct dmr_failure_prediction *prediction);

/* Health map management */
int dmr_health_map_init(struct dmr_health_map **health_map, sector_t total_sectors);
void dmr_health_map_cleanup(struct dmr_health_map *health_map);
struct dmr_sector_health *dmr_get_sector_health(struct dmr_health_map *health_map, 
                                               sector_t sector);
int dmr_set_sector_health(struct dmr_health_map *health_map, sector_t sector,
                         struct dmr_sector_health *health);

/* Extended health map functions */
int dmr_health_map_get_stats(struct dmr_health_map *health_map,
                            sector_t *total_tracked, size_t *memory_used);
int dmr_health_map_iterate(struct dmr_health_map *health_map,
                          int (*callback)(sector_t sector, 
                                        struct dmr_sector_health *health,
                                        void *context),
                          void *context);
int dmr_health_map_clear_sector(struct dmr_health_map *health_map, sector_t sector);
int dmr_health_map_compact(struct dmr_health_map *health_map);
void dmr_health_map_debug_dump(struct dmr_health_map *health_map, int max_entries);

/* Statistics and reporting */
void dmr_health_get_stats(struct dmr_health_scanner *scanner, 
                         struct dmr_health_stats *stats);
int dmr_health_generate_report(struct dmr_health_scanner *scanner, 
                              char *buffer, size_t buffer_size);

/* Sysfs interface functions */
int dmr_health_sysfs_init(struct dmr_health_scanner *scanner);
void dmr_health_sysfs_cleanup(struct dmr_health_scanner *scanner);

/* Predictive analysis functions */
int dmr_predict_sector_failure(struct dmr_health_scanner *scanner,
                              sector_t sector, struct dmr_failure_prediction *prediction);
int dmr_health_risk_assessment(struct dmr_health_scanner *scanner,
                              struct dmr_failure_prediction *assessment_results,
                              int max_results);
int dmr_health_trend_monitor(struct dmr_health_scanner *scanner);
void dmr_health_prediction_cleanup(struct dmr_health_scanner *scanner);

#endif /* DM_REMAP_HEALTH_CORE_H */