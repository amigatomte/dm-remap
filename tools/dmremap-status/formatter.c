#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dmremap_status.h"

/* ANSI color codes */
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_RED       "\x1b[31m"
#define COLOR_CYAN      "\x1b[36m"
#define COLOR_BOLD      "\x1b[1m"
#define COLOR_RESET     "\x1b[0m"

/**
 * Check if output is a terminal (for color output)
 */
static int is_terminal(FILE *out)
{
    return isatty(fileno(out));
}

/**
 * Helper to print colored status indicator
 */
static const char *get_status_indicator(int is_good)
{
    return is_good ? COLOR_GREEN "✓" COLOR_RESET : COLOR_RED "✗" COLOR_RESET;
}

/**
 * Helper to print a section header
 */
static void print_section_header(FILE *out, const char *title, int use_color)
{
    if (use_color) {
        fprintf(out, COLOR_CYAN "├─ " COLOR_BOLD "%s" COLOR_RESET "\n", title);
    } else {
        fprintf(out, "├─ %s\n", title);
    }
}

/**
 * Helper to print a key-value pair
 */
static void print_kv_pair(FILE *out, const char *key, const char *value, 
                          const char *indicator, int use_color)
{
    if (use_color && indicator) {
        fprintf(out, "│  %-28s %s  %s\n", key, indicator, value);
    } else {
        fprintf(out, "│  %-28s %s\n", key, value);
    }
}

/**
 * Helper to print device info line
 */
static void print_device_line(FILE *out, const char *label, const char *device, int use_color)
{
    if (use_color) {
        fprintf(out, "│  " COLOR_BOLD "%-26s" COLOR_RESET ": %s\n", label, device);
    } else {
        fprintf(out, "│  %-26s: %s\n", label, device);
    }
}

/**
 * Print human-readable formatted status
 */
