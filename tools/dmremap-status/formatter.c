#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "dmremap_status.h"

/* ANSI color codes */
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_RED       "\x1b[31m"
#define COLOR_CYAN      "\x1b[36m"
#define COLOR_BOLD      "\x1b[1m"
#define COLOR_RESET     "\x1b[0m"

#define MAX_DEVICE_STR   200
#define MAX_SYSFS_NAME   200

/**
 * Check if output is a terminal (for color output)
 */
static int is_terminal(FILE *out)
{
    return isatty(fileno(out));
}

/* Removed older unused helper functions to reduce warnings */

/**
 * Calculate display width of UTF-8 string, skipping ANSI escape codes
 * Handles common multi-byte characters correctly
 */
static int utf8_display_width(const char *str)
{
    if (!str) return 0;
    int width = 0;
    while (*str) {
        unsigned char c = (unsigned char)*str;
        // Skip ANSI escape sequences: ESC [ ... m
        if (c == '\x1b' && *(str + 1) == '[') {
            str += 2;
            while (*str && *str != 'm') str++;
            if (*str == 'm') str++;
            continue;
        }
        // Treat box drawing and common symbols as single width
        if ((c & 0x80) == 0) { // ASCII
            width++;
            str++;
            continue;
        }
        int advance = 1; // bytes consumed
        int char_width = 1; // display width
        if ((c & 0xE0) == 0xC0) {
            advance = 2;
        } else if ((c & 0xF0) == 0xE0) {
            advance = 3;
        } else if ((c & 0xF8) == 0xF0) {
            advance = 4;
            // Heuristic: treat 4-byte (emoji) as width 2
            char_width = 2;
        }
        width += char_width;
        str += advance;
    }
    return width;
}

/**
 * Print a formatted line with dynamic padding so total stripped line length == 73.
 * (73 chars including both border glyphs; content width target = 71)
 */
static void print_boxed_line(FILE *out, const char *content, int use_color_border)
{
    /* Choose target width based on border line length (excluding ANSI): 77 observed. */
           const int TOTAL_LINE = 77;        /* including both border characters */
    const int CONTENT_TARGET = TOTAL_LINE - 2; /* interior width */
    int display_width = utf8_display_width(content);
    int padding = CONTENT_TARGET - display_width;
    if (padding < 0) padding = 0;
    if (use_color_border) {
        fprintf(out, COLOR_CYAN "│" COLOR_RESET "%s%*s" COLOR_CYAN "│" COLOR_RESET "\n", content, padding, "");
    } else {
        fprintf(out, "│%s%*s│\n", content, padding, "");
    }
}

/* Header / section helper with automatic padding */
static void print_section_box(FILE *out, const char *title, int use_color_border)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "─ %s", title); /* leading marker */
    print_boxed_line(out, buf, use_color_border);
}

static void print_centered_boxed_line(FILE *out, const char *content, int use_color_border)
{
    const int TOTAL_LINE = 77;
    const int CONTENT_TARGET = TOTAL_LINE - 2;
    int display_width = utf8_display_width(content);
    int total_padding = CONTENT_TARGET - display_width;
    if (total_padding < 0) total_padding = 0;
    int left_padding = total_padding / 2;
    int right_padding = total_padding - left_padding;
    if (use_color_border) {
        fprintf(out, COLOR_CYAN "│" COLOR_RESET "%*s%s%*s" COLOR_CYAN "│" COLOR_RESET "\n",
                left_padding, "", content, right_padding, "");
    } else {
        fprintf(out, "│%*s%s%*s│\n", left_padding, "", content, right_padding, "");
    }
}

/* Read actual block device size (in 512-byte sectors) from sysfs. Returns 0 on failure */
static unsigned long long get_device_size_sectors(const char *device_path)
{
    if (!device_path) return 0ULL;
    const char *base = strrchr(device_path, '/');
    base = base ? base + 1 : device_path; /* basename */
    char sysfs_path[256];
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%.*s/size", MAX_SYSFS_NAME, base);
    FILE *f = fopen(sysfs_path, "r");
    if (!f) return 0ULL;
    unsigned long long sectors = 0ULL;
    if (fscanf(f, "%llu", &sectors) != 1) {
        sectors = 0ULL;
    }
    fclose(f);
    return sectors;
}

/* If loop device, try to read backing file and stat it for a more precise size */
static unsigned long long get_loop_backing_file_sectors(const char *device_path)
{
    if (!device_path) return 0ULL;
    const char *base = strrchr(device_path, '/');
    base = base ? base + 1 : device_path; /* basename */
    char backing_path[256];
    snprintf(backing_path, sizeof(backing_path), "/sys/class/block/%.*s/loop/backing_file", MAX_SYSFS_NAME, base);
    FILE *f = fopen(backing_path, "r");
    if (!f) return 0ULL;
    char file_path[512];
    if (!fgets(file_path, sizeof(file_path), f)) {
        fclose(f);
        return 0ULL;
    }
    fclose(f);
    /* Strip newline */
    size_t len = strlen(file_path);
    if (len && file_path[len-1] == '\n') file_path[len-1] = '\0';
    struct stat st;
    if (stat(file_path, &st) != 0) {
        return 0ULL;
    }
    /* Convert bytes to 512-byte sectors */
    return (unsigned long long)(st.st_size / 512ULL);
}

