/*
 * dm-remap v4.0 Automatic Setup Reassembly System - Discovery Engine
 * 
 * Functions for scanning devices, discovering setups, and automatically
 * reconstructing dm-remap configurations from stored metadata.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device-mapper.h>
#include <linux/proc_fs.h>
#include "../include/dm-remap-v4-setup-reassembly.h"

/* Internal discovery state */
struct dm_remap_v4_discovery_state {
    struct list_head discovered_setups;
    struct mutex discovery_lock;
    uint32_t discovery_id_counter;
    uint64_t last_scan_timestamp;
    uint32_t total_devices_scanned;
    uint32_t setups_discovered;
};

static struct dm_remap_v4_discovery_state discovery_state = {
    .discovery_lock = __MUTEX_INITIALIZER(discovery_state.discovery_lock),
    .discovery_id_counter = 1,
    .last_scan_timestamp = 0,
    .total_devices_scanned = 0,
    .setups_discovered = 0
};

/* Device scanning parameters */
static const char* default_scan_patterns[] = {
    "/dev/sd*",      /* SCSI disks */
    "/dev/nvme*",    /* NVMe devices */
    "/dev/vd*",      /* Virtual disks */
    "/dev/xvd*",     /* Xen virtual disks */
    "/dev/loop*",    /* Loop devices */
    "/dev/dm-*",     /* Device mapper devices */
    NULL
};

/*
 * Initialize discovery system
 */
