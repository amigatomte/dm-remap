/*
 * dm-remap-stress-sysfs.c - Phase 3.2C Stress Testing Control Interface
 * 
 * This file implements a comprehensive sysfs interface for controlling
 * and monitoring Phase 3.2C stress testing and performance validation.
 * 
 * INTERFACE CAPABILITIES:
 * - Start/stop stress tests with configurable parameters
 * - Real-time monitoring of test progress
 * - Performance regression testing control
 * - Large dataset validation management
 * - Production workload simulation
 * - Comprehensive results reporting
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dm-remap-core.h"
#include "dm-remap-stress-test.h"
#include "dm-remap-io-optimized.h"

/* Phase 3.2C: Sysfs kobject for stress testing control */
static struct kobject *dmr_stress_kobj = NULL;

/* Reference to current target for testing */
static struct dm_target *stress_test_target = (void *)-1;  /* -1 = not set, NULL = testing mode */

/* Utility functions (duplicated here to avoid header issues) */
static inline u64 dmr_stress_calculate_iops(u64 operations, u64 duration_ms)
{
    if (duration_ms == 0)
        return 0;
    return operations * 1000 / duration_ms;
}

/*
 * Phase 3.2C: Stress Test Control Attributes
 */

/**
 * stress_test_start_store - Start stress test with parameters
 */
