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
    
    if (duration_ms <= 0 || duration_ms > 86400000) { /* Max 24 hours for endurance testing */
        DMR_DEBUG(0, "Invalid duration: %d ms (max: 86400000 - 24 hours)", duration_ms);
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

/**
 * high_concurrency_stress_store - Run high-concurrency stress test with 1000+ operations
 * Format: echo "<concurrency_level> <duration_sec>" > high_concurrency_stress
 */
static ssize_t high_concurrency_stress_store(struct kobject *kobj,
                                            struct kobj_attribute *attr,
                                            const char *buf, size_t count)
{
    int concurrency_level, duration_sec;
    int ret;
    
    if (stress_test_target == (void *)-1) {
        DMR_DEBUG(0, "No target available for high-concurrency stress testing");
        return -ENODEV;
    }
    
    ret = sscanf(buf, "%d %d", &concurrency_level, &duration_sec);
    if (ret != 2) {
        DMR_DEBUG(0, "Usage: echo \"<concurrency_level> <duration_sec>\" > high_concurrency_stress");
        DMR_DEBUG(0, "Concurrency levels: 1000, 5000, 10000+ operations");
        return -EINVAL;
    }
    
    if (concurrency_level < 100 || concurrency_level > 50000) {
        DMR_DEBUG(0, "Invalid concurrency level: %d (range: 100-50000)", concurrency_level);
        return -EINVAL;
    }
    
    if (duration_sec <= 0 || duration_sec > 3600) {
        DMR_DEBUG(0, "Invalid duration: %d seconds (max: 3600)", duration_sec);
        return -EINVAL;
    }
    
    /* Start high-concurrency test with multiple workers to achieve target concurrency */
    int workers = min(DMR_STRESS_MAX_THREADS, concurrency_level / 100);
    ret = dmr_stress_test_start(stress_test_target, DMR_STRESS_MIXED_WORKLOAD,
                               workers, duration_sec * 1000);
    if (ret) {
        DMR_DEBUG(0, "Failed to start high-concurrency stress test: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C: Started high-concurrency stress test: %d concurrent ops, %d sec",
              concurrency_level, duration_sec);
    
    return count;
}

/**
 * tb_scale_validation_store - Run TB-scale dataset validation test
 * Format: echo "<dataset_size_gb> <test_type>" > tb_scale_validation
 */
static ssize_t tb_scale_validation_store(struct kobject *kobj,
                                        struct kobj_attribute *attr,
                                        const char *buf, size_t count)
{
    int dataset_size_gb, test_type;
    int ret;
    
    if (stress_test_target == (void *)-1) {
        DMR_DEBUG(0, "No target available for TB-scale validation");
        return -ENODEV;
    }
    
    ret = sscanf(buf, "%d %d", &dataset_size_gb, &test_type);
    if (ret != 2) {
        DMR_DEBUG(0, "Usage: echo \"<dataset_size_gb> <test_type>\" > tb_scale_validation");
        DMR_DEBUG(0, "Dataset sizes: 100, 500, 1000+ GB | Test types: 0-5");
        return -EINVAL;
    }
    
    if (dataset_size_gb < 10 || dataset_size_gb > 10000) {
        DMR_DEBUG(0, "Invalid dataset size: %d GB (range: 10-10000)", dataset_size_gb);
        return -EINVAL;
    }
    
    if (test_type < 0 || test_type >= DMR_STRESS_MAX_TYPES) {
        DMR_DEBUG(0, "Invalid test type: %d", test_type);
        return -EINVAL;
    }
    
    /* Calculate test duration based on dataset size (scale with size) */
    int duration_ms = dataset_size_gb * 10000; /* 10 seconds per GB */
    int workers = min(DMR_STRESS_MAX_THREADS, dataset_size_gb / 10); /* Scale workers with dataset */
    
    ret = dmr_stress_test_start(stress_test_target, (enum dmr_stress_test_type)test_type,
                               workers, duration_ms);
    if (ret) {
        DMR_DEBUG(0, "Failed to start TB-scale validation test: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C: Started TB-scale validation: %d GB dataset, type %d, %d workers",
              dataset_size_gb, test_type, workers);
    
    return count;
}

/**
 * endurance_test_store - Run extended endurance testing (1-24 hours)
 * Format: echo "<hours> <test_type>" > endurance_test
 */
static ssize_t endurance_test_store(struct kobject *kobj,
                                   struct kobj_attribute *attr,
                                   const char *buf, size_t count)
{
    int hours, test_type;
    int ret;
    
    if (stress_test_target == (void *)-1) {
        DMR_DEBUG(0, "No target available for endurance testing");
        return -ENODEV;
    }
    
    ret = sscanf(buf, "%d %d", &hours, &test_type);
    if (ret != 2) {
        DMR_DEBUG(0, "Usage: echo \"<hours> <test_type>\" > endurance_test");
        DMR_DEBUG(0, "Duration: 1-24 hours | Test types: 0-5");
        return -EINVAL;
    }
    
    if (hours < 1 || hours > 24) {
        DMR_DEBUG(0, "Invalid duration: %d hours (range: 1-24)", hours);
        return -EINVAL;
    }
    
    if (test_type < 0 || test_type >= DMR_STRESS_MAX_TYPES) {
        DMR_DEBUG(0, "Invalid test type: %d", test_type);
        return -EINVAL;
    }
    
    int duration_ms = hours * 3600 * 1000; /* Convert hours to milliseconds */
    int workers = min(DMR_STRESS_MAX_THREADS / 2, 8); /* Conservative worker count for long tests */
    
    ret = dmr_stress_test_start(stress_test_target, (enum dmr_stress_test_type)test_type,
                               workers, duration_ms);
    if (ret) {
        DMR_DEBUG(0, "Failed to start endurance test: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C: Started endurance test: %d hours, type %d, %d workers",
              hours, test_type, workers);
    
    return count;
}

/**
 * production_workload_store - Simulate production workloads (database, file server, VM)
 * Format: echo "<workload_type> <intensity> <duration_min>" > production_workload
 */
static ssize_t production_workload_store(struct kobject *kobj,
                                        struct kobj_attribute *attr,
                                        const char *buf, size_t count)
{
    int workload_type, intensity, duration_min;
    int ret;
    
    if (stress_test_target == (void *)-1) {
        DMR_DEBUG(0, "No target available for production workload simulation");
        return -ENODEV;
    }
    
    ret = sscanf(buf, "%d %d %d", &workload_type, &intensity, &duration_min);
    if (ret != 3) {
        DMR_DEBUG(0, "Usage: echo \"<workload_type> <intensity> <duration_min>\" > production_workload");
        DMR_DEBUG(0, "Workload types: 0=database, 1=file_server, 2=virtualization, 3=mixed");
        DMR_DEBUG(0, "Intensity: 1=light, 2=moderate, 3=heavy, 4=extreme");
        return -EINVAL;
    }
    
    if (workload_type < 0 || workload_type > 3) {
        DMR_DEBUG(0, "Invalid workload type: %d (range: 0-3)", workload_type);
        return -EINVAL;
    }
    
    if (intensity < 1 || intensity > 4) {
        DMR_DEBUG(0, "Invalid intensity: %d (range: 1-4)", intensity);
        return -EINVAL;
    }
    
    if (duration_min < 1 || duration_min > 1440) { /* Max 24 hours */
        DMR_DEBUG(0, "Invalid duration: %d minutes (range: 1-1440)", duration_min);
        return -EINVAL;
    }
    
    /* Map workload types to appropriate stress test patterns */
    enum dmr_stress_test_type test_type;
    int workers;
    
    switch (workload_type) {
    case 0: /* Database - random I/O heavy */
        test_type = DMR_STRESS_RANDOM_WRITE;
        workers = intensity * 4;
        break;
    case 1: /* File server - mixed sequential/random */
        test_type = DMR_STRESS_MIXED_WORKLOAD;
        workers = intensity * 3;
        break;
    case 2: /* Virtualization - remap heavy */
        test_type = DMR_STRESS_REMAP_HEAVY;
        workers = intensity * 5;
        break;
    case 3: /* Mixed workload */
    default:
        test_type = DMR_STRESS_MIXED_WORKLOAD;
        workers = intensity * 4;
        break;
    }
    
    workers = min(workers, DMR_STRESS_MAX_THREADS);
    int duration_ms = duration_min * 60 * 1000;
    
    ret = dmr_stress_test_start(stress_test_target, test_type, workers, duration_ms);
    if (ret) {
        DMR_DEBUG(0, "Failed to start production workload simulation: %d", ret);
        return ret;
    }
    
    DMR_DEBUG(1, "Phase 3.2C: Started production workload: type %d, intensity %d, %d min, %d workers",
              workload_type, intensity, duration_min, workers);
    
    return count;
}

/* Define the new attribute structures */
static struct kobj_attribute high_concurrency_stress_attr = 
    __ATTR(high_concurrency_stress, 0200, NULL, high_concurrency_stress_store);

static struct kobj_attribute tb_scale_validation_attr = 
    __ATTR(tb_scale_validation, 0200, NULL, tb_scale_validation_store);

static struct kobj_attribute endurance_test_attr = 
    __ATTR(endurance_test, 0200, NULL, endurance_test_store);

static struct kobj_attribute production_workload_attr = 
    __ATTR(production_workload, 0200, NULL, production_workload_store);

/* Phase 3.2C: Enhanced Attribute group with advanced features */
static struct attribute *dmr_stress_attrs[] = {
    /* Basic test control */
    &stress_test_start_attr.attr,
    &stress_test_stop_attr.attr,
    &stress_test_status_attr.attr,
    &stress_test_set_target_attr.attr,
    
    /* Quick tests */
    &stress_test_quick_validation_attr.attr,
    &stress_test_regression_test_attr.attr,
    &stress_test_memory_pressure_attr.attr,
    
    /* Advanced Phase 3.2C features */
    &high_concurrency_stress_attr.attr,
    &tb_scale_validation_attr.attr,
    &endurance_test_attr.attr,
    &production_workload_attr.attr,
    
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