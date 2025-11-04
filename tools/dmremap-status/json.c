#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dmremap_status.h"

/**
 * Simple JSON escaping for strings
 */
static void json_escape_string(FILE *out, const char *str)
{
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '"':
            fprintf(out, "\\\"");
            break;
        case '\\':
            fprintf(out, "\\\\");
            break;
        case '\b':
            fprintf(out, "\\b");
            break;
        case '\f':
            fprintf(out, "\\f");
            break;
        case '\n':
            fprintf(out, "\\n");
            break;
        case '\r':
            fprintf(out, "\\r");
            break;
        case '\t':
            fprintf(out, "\\t");
            break;
        default:
            if (*p < 0x20) {
                fprintf(out, "\\u%04x", (unsigned char)*p);
            } else {
                fputc(*p, out);
            }
            break;
        }
    }
}

/**
 * Print JSON string value
 */
static void json_print_string(FILE *out, const char *key, const char *value)
{
    fprintf(out, "    \"%s\": \"", key);
    json_escape_string(out, value);
    fprintf(out, "\"");
}

/**
 * Print JSON number value
 */
static void json_print_number(FILE *out, const char *key, unsigned long long value)
{
    fprintf(out, "    \"%s\": %llu", key, value);
}

/**
 * Print JSON double value
 */
static void json_print_double(FILE *out, const char *key, double value)
{
    fprintf(out, "    \"%s\": %.2f", key, value);
}

/**
 * Print JSON output format
 */
int print_json(const dm_remap_status_t *status, FILE *out)
{
    if (!status || !out) {
        fprintf(stderr, "Error: NULL pointer passed to print_json\n");
        return -1;
    }

    /* Get current time as ISO 8601 */
    char timestamp_str[32];
    struct tm *tm_info = localtime(&status->timestamp);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    fprintf(out, "{\n");

    /* Basic Information */
    fprintf(out, "  \"timestamp\": \"%s\",\n", timestamp_str);
    fprintf(out, "  \"device\": {\n");
    json_print_string(out, "version", status->version);
    fprintf(out, ",\n");
    json_print_string(out, "target", status->target_type);
    fprintf(out, ",\n");
    json_print_number(out, "size_sectors", status->device_size_sectors);
    fprintf(out, ",\n");
    json_print_double(out, "size_gb", status->device_size_gb);
    fprintf(out, ",\n");
    json_print_number(out, "sector_size", status->sector_size);
    fprintf(out, "\n  },\n");

    /* Device Paths */
    fprintf(out, "  \"devices\": {\n");
    json_print_string(out, "main", status->main_device);
    fprintf(out, ",\n");
    json_print_string(out, "spare", status->spare_device);
    fprintf(out, ",\n");
    json_print_number(out, "spare_capacity_sectors", status->spare_capacity_sectors);
    fprintf(out, ",\n");
    json_print_double(out, "spare_capacity_gb", status->spare_capacity_gb);
    fprintf(out, "\n  },\n");

    /* I/O Statistics */
    fprintf(out, "  \"io_statistics\": {\n");
    json_print_number(out, "total_reads", status->total_reads);
    fprintf(out, ",\n");
    json_print_number(out, "total_writes", status->total_writes);
    fprintf(out, ",\n");
    json_print_number(out, "total_remaps", status->total_remaps);
    fprintf(out, ",\n");
    json_print_number(out, "total_errors", status->total_errors);
    fprintf(out, "\n  },\n");

    /* Performance Metrics */
    fprintf(out, "  \"performance\": {\n");
    json_print_number(out, "io_ops_completed", status->io_ops_completed);
    fprintf(out, ",\n");
    json_print_number(out, "total_time_ns", status->total_time_ns);
    fprintf(out, ",\n");
    json_print_number(out, "avg_latency_ns", status->avg_latency_ns);
    fprintf(out, ",\n");
    json_print_double(out, "avg_latency_us", status->avg_latency_us);
    fprintf(out, ",\n");
    json_print_number(out, "throughput_bps", status->throughput_bps);
    fprintf(out, ",\n");
    json_print_double(out, "throughput_mbps", status->throughput_mbps);
    fprintf(out, "\n  },\n");

    /* Remapping Statistics (Phase 1.3) */
    fprintf(out, "  \"remapping\": {\n");
    json_print_number(out, "active_remaps", status->active_remaps);
    fprintf(out, ",\n");
    json_print_number(out, "total_ios", status->total_ios_phase13);
    fprintf(out, ",\n");
    json_print_number(out, "normal_ios", status->normal_ios);
    fprintf(out, ",\n");
    json_print_number(out, "remapped_ios", status->remapped_ios);
    fprintf(out, ",\n");
    json_print_number(out, "remapped_sectors", status->remapped_sectors);
    fprintf(out, "\n  },\n");

    /* Cache Statistics (Phase 1.4) */
    fprintf(out, "  \"cache\": {\n");
    json_print_number(out, "hits", status->cache_hits);
    fprintf(out, ",\n");
    json_print_number(out, "misses", status->cache_misses);
    fprintf(out, ",\n");

    unsigned long long total_lookups = status->cache_hits + status->cache_misses;
    fprintf(out, "    \"total_lookups\": %llu,\n", total_lookups);
    json_print_number(out, "fast_path_hits", status->fast_path_hits);
    fprintf(out, ",\n");
    json_print_number(out, "slow_path_hits", status->slow_path_hits);
    fprintf(out, ",\n");
    json_print_number(out, "hit_rate_percent", status->cache_hit_rate_percent);
    fprintf(out, "\n  },\n");

    /* Health Monitoring */
    fprintf(out, "  \"health\": {\n");
    json_print_number(out, "score", status->health_score);
    fprintf(out, ",\n");
    json_print_string(out, "status", get_health_status_string(status->health_score));
    fprintf(out, ",\n");
    json_print_number(out, "hotspots", status->hotspot_count);
    fprintf(out, ",\n");
    json_print_number(out, "health_scans", status->health_scans);
    fprintf(out, "\n  },\n");

    /* Operational Information */
    fprintf(out, "  \"operational\": {\n");
    json_print_string(out, "state", status->operational_state);
    fprintf(out, ",\n");
    json_print_string(out, "mode", status->device_mode);
    fprintf(out, "\n  }\n");

    fprintf(out, "}\n");

    return 0;
}