static void format_size(char *buf, size_t buflen, unsigned long long sectors)
{
    if (!buf || buflen == 0) return;
    double bytes = (double)sectors * 512.0;
    double mib = bytes / (1024.0 * 1024.0);
    double gib = bytes / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1.0) {
        snprintf(buf, buflen, "%.1f GB", gib);
    } else {
        if (mib >= 100.0)
            snprintf(buf, buflen, "%.0f MB", mib);
        else
            snprintf(buf, buflen, "%.1f MB", mib);
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
    char line[256];
    char indicator[64];

    /* Header */
    const int CONTENT_TARGET = 75; /* matches TOTAL_LINE - 2 inside print_boxed_line */
    if (use_color) {
        fprintf(out, COLOR_CYAN "┌");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┐" COLOR_RESET "\n");
    print_centered_boxed_line(out, COLOR_BOLD "dm-remap Status" COLOR_RESET, 1);
        fprintf(out, COLOR_CYAN "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "┌");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┐\n");
    print_centered_boxed_line(out, "dm-remap Status", 0);
        fprintf(out, "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤\n");
    }

    /* Basic Information */
    snprintf(line, sizeof(line), "  Device Version        : %.*s", MAX_DEVICE_STR, status->version);
    print_boxed_line(out, line, use_color);
    
    snprintf(line, sizeof(line), "  Device Size           : %.1f GB (%llu sectors)", 
             status->device_size_gb, status->device_size_sectors);
    print_boxed_line(out, line, use_color);
    
    print_boxed_line(out, "", use_color);

    /* Device Configuration */
    snprintf(line, sizeof(line), "  Main Device           : %.*s", MAX_DEVICE_STR, status->main_device);
    print_boxed_line(out, line, use_color);
    
    snprintf(line, sizeof(line), "  Spare Device          : %.*s", MAX_DEVICE_STR, status->spare_device);
    print_boxed_line(out, line, use_color);
    
    /* Spare Capacity (with sanity check vs actual device size) */
    snprintf(line, sizeof(line), "  Spare Capacity        : %s", use_color ? COLOR_GREEN "Available" COLOR_RESET : "Available");
    print_boxed_line(out, line, use_color);

    unsigned long long sysfs_spare_sectors = get_device_size_sectors(status->spare_device);
    unsigned long long backing_spare_sectors = get_loop_backing_file_sectors(status->spare_device);
    unsigned long long actual_spare_sectors = backing_spare_sectors ? backing_spare_sectors : sysfs_spare_sectors;
    char reported_size_buf[32];
    char actual_size_buf[32];
    format_size(reported_size_buf, sizeof(reported_size_buf), status->spare_capacity_sectors);
    format_size(actual_size_buf, sizeof(actual_size_buf), actual_spare_sectors);
    int mismatch = (actual_spare_sectors > 0 && status->spare_capacity_sectors > actual_spare_sectors * 2ULL);

    if (mismatch) {
        snprintf(line, sizeof(line), "    └─ Reported: %s (%llu sectors) [UNREALISTIC]", reported_size_buf, status->spare_capacity_sectors);
        print_boxed_line(out, line, use_color);
        snprintf(line, sizeof(line), "    └─ Actual  : %s (%llu sectors) [sanity]", actual_size_buf, actual_spare_sectors);
        print_boxed_line(out, line, use_color);
    } else {
        snprintf(line, sizeof(line), "    └─ %s (%llu sectors)", reported_size_buf, status->spare_capacity_sectors);
        print_boxed_line(out, line, use_color);
    }

    /* Separator before PERFORMANCE */
    if (use_color) {
        fprintf(out, COLOR_CYAN "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤\n");
    }

    /* PERFORMANCE */
    print_section_box(out, use_color ? COLOR_BOLD "PERFORMANCE" COLOR_RESET : "PERFORMANCE", use_color);
    
    const char *perf_rating = get_performance_rating(status->avg_latency_ns);
    if (use_color) {
        snprintf(indicator, sizeof(indicator), "[" COLOR_GREEN "%s" COLOR_RESET "]", perf_rating);
    } else {
        snprintf(indicator, sizeof(indicator), "[%s]", perf_rating);
    }
    snprintf(line, sizeof(line), "  Avg Latency           : %.1f μs  %-28s", 
             status->avg_latency_us, indicator);
    print_boxed_line(out, line, use_color);

    snprintf(line, sizeof(line), "  Throughput            : %.0f MB/s", status->throughput_mbps);
    print_boxed_line(out, line, use_color);

    snprintf(line, sizeof(line), "  I/O Operations        : %llu completed", status->io_ops_completed);
    print_boxed_line(out, line, use_color);

    /* HEALTH */
    /* Separator before HEALTH */
    if (use_color) {
        fprintf(out, COLOR_CYAN "├" COLOR_RESET);
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, COLOR_CYAN "┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤\n");
    }
    print_section_box(out, use_color ? COLOR_BOLD "HEALTH" COLOR_RESET : "HEALTH", use_color);

    const char *health_status = get_health_status_string(status->health_score);
    const char *health_color = status->health_score >= 80 ? COLOR_GREEN : 
                               status->health_score >= 60 ? COLOR_YELLOW : COLOR_RED;
    if (use_color) {
        snprintf(indicator, sizeof(indicator), "[%s%s" COLOR_RESET " %d/100]", health_color, health_status, status->health_score);
    } else {
        snprintf(indicator, sizeof(indicator), "[%s %d/100]", health_status, status->health_score);
    }
    snprintf(line, sizeof(line), "  Health Score          : %-38s  ", indicator);
    print_boxed_line(out, line, use_color);

    snprintf(line, sizeof(line), "  Operational State     : %-38s  ", status->operational_state);
    print_boxed_line(out, line, use_color);

    snprintf(line, sizeof(line), "  Errors                : %llu", status->total_errors);
    print_boxed_line(out, line, use_color);

    const char *hotspot_symbol = status->hotspot_count > 0 ? 
                                (use_color ? COLOR_YELLOW "⚠" COLOR_RESET : "!") : 
                                (use_color ? COLOR_GREEN "✓" COLOR_RESET : "✓");
    snprintf(line, sizeof(line), "  Hotspots              : %u %s", 
             status->hotspot_count, hotspot_symbol);
    print_boxed_line(out, line, use_color);

    /* REMAPPING */
    /* Separator before REMAPPING */
    if (use_color) {
        fprintf(out, COLOR_CYAN "├" COLOR_RESET);
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, COLOR_CYAN "┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤\n");
    }
    print_section_box(out, use_color ? COLOR_BOLD "REMAPPING" COLOR_RESET : "REMAPPING", use_color);

    snprintf(line, sizeof(line), "  Active Remaps         : %u", status->active_remaps);
    print_boxed_line(out, line, use_color);

    double remap_percent = status->total_ios_phase13 > 0 ? 
                          (status->remapped_ios * 100.0 / status->total_ios_phase13) : 0.0;
    snprintf(line, sizeof(line), "  Remapped I/O          : %llu / %llu (%.1f%%)",
             status->remapped_ios, status->total_ios_phase13, remap_percent);
    print_boxed_line(out, line, use_color);

    snprintf(line, sizeof(line), "  Remapped Sectors      : %llu", status->remapped_sectors);
    print_boxed_line(out, line, use_color);

    snprintf(line, sizeof(line), "  Total I/O             : %llu reads, %llu writes",
             status->total_reads, status->total_writes);
    print_boxed_line(out, line, use_color);

    /* CACHE */
    /* Separator before CACHE */
    if (use_color) {
        fprintf(out, COLOR_CYAN "├" COLOR_RESET);
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, COLOR_CYAN "┤" COLOR_RESET "\n");
    } else {
        fprintf(out, "├");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┤\n");
    }
    print_section_box(out, use_color ? COLOR_BOLD "CACHE PERFORMANCE" COLOR_RESET : "CACHE PERFORMANCE", use_color);

    unsigned long long total_lookups = status->cache_hits + status->cache_misses;
    const char *cache_rating = get_cache_rating(status->cache_hit_rate_percent);
    const char *cache_color = status->cache_hit_rate_percent >= 60 ? COLOR_GREEN :
                             status->cache_hit_rate_percent >= 40 ? COLOR_YELLOW : COLOR_RED;
    if (use_color) {
        snprintf(indicator, sizeof(indicator), "[%s%s" COLOR_RESET "]", cache_color, cache_rating);
    } else {
        snprintf(indicator, sizeof(indicator), "[%s]", cache_rating);
    }
    snprintf(line, sizeof(line), "  Cache Hit Rate        : %llu / %llu (%u%%)  %-17s",
             status->cache_hits, total_lookups, status->cache_hit_rate_percent, indicator);
    print_boxed_line(out, line, use_color);

    double fast_path_percent = total_lookups > 0 ? 
                              (status->fast_path_hits * 100.0 / total_lookups) : 0.0;
    snprintf(line, sizeof(line), "  Fast Path Hits        : %llu / %llu (%.1f%%)",
             status->fast_path_hits, total_lookups, fast_path_percent);
    print_boxed_line(out, line, use_color);

    double slow_path_percent = total_lookups > 0 ? 
                              (status->slow_path_hits * 100.0 / total_lookups) : 0.0;
    snprintf(line, sizeof(line), "  Slow Path Hits        : %llu / %llu (%.1f%%)",
             status->slow_path_hits, total_lookups, slow_path_percent);
    print_boxed_line(out, line, use_color);

    /* Footer */
    /* Footer border */
    if (use_color) {
        fprintf(out, COLOR_CYAN "└");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┘" COLOR_RESET "\n");
    } else {
        fprintf(out, "└");
        for (int i=0;i<CONTENT_TARGET;i++) fprintf(out, "─");
        fprintf(out, "┘\n");
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