int print_human_readable(const dm_remap_status_t *status, FILE *out)
{
    if (!status || !out) {
        fprintf(stderr, "Error: NULL pointer passed to print_human_readable\n");
        return -1;
    }

    int use_color = is_terminal(out);
    char indicator[32];

    /* Header */
    if (use_color) {
        fprintf(out, COLOR_BOLD "┌───────────────────────────────────────────────────────────────────┐" COLOR_RESET "\n");
        fprintf(out, COLOR_BOLD "│" COLOR_CYAN "                    dm-remap Status                           " COLOR_BOLD "│" COLOR_RESET "\n");
        fprintf(out, COLOR_BOLD "├───────────────────────────────────────────────────────────────────┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "┌───────────────────────────────────────────────────────────────────┐\n");
        fprintf(out, "│                    dm-remap Status                               │\n");
        fprintf(out, "├───────────────────────────────────────────────────────────────────┤\n");
    }

    /* Basic Information */
    fprintf(out, "│  Device Version        : %s\n", status->version);
    fprintf(out, "│  Device Size           : %.1f GB (%llu sectors)\n", 
            status->device_size_gb, status->device_size_sectors);
    fprintf(out, "│\n");

    /* Device Configuration */
    print_device_line(out, "Main Device", status->main_device, use_color);
    print_device_line(out, "Spare Device", status->spare_device, use_color);
    print_device_line(out, "Spare Capacity", 
                     use_color ? COLOR_GREEN "Available" COLOR_RESET : "Available", use_color);
    fprintf(out, "│    └─ %.1f GB (%llu sectors)\n", 
            status->spare_capacity_gb, status->spare_capacity_sectors);

    if (use_color) {
        fprintf(out, COLOR_BOLD "├───────────────────────────────────────────────────────────────────┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├───────────────────────────────────────────────────────────────────┤\n");
    }

    /* PERFORMANCE */
    print_section_header(out, "PERFORMANCE", use_color);
    
    const char *perf_rating = get_performance_rating(status->avg_latency_ns);
    snprintf(indicator, sizeof(indicator), "[%s%s%s]", 
             use_color ? COLOR_GREEN : "", perf_rating, use_color ? COLOR_RESET : "");
    fprintf(out, "│  %-28s %.1f μs  %s\n", 
            "Avg Latency", status->avg_latency_us, indicator);

    fprintf(out, "│  %-28s %.0f MB/s\n", 
            "Throughput", status->throughput_mbps);

    fprintf(out, "│  %-28s %llu completed\n", 
            "I/O Operations", status->io_ops_completed);

    /* HEALTH */
    if (use_color) {
        fprintf(out, COLOR_BOLD "├───────────────────────────────────────────────────────────────────┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├───────────────────────────────────────────────────────────────────┤\n");
    }

    print_section_header(out, "HEALTH", use_color);

    const char *health_status = get_health_status_string(status->health_score);
    const char *health_color = status->health_score >= 80 ? COLOR_GREEN : 
                               status->health_score >= 60 ? COLOR_YELLOW : COLOR_RED;
    snprintf(indicator, sizeof(indicator), "[%s%s%s %d/100]",
             use_color ? health_color : "", health_status, 
             use_color ? COLOR_RESET : "", status->health_score);
    fprintf(out, "│  %-28s %s\n", "Health Score", indicator);

    fprintf(out, "│  %-28s %s\n", "Operational State",
            status->operational_state);

    fprintf(out, "│  %-28s %llu\n", "Errors", status->total_errors);

    const char *hotspot_indicator = status->hotspot_count > 0 ? 
                                   (use_color ? COLOR_YELLOW "⚠" COLOR_RESET : "!") : 
                                   (use_color ? COLOR_GREEN "✓" COLOR_RESET : "");
    fprintf(out, "│  %-28s %u %s\n", "Hotspots", status->hotspot_count, hotspot_indicator);

    /* REMAPPING */
    if (use_color) {
        fprintf(out, COLOR_BOLD "├───────────────────────────────────────────────────────────────────┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├───────────────────────────────────────────────────────────────────┤\n");
    }

    print_section_header(out, "REMAPPING", use_color);

    fprintf(out, "│  %-28s %u\n", "Active Remaps", status->active_remaps);

    double remap_percent = status->total_ios_phase13 > 0 ? 
                          (status->remapped_ios * 100.0 / status->total_ios_phase13) : 0.0;
    fprintf(out, "│  %-28s %llu / %llu (%.1f%%)\n",
            "Remapped I/O", status->remapped_ios, status->total_ios_phase13, remap_percent);

    fprintf(out, "│  %-28s %llu\n", "Remapped Sectors", status->remapped_sectors);

    fprintf(out, "│  %-28s %llu reads, %llu writes\n",
            "Total I/O", status->total_reads, status->total_writes);

    /* CACHE */
    if (use_color) {
        fprintf(out, COLOR_BOLD "├───────────────────────────────────────────────────────────────────┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├───────────────────────────────────────────────────────────────────┤\n");
    }

    print_section_header(out, "CACHE PERFORMANCE", use_color);

    unsigned long long total_lookups = status->cache_hits + status->cache_misses;
    const char *cache_rating = get_cache_rating(status->cache_hit_rate_percent);
    const char *cache_color = status->cache_hit_rate_percent >= 60 ? COLOR_GREEN :
                             status->cache_hit_rate_percent >= 40 ? COLOR_YELLOW : COLOR_RED;
    snprintf(indicator, sizeof(indicator), "[%s%s%s]",
             use_color ? cache_color : "", cache_rating, use_color ? COLOR_RESET : "");
    fprintf(out, "│  %-28s %llu / %llu (%u%%)  %s\n",
            "Cache Hit Rate", status->cache_hits, total_lookups, 
            status->cache_hit_rate_percent, indicator);

    double fast_path_percent = total_lookups > 0 ? 
                              (status->fast_path_hits * 100.0 / total_lookups) : 0.0;
    fprintf(out, "│  %-28s %llu / %llu (%.1f%%)\n",
            "Fast Path Hits", status->fast_path_hits, total_lookups, fast_path_percent);

    double slow_path_percent = total_lookups > 0 ? 
                              (status->slow_path_hits * 100.0 / total_lookups) : 0.0;
    fprintf(out, "│  %-28s %llu / %llu (%.1f%%)\n",
            "Slow Path Hits", status->slow_path_hits, total_lookups, slow_path_percent);

    /* Footer */
    if (use_color) {
        fprintf(out, COLOR_BOLD "└───────────────────────────────────────────────────────────────────┘" COLOR_RESET "\n");
    } else {
        fprintf(out, "└───────────────────────────────────────────────────────────────────┘\n");
    }

    return 0;
}

/**
 * Print compact single-line status
 */
int print_compact(const dm_remap_status_t *status, FILE *out)
{
    if (!status || !out) {
        fprintf(stderr, "Error: NULL pointer passed to print_compact\n");
        return -1;
    }

    fprintf(out, "%s: health=%u%% latency=%.1fμs throughput=%.0fMB/s cache=%u%% errors=%llu hotspots=%u\n",
            status->version,
            status->health_score,
            status->avg_latency_us,
            status->throughput_mbps,
            status->cache_hit_rate_percent,
            status->total_errors,
            status->hotspot_count);

    return 0;
}