/**
 * Print CSV output format
 * Single line with all important metrics
 */
int print_csv(const dm_remap_status_t *status, FILE *out)
{
    if (!status || !out) {
        fprintf(stderr, "Error: NULL pointer passed to print_csv\n");
        return -1;
    }

    /* Print header on first call (not tracked, so always print as comment) */
    fprintf(out, "# timestamp,device,version,size_gb,health_score,health_status,operational_state,");
    fprintf(out, "avg_latency_us,throughput_mbps,total_reads,total_writes,total_errors,");
    fprintf(out, "active_remaps,remapped_ios,remapped_sectors,cache_hit_rate,total_ios_completed,hotspots\n");

    /* Print data */
    char timestamp_str[32];
    struct tm *tm_info = localtime(&status->timestamp);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    fprintf(out, "%s,%s,%s,%.1f,%u,%s,%s,%.1f,%.0f,%llu,%llu,%llu,%u,%llu,%llu,%u,%llu,%u\n",
            timestamp_str,
            status->main_device,
            status->version,
            status->device_size_gb,
            status->health_score,
            get_health_status_string(status->health_score),
            status->operational_state,
            status->avg_latency_us,
            status->throughput_mbps,
            status->total_reads,
            status->total_writes,
            status->total_errors,
            status->active_remaps,
            status->remapped_ios,
            status->remapped_sectors,
            status->cache_hit_rate_percent,
            status->io_ops_completed,
            status->hotspot_count);

    return 0;
}
