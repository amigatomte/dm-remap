#ifndef DMREMAP_STATUS_H
#define DMREMAP_STATUS_H

#include <time.h>

/**
 * Parsed dm-remap status output from kernel module
 * Corresponds to the 31-field output from dmsetup status
 */
typedef struct {
    /* Header info (positions 1-3) */
    unsigned long long start_sector;
    unsigned long long device_size_sectors;
    char target_type[64];
    
    /* Version & Devices (positions 4-6) */
    char version[64];
    char main_device[256];
    char spare_device[256];
    
    /* Basic I/O Statistics (positions 7-10) */
    unsigned long long total_reads;
    unsigned long long total_writes;
    unsigned long long total_remaps;
    unsigned long long total_errors;
    
    /* Active State (position 11) */
    unsigned int active_remaps;
    
    /* Performance Metrics (positions 12-15) */
    unsigned long long io_ops_completed;
    unsigned long long total_time_ns;
    unsigned long long avg_latency_ns;
    unsigned long long throughput_bps;
    
    /* Device Info (positions 16-17) */
    unsigned int sector_size;
    unsigned long long spare_capacity_sectors;
    
    /* Phase 1.3 Statistics (positions 18-21) */
    unsigned long long total_ios_phase13;
    unsigned long long normal_ios;
    unsigned long long remapped_ios;
    unsigned long long remapped_sectors;
    
    /* Phase 1.4 Cache Statistics (positions 22-25) */
    unsigned long long cache_hits;
    unsigned long long cache_misses;
    unsigned long long fast_path_hits;
    unsigned long long slow_path_hits;
    
    /* Health Monitoring (position 26) */
    unsigned long long health_scans;
    
    /* Health & Performance (positions 27-29) */
    unsigned int health_score;
    unsigned int hotspot_count;
    unsigned int cache_hit_rate_percent;
    
    /* Operational Info (positions 30-31) */
    char operational_state[32];
    char device_mode[32];
    
    /* Computed/Derived Fields */
    time_t timestamp;
    double avg_latency_us;      /* Average latency in microseconds */
    double throughput_mbps;     /* Throughput in MB/s */
    double spare_capacity_gb;   /* Spare capacity in GB */
    double device_size_gb;      /* Device size in GB */
    
} dm_remap_status_t;

/**
 * Output format options
 */
typedef enum {
    OUTPUT_HUMAN,       /* Pretty-printed for terminal */
    OUTPUT_JSON,        /* JSON format */
    OUTPUT_CSV,         /* CSV format */
    OUTPUT_RAW,         /* Raw kernel output */
    OUTPUT_COMPACT,     /* Single-line summary */
} output_format_t;

/* Parser functions */
int parse_dmremap_status(const char *raw_status, dm_remap_status_t *parsed);
int parse_dmremap_status_file(const char *filename, dm_remap_status_t *parsed);

/* Formatter functions */
int print_human_readable(const dm_remap_status_t *status, FILE *out);
int print_json(const dm_remap_status_t *status, FILE *out);
int print_csv(const dm_remap_status_t *status, FILE *out);
int print_compact(const dm_remap_status_t *status, FILE *out);

/* Helper functions */
const char *get_health_status_string(unsigned int score);
const char *get_performance_rating(unsigned long long latency_ns);
const char *get_cache_rating(unsigned int hit_rate);
void compute_derived_fields(dm_remap_status_t *status);

#endif /* DMREMAP_STATUS_H */