int dm_remap_v4_init_discovery_system(void)
{
    mutex_lock(&discovery_state.discovery_lock);
    
    INIT_LIST_HEAD(&discovery_state.discovered_setups);
    discovery_state.discovery_id_counter = 1;
    discovery_state.last_scan_timestamp = ktime_get_real_seconds();
    discovery_state.total_devices_scanned = 0;
    discovery_state.setups_discovered = 0;
    
    mutex_unlock(&discovery_state.discovery_lock);
    
    DMINFO("dm-remap v4.0 discovery system initialized");
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Cleanup discovery system
 */
void dm_remap_v4_cleanup_discovery_system(void)
{
    struct dm_remap_v4_discovery_result *result, *tmp;
    
    mutex_lock(&discovery_state.discovery_lock);
    
    /* Free all discovered setups */
    list_for_each_entry_safe(result, tmp, &discovery_state.discovered_setups, list) {
        list_del(&result->list);
        kfree(result);
    }
    
    mutex_unlock(&discovery_state.discovery_lock);
    
    DMINFO("dm-remap v4.0 discovery system cleaned up");
}

/*
 * Scan single device for dm-remap metadata
 */
int dm_remap_v4_scan_device_for_metadata(
    const char *device_path,
    struct dm_remap_v4_discovery_result *result)
{
    struct dm_remap_v4_setup_metadata metadata;
    struct dm_remap_v4_metadata_read_result read_result;
    int scan_result;
    
    if (!device_path || !result) {
        return -EINVAL;
    }
    
    /* Initialize result structure */
    memset(result, 0, sizeof(*result));
    strncpy(result->device_path, device_path, sizeof(result->device_path) - 1);
    result->discovery_timestamp = ktime_get_real_seconds();
    
    /* Try to read metadata from device */
    scan_result = dm_remap_v4_read_metadata_validated(device_path, &metadata, &read_result);
    
    result->copies_found = read_result.copies_found;
    result->copies_valid = read_result.copies_valid;
    result->corruption_level = read_result.corruption_level;
    
    if (scan_result == DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        /* Found valid metadata */
        result->metadata = metadata;
        result->has_metadata = true;
        result->confidence_score = dm_remap_v4_calculate_confidence_score(result);
        
        DMINFO("Found dm-remap metadata on %s: setup='%s', confidence=%.2f",
               device_path, metadata.setup_description, result->confidence_score);
        
        return DM_REMAP_V4_REASSEMBLY_SUCCESS;
    } else if (scan_result == -DM_REMAP_V4_REASSEMBLY_ERROR_NO_METADATA) {
        /* No metadata found - normal case */
        result->has_metadata = false;
        result->confidence_score = 0.0f;
        return DM_REMAP_V4_REASSEMBLY_SUCCESS;
    } else {
        /* Error occurred during scanning */
        result->has_metadata = false;
        result->confidence_score = 0.0f;
        result->corruption_level = 10; /* Maximum corruption */
        
        DMWARN("Error scanning device %s for metadata: %d", device_path, scan_result);
        return scan_result;
    }
}

/*
 * Check if device path exists and is accessible
 */
static bool dm_remap_v4_device_exists(const char *device_path)
{
    struct file *file;
    
    if (!device_path) {
        return false;
    }
    
    file = filp_open(device_path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        return false;
    }
    
    filp_close(file, NULL);
    return true;
}

/*
 * Scan all block devices for dm-remap metadata
 */
int dm_remap_v4_scan_all_devices(
    struct dm_remap_v4_discovery_result **results,
    uint32_t *num_results,
    uint32_t max_results)
{
    struct dm_remap_v4_discovery_result *result_array;
    struct dm_remap_v4_discovery_result scan_result;
    uint32_t results_count = 0;
    uint32_t devices_scanned = 0;
    char device_path[256];
    int i, j;
    int scan_status;
    
    if (!results || !num_results || max_results == 0) {
        return -EINVAL;
    }
    
    /* Allocate results array */
    result_array = kcalloc(max_results, sizeof(*result_array), GFP_KERNEL);
    if (!result_array) {
        DMERR("Failed to allocate results array for device scan");
        return -ENOMEM;
    }
    
    DMINFO("Starting system-wide device scan for dm-remap metadata (max %u results)", max_results);
    
    /* Scan common device patterns */
    for (i = 0; default_scan_patterns[i] && results_count < max_results; i++) {
        const char *pattern = default_scan_patterns[i];
        
        /* Simple pattern matching - in a real implementation, this would use
         * proper glob matching or sysfs enumeration */
        if (strstr(pattern, "sd*")) {
            /* Scan /dev/sda through /dev/sdz */
            for (j = 'a'; j <= 'z' && results_count < max_results; j++) {
                snprintf(device_path, sizeof(device_path), "/dev/sd%c", j);
                
                if (dm_remap_v4_device_exists(device_path)) {
                    devices_scanned++;
                    scan_status = dm_remap_v4_scan_device_for_metadata(device_path, &scan_result);
                    
                    if (scan_status == DM_REMAP_V4_REASSEMBLY_SUCCESS && scan_result.has_metadata) {
                        result_array[results_count] = scan_result;
                        results_count++;
                        DMINFO("Added discovery result %u: %s", results_count, device_path);
                    }
                }
            }
        } else if (strstr(pattern, "nvme*")) {
            /* Scan /dev/nvme0n1 through /dev/nvme9n9 */
            for (i = 0; i < 10 && results_count < max_results; i++) {
                for (j = 1; j <= 9 && results_count < max_results; j++) {
                    snprintf(device_path, sizeof(device_path), "/dev/nvme%dn%d", i, j);
                    
                    if (dm_remap_v4_device_exists(device_path)) {
                        devices_scanned++;
                        scan_status = dm_remap_v4_scan_device_for_metadata(device_path, &scan_result);
                        
                        if (scan_status == DM_REMAP_V4_REASSEMBLY_SUCCESS && scan_result.has_metadata) {
                            result_array[results_count] = scan_result;
                            results_count++;
                            DMINFO("Added discovery result %u: %s", results_count, device_path);
                        }
                    }
                }
            }
        }
        /* Add other pattern implementations as needed */
    }
    
    /* Update discovery state */
    mutex_lock(&discovery_state.discovery_lock);
    discovery_state.total_devices_scanned += devices_scanned;
    discovery_state.setups_discovered += results_count;
    discovery_state.last_scan_timestamp = ktime_get_real_seconds();
    mutex_unlock(&discovery_state.discovery_lock);
    
    *results = result_array;
    *num_results = results_count;
    
    DMINFO("Device scan completed: %u devices scanned, %u setups discovered", 
           devices_scanned, results_count);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Group discovered results by setup ID
 */
int dm_remap_v4_group_discovery_results(
    const struct dm_remap_v4_discovery_result *results,
    uint32_t num_results,
    struct dm_remap_v4_setup_group **groups,
    uint32_t *num_groups)
{
    struct dm_remap_v4_setup_group *group_array;
    uint32_t groups_count = 0;
    bool found_group;
    int i, j;
    
    if (!results || !groups || !num_groups || num_results == 0) {
        return -EINVAL;
    }
    
    /* Allocate groups array (worst case: one group per result) */
    group_array = kcalloc(num_results, sizeof(*group_array), GFP_KERNEL);
    if (!group_array) {
        DMERR("Failed to allocate groups array");
        return -ENOMEM;
    }
    
    DMINFO("Grouping %u discovery results by setup", num_results);
    
    /* Group results by setup description and main device */
    for (i = 0; i < num_results; i++) {
        const struct dm_remap_v4_discovery_result *result = &results[i];
        
        if (!result->has_metadata) {
            continue;
        }
        
        found_group = false;
        
        /* Try to find existing group */
        for (j = 0; j < groups_count; j++) {
            struct dm_remap_v4_setup_group *group = &group_array[j];
            
            /* Compare by setup description and main device UUID */
            if (strcmp(group->setup_description, result->metadata.setup_description) == 0 &&
                uuid_equal(&group->main_device_uuid, &result->metadata.main_device.device_uuid)) {
                
                /* Add to existing group */
                if (group->num_devices < DM_REMAP_V4_MAX_DEVICES_PER_GROUP) {
                    group->devices[group->num_devices] = *result;
                    group->num_devices++;
                    
                    /* Update group confidence with best result */
                    if (result->confidence_score > group->group_confidence) {
                        group->group_confidence = result->confidence_score;
                        group->best_metadata = result->metadata;
                    }
                    
                    found_group = true;
                    break;
                }
            }
        }
        
        /* Create new group if not found */
        if (!found_group && groups_count < num_results) {
            struct dm_remap_v4_setup_group *new_group = &group_array[groups_count];
            
            memset(new_group, 0, sizeof(*new_group));
            new_group->group_id = groups_count + 1;
            strncpy(new_group->setup_description, result->metadata.setup_description,
                    sizeof(new_group->setup_description) - 1);
            new_group->main_device_uuid = result->metadata.main_device.device_uuid;
            new_group->discovery_timestamp = result->discovery_timestamp;
            new_group->group_confidence = result->confidence_score;
            new_group->best_metadata = result->metadata;
            
            /* Add first device to group */
            new_group->devices[0] = *result;
            new_group->num_devices = 1;
            
            groups_count++;
            
            DMINFO("Created new setup group %u: '%s'", new_group->group_id, 
                   new_group->setup_description);
        }
    }
    
    /* Sort groups by confidence (highest first) */
    for (i = 0; i < groups_count - 1; i++) {
        for (j = i + 1; j < groups_count; j++) {
            if (group_array[j].group_confidence > group_array[i].group_confidence) {
                struct dm_remap_v4_setup_group temp = group_array[i];
                group_array[i] = group_array[j];
                group_array[j] = temp;
            }
        }
    }
    
    *groups = group_array;
    *num_groups = groups_count;
    
    DMINFO("Grouped %u results into %u setup groups", num_results, groups_count);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Validate setup group consistency
 */
int dm_remap_v4_validate_setup_group(const struct dm_remap_v4_setup_group *group)
{
    const struct dm_remap_v4_setup_metadata *ref_metadata;
    uint64_t version_conflicts = 0;
    uint64_t highest_version = 0;
    float min_confidence = 1.0f;
    int i;
    
    if (!group || group->num_devices == 0) {
        return -EINVAL;
    }
    
    ref_metadata = &group->best_metadata;
    
    DMINFO("Validating setup group %u: '%s' (%u devices)",
           group->group_id, group->setup_description, group->num_devices);
    
    /* Check version consistency */
    for (i = 0; i < group->num_devices; i++) {
        const struct dm_remap_v4_discovery_result *device = &group->devices[i];
        
        if (!device->has_metadata) {
            continue;
        }
        
        /* Track version conflicts */
        if (device->metadata.version_counter != ref_metadata->version_counter) {
            version_conflicts++;
            if (device->metadata.version_counter > highest_version) {
                highest_version = device->metadata.version_counter;
            }
        }
        
        /* Track minimum confidence */
        if (device->confidence_score < min_confidence) {
            min_confidence = device->confidence_score;
        }
        
        /* Verify main device consistency */
        if (!uuid_equal(&device->metadata.main_device.device_uuid, 
                       &ref_metadata->main_device.device_uuid)) {
            DMERR("Main device UUID mismatch in group %u", group->group_id);
            return -DM_REMAP_V4_REASSEMBLY_ERROR_SETUP_CONFLICT;
        }
    }
    
    /* Evaluate validation results */
    if (version_conflicts > 0) {
        DMWARN("Setup group %u has %llu version conflicts (highest: %llu)",
               group->group_id, version_conflicts, highest_version);
    }
    
    if (min_confidence < DM_REMAP_V4_MIN_CONFIDENCE_THRESHOLD) {
        DMWARN("Setup group %u has low minimum confidence: %.2f",
               group->group_id, min_confidence);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_LOW_CONFIDENCE;
    }
    
    if (group->group_confidence < DM_REMAP_V4_MIN_CONFIDENCE_THRESHOLD) {
        DMWARN("Setup group %u has low group confidence: %.2f",
               group->group_id, group->group_confidence);
        return -DM_REMAP_V4_REASSEMBLY_ERROR_LOW_CONFIDENCE;
    }
    
    DMINFO("Setup group %u validation passed: confidence=%.2f, conflicts=%llu",
           group->group_id, group->group_confidence, version_conflicts);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Reconstruct dm-remap setup from discovered metadata
 */
int dm_remap_v4_reconstruct_setup(
    const struct dm_remap_v4_setup_group *group,
    struct dm_remap_v4_reconstruction_plan *plan)
{
    const struct dm_remap_v4_setup_metadata *metadata;
    int result;
    int i;
    
    if (!group || !plan) {
        return -EINVAL;
    }
    
    /* Validate group first */
    result = dm_remap_v4_validate_setup_group(group);
    if (result != DM_REMAP_V4_REASSEMBLY_SUCCESS) {
        DMERR("Setup group %u validation failed: %d", group->group_id, result);
        return result;
    }
    
    metadata = &group->best_metadata;
    
    /* Initialize reconstruction plan */
    memset(plan, 0, sizeof(*plan));
    plan->group_id = group->group_id;
    plan->plan_timestamp = ktime_get_real_seconds();
    plan->confidence_score = group->group_confidence;
    
    strncpy(plan->setup_name, metadata->setup_description, 
            sizeof(plan->setup_name) - 1);
    strncpy(plan->target_name, "remap-v4", sizeof(plan->target_name) - 1);
    
    /* Copy target parameters */
    strncpy(plan->target_params, metadata->target_config.target_params,
            sizeof(plan->target_params) - 1);
    
    /* Set main device */
    strncpy(plan->main_device_path, metadata->main_device.device_path,
            sizeof(plan->main_device_path) - 1);
    
    /* Copy spare devices */
    plan->num_spare_devices = metadata->num_spare_devices;
    for (i = 0; i < metadata->num_spare_devices && i < DM_REMAP_V4_MAX_SPARE_DEVICES; i++) {
        const struct dm_remap_v4_spare_relationship *spare = &metadata->spare_devices[i];
        strncpy(plan->spare_device_paths[i], spare->spare_fingerprint.device_path,
                sizeof(plan->spare_device_paths[i]) - 1);
    }
    
    /* Copy sysfs settings */
    plan->num_sysfs_settings = metadata->sysfs_config.num_settings;
    for (i = 0; i < metadata->sysfs_config.num_settings && i < DM_REMAP_V4_MAX_SYSFS_SETTINGS; i++) {
        plan->sysfs_settings[i] = metadata->sysfs_config.settings[i];
    }
    
    /* Generate dmsetup command */
    snprintf(plan->dmsetup_create_command, sizeof(plan->dmsetup_create_command),
             "dmsetup create %s --table \"%s\"",
             metadata->setup_description, metadata->target_config.target_params);
    
    /* Set reconstruction steps */
    plan->num_steps = 0;
    
    /* Step 1: Verify devices */
    strncpy(plan->steps[plan->num_steps].description, "Verify all devices are accessible",
            sizeof(plan->steps[plan->num_steps].description) - 1);
    plan->steps[plan->num_steps].step_type = 1; /* Verification step */
    plan->num_steps++;
    
    /* Step 2: Create DM target */
    strncpy(plan->steps[plan->num_steps].description, "Create device-mapper target",
            sizeof(plan->steps[plan->num_steps].description) - 1);
    plan->steps[plan->num_steps].step_type = 2; /* Creation step */
    plan->num_steps++;
    
    /* Step 3: Apply sysfs settings */
    if (plan->num_sysfs_settings > 0) {
        strncpy(plan->steps[plan->num_steps].description, "Apply sysfs configuration",
                sizeof(plan->steps[plan->num_steps].description) - 1);
        plan->steps[plan->num_steps].step_type = 3; /* Configuration step */
        plan->num_steps++;
    }
    
    DMINFO("Created reconstruction plan for setup '%s': %u steps, confidence=%.2f",
           plan->setup_name, plan->num_steps, plan->confidence_score);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

/*
 * Get discovery system statistics
 */
int dm_remap_v4_get_discovery_stats(struct dm_remap_v4_discovery_stats *stats)
{
    if (!stats) {
        return -EINVAL;
    }
    
    mutex_lock(&discovery_state.discovery_lock);
    
    memset(stats, 0, sizeof(*stats));
    stats->last_scan_timestamp = discovery_state.last_scan_timestamp;
    stats->total_devices_scanned = discovery_state.total_devices_scanned;
    stats->setups_discovered = discovery_state.setups_discovered;
    stats->system_uptime = ktime_get_real_seconds();
    
    /* Count current discovered setups in memory */
    struct dm_remap_v4_discovery_result *result;
    list_for_each_entry(result, &discovery_state.discovered_setups, list) {
        stats->setups_in_memory++;
        if (result->confidence_score >= DM_REMAP_V4_MIN_CONFIDENCE_THRESHOLD) {
            stats->high_confidence_setups++;
        }
    }
    
    mutex_unlock(&discovery_state.discovery_lock);
    
    return DM_REMAP_V4_REASSEMBLY_SUCCESS;
}

MODULE_DESCRIPTION("dm-remap v4.0 Setup Reassembly Discovery Engine");
MODULE_AUTHOR("dm-remap development team");
MODULE_LICENSE("GPL");