static ssize_t stress_test_start_store(struct kobject *kobj,
                                      struct kobj_attribute *attr,
                                      const char *buf, size_t count)
{
    int test_type, num_workers, duration_ms;
    int ret;
    
    /* Phase 3.2C allows NULL target for testing mode */
    if (stress_test_target == (void *)-1) {  /* Use special marker to indicate no target set */
        DMR_DEBUG(0, "No target available for stress testing");
        return -ENODEV;
    }
    
    ret = sscanf(buf, "%d %d %d", &test_type, &num_workers, &duration_ms);
    if (ret != 3) {
        DMR_DEBUG(0, "Usage: echo \"<test_type> <num_workers> <duration_ms>\" > stress_test_start");
        DMR_DEBUG(0, "Test types: 0=seq_read, 1=rand_read, 2=seq_write, 3=rand_write, 4=mixed, 5=remap_heavy");
        return -EINVAL;
    }
    
    if (test_type < 0 || test_type >= DMR_STRESS_MAX_TYPES) {
        DMR_DEBUG(0, "Invalid test type: %d", test_type);
        return -EINVAL;
    }
    
    if (num_workers <= 0 || num_workers > DMR_STRESS_MAX_THREADS) {
        DMR_DEBUG(0, "Invalid number of workers: %d (max: %d)", 
                  num_workers, DMR_STRESS_MAX_THREADS);
        return -EINVAL;
    }
    
    if (duration_ms <= 0 || duration_ms > 3600000) { /* Max 1 hour */
        DMR_DEBUG(0, "Invalid duration: %d ms (max: 3600000)", duration_ms);
        return -EINVAL;
    }
    
    ret = dmr_stress_test_start(stress_test_target, (enum dmr_stress_test_type)test_type,
                               num_workers, duration_ms);
    if (ret) {
        DMR_DEBUG(0, "Failed to start stress test: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C: Started stress test type %d with %d workers for %d ms",
              test_type, num_workers, duration_ms);
    
    return count;
}

/**
 * stress_test_stop_store - Stop ongoing stress test
 */
static ssize_t stress_test_stop_store(struct kobject *kobj,
                                     struct kobj_attribute *attr,
                                     const char *buf, size_t count)
{
    int stop_cmd;
    int ret;
    
    ret = kstrtoint(buf, 10, &stop_cmd);
    if (ret)
        return ret;
    
    if (stop_cmd == 1) {
        dmr_stress_test_stop();
        DMR_DEBUG(1, "Phase 3.2C: Stress test stop requested");
    }
    
    return count;
}

/**
 * stress_test_status_show - Show current stress test status
 */
static ssize_t stress_test_status_show(struct kobject *kobj,
                                      struct kobj_attribute *attr, char *buf)
{
    bool is_running = dmr_stress_test_is_running();
    
    return sprintf(buf, "%s\n", is_running ? "RUNNING" : "STOPPED");
}

/**
 * stress_test_results_show - Show comprehensive stress test results
 */
static ssize_t stress_test_results_show(struct kobject *kobj,
                                       struct kobj_attribute *attr, char *buf)
{
    struct dmr_performance_regression_results results;
    
    dmr_stress_test_get_results(&results);
    
    return sprintf(buf,
        "=== Phase 3.2C Stress Test Results ===\n"
        "\n"
        "Test Configuration:\n"
        "  Duration:         %llu ms\n"
        "  Worker Threads:   %u\n"
        "\n"
        "Performance Metrics:\n"
        "  Total Operations: %llu\n"
        "  Total Bytes:      %llu (%llu MB)\n"
        "  Total Errors:     %llu\n"
        "  Average Latency:  %llu ns\n"
        "  Throughput:       %llu MB/s\n"
        "  IOPS:             %llu\n"
        "\n"
        "Regression Analysis:\n"
        "  Baseline Latency: %llu ns\n"
        "  Latency Change:   %+lld ns (%+d%%)\n"
        "  Baseline Throughput: %llu MB/s\n"
        "  Throughput Change:   %+lld MB/s (%+d%%)\n"
        "\n"
        "Test Result:      %s\n"
        "Failure Reason:   %s\n"
        "\n",
        
        results.test_duration_ms,
        results.worker_threads,
        
        results.total_operations,
        results.total_bytes,
        results.total_bytes / (1024 * 1024),
        results.total_errors,
        results.current_avg_latency_ns,
        results.current_throughput_mb,
        dmr_stress_calculate_iops(results.total_operations, results.test_duration_ms),
        
        results.baseline_avg_latency_ns,
        results.latency_regression_ns,
        results.latency_regression_percent,
        results.baseline_throughput_mb,
        results.throughput_regression_mb,
        results.throughput_regression_percent,
        
        results.passed ? "PASSED" : "FAILED",
        results.passed ? "None" : results.failure_reason);
}

/**
 * stress_test_quick_validation_store - Run quick validation test
 */
static ssize_t stress_test_quick_validation_store(struct kobject *kobj,
                                                 struct kobj_attribute *attr,
                                                 const char *buf, size_t count)
{
    int run_test;
    int ret;
    
    if (!stress_test_target) {
        DMR_DEBUG(0, "No target available for stress testing");
        return -ENODEV;
    }
    
    ret = kstrtoint(buf, 10, &run_test);
    if (ret)
        return ret;
    
    if (run_test == 1) {
        /* Run quick 30-second mixed workload test with 4 workers */
        ret = dmr_stress_test_start(stress_test_target, DMR_STRESS_MIXED_WORKLOAD, 4, 30000);
        if (ret) {
            DMR_DEBUG(0, "Failed to start quick validation: %d", ret);
            return ret;
        }
        
        DMR_DEBUG(1, "Phase 3.2C: Started quick validation test");
    }
    
    return count;
}

/**
 * stress_test_regression_test_store - Run performance regression test
 */
static ssize_t stress_test_regression_test_store(struct kobject *kobj,
                                                struct kobj_attribute *attr,
                                                const char *buf, size_t count)
{
    int run_test;
    int ret;
    struct dmr_performance_regression_results results;
    
    if (!stress_test_target) {
        DMR_DEBUG(0, "No target available for regression testing");
        return -ENODEV;
    }
    
    ret = kstrtoint(buf, 10, &run_test);
    if (ret)
        return ret;
    
    if (run_test == 1) {
        ret = dmr_performance_regression_test(stress_test_target, &results);
        if (ret) {
            DMR_DEBUG(0, "Performance regression test failed: %d", ret);
            return ret;
        }
        
        DMR_DEBUG(1, "Phase 3.2C: Performance regression test %s",
                  results.passed ? "PASSED" : "FAILED");
        
        if (!results.passed) {
            DMR_DEBUG(1, "Regression details: %s", results.failure_reason);
        }
    }
    
    return count;
}

/**
 * stress_test_memory_pressure_store - Run memory pressure test
 */
static ssize_t stress_test_memory_pressure_store(struct kobject *kobj,
                                                struct kobj_attribute *attr,
                                                const char *buf, size_t count)
{
    int pressure_mb, duration_ms;
    int ret;
    
    if (!stress_test_target) {
        DMR_DEBUG(0, "No target available for memory pressure testing");
        return -ENODEV;
    }
    
    ret = sscanf(buf, "%d %d", &pressure_mb, &duration_ms);
    if (ret != 2) {
        DMR_DEBUG(0, "Usage: echo \"<pressure_mb> <duration_ms>\" > stress_test_memory_pressure");
        return -EINVAL;
    }
    
    if (pressure_mb <= 0 || pressure_mb > 1024) { /* Max 1GB pressure */
        DMR_DEBUG(0, "Invalid memory pressure: %d MB (max: 1024)", pressure_mb);
        return -EINVAL;
    }
    
    if (duration_ms <= 0 || duration_ms > 300000) { /* Max 5 minutes */
        DMR_DEBUG(0, "Invalid duration: %d ms (max: 300000)", duration_ms);
        return -EINVAL;
    }
    
    ret = dmr_memory_pressure_test(stress_test_target, pressure_mb * 1024 * 1024, duration_ms);
    if (ret) {
        DMR_DEBUG(0, "Memory pressure test failed: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C: Memory pressure test completed successfully");
    
    return count;
}

/**
 * stress_test_set_target_store - Set target for stress testing
 */
static ssize_t stress_test_set_target_store(struct kobject *kobj,
                                           struct kobj_attribute *attr,
                                           const char *buf, size_t count)
{
    /* For Phase 3.2C testing, we'll use a simplified approach that works 
     * with the existing global dm-remap instances rather than trying to 
     * dynamically discover device mapper devices using kernel internals 
     * that may not be available in all kernel versions. */
    
    char device_name[64];
    int ret;
    
    /* Parse device name from input */
    ret = sscanf(buf, "%63s", device_name);
    if (ret != 1) {
        DMR_DEBUG(0, "Phase 3.2C: Invalid device name format");
        return -EINVAL;
    }
    
    /* For testing purposes, we'll create a mock target */
    dmr_stress_test_set_target(NULL);  /* This will create the manager */
    
    /* Set NULL target to indicate testing mode */
    stress_test_target = NULL;
    
    DMR_DEBUG(1, "Phase 3.2C: Successfully set target %s for stress testing (simplified mode - NULL target)", device_name);
    return count;
}

/**
 * stress_test_comprehensive_report_show - Generate comprehensive test report
 */
static ssize_t stress_test_comprehensive_report_show(struct kobject *kobj,
                                                    struct kobj_attribute *attr, char *buf)
{
    struct dmr_performance_regression_results results;
    
    /* Get stress test results */
    dmr_stress_test_get_results(&results);
    
    return sprintf(buf,
        "=== Phase 3.2C Comprehensive Performance Report ===\n"
        "\n"
        "STRESS TEST RESULTS:\n"
        "  Test Status:      %s\n"
        "  Duration:         %llu ms\n"
        "  Workers:          %u threads\n"
        "  Operations:       %llu\n"
        "  Data Processed:   %llu MB\n"
        "  Errors:           %llu\n"
        "  Avg Latency:      %llu ns\n"
        "  Throughput:       %llu MB/s\n"
        "  IOPS:             %llu\n"
        "\n"
        "OVERALL ASSESSMENT:\n"
        "  Test Result:      %s\n"
        "  Performance:      %s\n"
        "  Reliability:      %s\n"
        "\n",
        
        dmr_stress_test_is_running() ? "RUNNING" : "COMPLETED",
        results.test_duration_ms,
        results.worker_threads,
        results.total_operations,
        results.total_bytes / (1024 * 1024),
        results.total_errors,
        results.current_avg_latency_ns,
        results.current_throughput_mb,
        dmr_stress_calculate_iops(results.total_operations, results.test_duration_ms),
        
        results.passed ? "PASSED" : "FAILED",
        results.current_avg_latency_ns < 1000 ? "EXCELLENT" : 
            results.current_avg_latency_ns < 2000 ? "GOOD" : "NEEDS_OPTIMIZATION",
        results.total_errors == 0 ? "STABLE" : "UNSTABLE");
}

/* Phase 3.2C: Sysfs attribute definitions */
static struct kobj_attribute stress_test_start_attr = 
    __ATTR(stress_test_start, 0200, NULL, stress_test_start_store);
static struct kobj_attribute stress_test_stop_attr = 
    __ATTR(stress_test_stop, 0200, NULL, stress_test_stop_store);
static struct kobj_attribute stress_test_status_attr = 
    __ATTR(stress_test_status, 0444, stress_test_status_show, NULL);
static struct kobj_attribute stress_test_results_attr = 
    __ATTR(stress_test_results, 0444, stress_test_results_show, NULL);
static struct kobj_attribute stress_test_quick_validation_attr = 
    __ATTR(quick_validation, 0200, NULL, stress_test_quick_validation_store);
static struct kobj_attribute stress_test_regression_test_attr = 
    __ATTR(regression_test, 0200, NULL, stress_test_regression_test_store);
static struct kobj_attribute stress_test_memory_pressure_attr = 
    __ATTR(memory_pressure_test, 0200, NULL, stress_test_memory_pressure_store);
static struct kobj_attribute stress_test_set_target_attr = 
    __ATTR(set_target, 0200, NULL, stress_test_set_target_store);
static struct kobj_attribute stress_test_comprehensive_report_attr = 
    __ATTR(comprehensive_report, 0444, stress_test_comprehensive_report_show, NULL);

/* Phase 3.2C: Attribute group */
static struct attribute *dmr_stress_attrs[] = {
    /* Test control */
    &stress_test_start_attr.attr,
    &stress_test_stop_attr.attr,
    &stress_test_status_attr.attr,
    &stress_test_set_target_attr.attr,
    
    /* Quick tests */
    &stress_test_quick_validation_attr.attr,
    &stress_test_regression_test_attr.attr,
    &stress_test_memory_pressure_attr.attr,
    
    /* Results and reporting */
    &stress_test_results_attr.attr,
    &stress_test_comprehensive_report_attr.attr,
    
    NULL,
};

static struct attribute_group dmr_stress_attr_group = {
    .attrs = dmr_stress_attrs,
};

/**
 * dmr_stress_sysfs_init - Initialize Phase 3.2C stress testing sysfs interface
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_stress_sysfs_init(void)
{
    int ret;
    
    /* Create stress testing kobject under /sys/kernel/ */
    dmr_stress_kobj = kobject_create_and_add("dm_remap_stress_test", kernel_kobj);
    if (!dmr_stress_kobj) {
        DMR_DEBUG(0, "Failed to create Phase 3.2C stress testing sysfs kobject");
        return -ENOMEM;
    }
    
    /* Create attribute group */
    ret = sysfs_create_group(dmr_stress_kobj, &dmr_stress_attr_group);
    if (ret) {
        DMR_DEBUG(0, "Failed to create Phase 3.2C stress testing sysfs attributes: %d", ret);
        kobject_put(dmr_stress_kobj);
        dmr_stress_kobj = NULL;
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C stress testing sysfs interface initialized at /sys/kernel/dm_remap_stress_test/");
    
    return 0;
}

/**
 * dmr_stress_sysfs_cleanup - Cleanup Phase 3.2C stress testing sysfs interface
 */
void dmr_stress_sysfs_cleanup(void)
{
    if (dmr_stress_kobj) {
        sysfs_remove_group(dmr_stress_kobj, &dmr_stress_attr_group);
        kobject_put(dmr_stress_kobj);
        dmr_stress_kobj = NULL;
    }
    
    DMR_DEBUG(1, "Phase 3.2C stress testing sysfs interface cleaned up");
}

/**
 * dmr_stress_sysfs_set_target - Set target for stress testing
 * @ti: Target instance
 */
void dmr_stress_sysfs_set_target(struct dm_target *ti)
{
    stress_test_target = ti;
    DMR_DEBUG(2, "Phase 3.2C: Stress test target set");
}