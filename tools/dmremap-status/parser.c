#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dmremap_status.h"

/**
 * Parse raw kernel status output into structured data
 * 
 * Expected format (31 fields):
 * 0 4194304 dm-remap-v4 v4.0-phase1.4 /dev/mapper/dm-test-linear /dev/loop1 
 * 6 0 0 0 1 6 43797 7299 758518518 512 4292870144 
 * 6 6 0 0 4 2 4 0 0 100 0 66 operational real
 */
int parse_dmremap_status(const char *raw_status, dm_remap_status_t *parsed)
{
    if (!raw_status || !parsed) {
        fprintf(stderr, "Error: NULL pointer passed to parse_dmremap_status\n");
        return -1;
    }

    /* Create a modifiable copy for strtok */
    char *status_copy = strdup(raw_status);
    if (!status_copy) {
        perror("strdup");
        return -1;
    }

    char *saveptr = NULL;
    char *token = NULL;
    int field = 0;

    /* Initialize timestamp */
    parsed->timestamp = time(NULL);

    /* Parse each field */
    for (token = strtok_r(status_copy, " ", &saveptr); token; token = strtok_r(NULL, " ", &saveptr)) {
        field++;

        switch (field) {
        case 1:
            parsed->start_sector = strtoull(token, NULL, 10);
            break;
        case 2:
            parsed->device_size_sectors = strtoull(token, NULL, 10);
            break;
        case 3:
            strncpy(parsed->target_type, token, sizeof(parsed->target_type) - 1);
            break;
        case 4:
            strncpy(parsed->version, token, sizeof(parsed->version) - 1);
            break;
        case 5:
            strncpy(parsed->main_device, token, sizeof(parsed->main_device) - 1);
            break;
        case 6:
            strncpy(parsed->spare_device, token, sizeof(parsed->spare_device) - 1);
            break;
        case 7:
            parsed->total_reads = strtoull(token, NULL, 10);
            break;
        case 8:
            parsed->total_writes = strtoull(token, NULL, 10);
            break;
        case 9:
            parsed->total_remaps = strtoull(token, NULL, 10);
            break;
        case 10:
            parsed->total_errors = strtoull(token, NULL, 10);
            break;
        case 11:
            parsed->active_remaps = (unsigned int)strtoul(token, NULL, 10);
            break;
        case 12:
            parsed->io_ops_completed = strtoull(token, NULL, 10);
            break;
        case 13:
            parsed->total_time_ns = strtoull(token, NULL, 10);
            break;
        case 14:
            parsed->avg_latency_ns = strtoull(token, NULL, 10);
            break;
        case 15:
            parsed->throughput_bps = strtoull(token, NULL, 10);
            break;
        case 16:
            parsed->sector_size = (unsigned int)strtoul(token, NULL, 10);
            break;
        case 17:
            parsed->spare_capacity_sectors = strtoull(token, NULL, 10);
            break;
        case 18:
            parsed->total_ios_phase13 = strtoull(token, NULL, 10);
            break;
        case 19:
            parsed->normal_ios = strtoull(token, NULL, 10);
            break;
        case 20:
            parsed->remapped_ios = strtoull(token, NULL, 10);
            break;
        case 21:
            parsed->remapped_sectors = strtoull(token, NULL, 10);
            break;
        case 22:
            parsed->cache_hits = strtoull(token, NULL, 10);
            break;
        case 23:
            parsed->cache_misses = strtoull(token, NULL, 10);
            break;
        case 24:
            parsed->fast_path_hits = strtoull(token, NULL, 10);
            break;
        case 25:
            parsed->slow_path_hits = strtoull(token, NULL, 10);
            break;
        case 26:
            parsed->health_scans = strtoull(token, NULL, 10);
            break;
        case 27:
            parsed->health_score = (unsigned int)strtoul(token, NULL, 10);
            break;
        case 28:
            parsed->hotspot_count = (unsigned int)strtoul(token, NULL, 10);
            break;
        case 29:
            parsed->cache_hit_rate_percent = (unsigned int)strtoul(token, NULL, 10);
            break;
        case 30:
            strncpy(parsed->operational_state, token, sizeof(parsed->operational_state) - 1);
            break;
        case 31:
            strncpy(parsed->device_mode, token, sizeof(parsed->device_mode) - 1);
            break;
        default:
            fprintf(stderr, "Warning: extra field %d: %s\n", field, token);
            break;
        }
    }

    free(status_copy);

    /* Verify we got all required fields */
    if (field < 31) {
        fprintf(stderr, "Error: Expected 31 fields, got %d\n", field);
        return -1;
    }

    /* Compute derived fields */
    compute_derived_fields(parsed);

    return 0;
}

/**
 * Parse status from a file
 */
int parse_dmremap_status_file(const char *filename, dm_remap_status_t *parsed)
{
    if (!filename || !parsed) {
        fprintf(stderr, "Error: NULL pointer passed to parse_dmremap_status_file\n");
        return -1;
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return -1;
    }

    char buffer[4096];
    if (!fgets(buffer, sizeof(buffer), f)) {
        perror("fgets");
        fclose(f);
        return -1;
    }

    fclose(f);

    /* Remove trailing newline */
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    return parse_dmremap_status(buffer, parsed);
}

/**
 * Compute derived/calculated fields
 */
void compute_derived_fields(dm_remap_status_t *status)
{
    if (!status)
        return;

    /* Convert latency from nanoseconds to microseconds */
    status->avg_latency_us = status->avg_latency_ns / 1000.0;

    /* Convert throughput from bytes/s to MB/s */
    status->throughput_mbps = status->throughput_bps / (1024.0 * 1024.0);

    /* Convert sectors to GB (sector size typically 512 bytes) */
    unsigned long long bytes_per_sector = status->sector_size ? status->sector_size : 512;
    status->spare_capacity_gb = (status->spare_capacity_sectors * bytes_per_sector) / (1024.0 * 1024.0 * 1024.0);
    status->device_size_gb = (status->device_size_sectors * bytes_per_sector) / (1024.0 * 1024.0 * 1024.0);
}

/**
 * Get health status string based on score
 */
const char *get_health_status_string(unsigned int score)
{
    if (score >= 95)
        return "EXCELLENT";
    else if (score >= 80)
        return "GOOD";
    else if (score >= 60)
        return "FAIR";
    else if (score >= 40)
        return "POOR";
    else
        return "CRITICAL";
}

/**
 * Get performance rating based on latency
 */
const char *get_performance_rating(unsigned long long latency_ns)
{
    if (latency_ns < 1000)        /* < 1 microsecond */
        return "EXCELLENT";
    else if (latency_ns < 10000)   /* < 10 microseconds */
        return "EXCELLENT";
    else if (latency_ns < 50000)   /* < 50 microseconds */
        return "GOOD";
    else if (latency_ns < 100000)  /* < 100 microseconds */
        return "FAIR";
    else if (latency_ns < 1000000) /* < 1 millisecond */
        return "POOR";
    else
        return "CRITICAL";
}

/**
 * Get cache hit rate rating
 */
const char *get_cache_rating(unsigned int hit_rate)
{
    if (hit_rate >= 80)
        return "EXCELLENT";
    else if (hit_rate >= 60)
        return "GOOD";
    else if (hit_rate >= 40)
        return "FAIR";
    else if (hit_rate >= 20)
        return "POOR";
    else
        return "CRITICAL";
}
