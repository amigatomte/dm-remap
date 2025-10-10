/*
 * dm-remap-stress-test.h - Phase 3.2C Production Performance Validation
 * 
 * This file implements comprehensive stress testing and performance validation
 * for the dm-remap system under production-like conditions.
 * 
 * KEY PHASE 3.2C FEATURES:
 * - Multi-threaded concurrent I/O stress testing
 * - Large dataset performance validation (TB-scale)
 * - Production workload simulation
 * - Performance regression testing framework
 * - Comprehensive benchmarking suite
 * - Real-time stress monitoring
 * 
 * VALIDATION TARGETS:
 * - Maintain <500ns average latency under 1000+ concurrent I/Os
 * - Handle >10,000 remap entries without performance degradation
 * - Process >1TB of data with consistent performance
 * - Zero memory leaks or resource exhaustion under stress
 * - Stable operation for 24+ hours continuous testing
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_STRESS_TEST_H
#define DM_REMAP_STRESS_TEST_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/atomic.h>
#include <linux/timer.h>
#include <linux/kthread.h>

/* Phase 3.2C: Stress test configuration constants */
#define DMR_STRESS_MAX_THREADS          32
#define DMR_STRESS_MAX_CONCURRENT_IOS   1000
#define DMR_STRESS_MAX_REMAP_ENTRIES    10000
#define DMR_STRESS_TEST_DURATION_MS     (60 * 1000)  /* 1 minute default */
#define DMR_STRESS_LATENCY_TARGET_NS    500          /* <500ns target */
#define DMR_STRESS_THROUGHPUT_TARGET_MB 100          /* 100 MB/s minimum */

/* Phase 3.2C: Stress test types */
enum dmr_stress_test_type {
    DMR_STRESS_SEQUENTIAL_READ = 0,
    DMR_STRESS_RANDOM_READ,
    DMR_STRESS_SEQUENTIAL_WRITE,
    DMR_STRESS_RANDOM_WRITE,
    DMR_STRESS_MIXED_WORKLOAD,
    DMR_STRESS_REMAP_HEAVY,
    DMR_STRESS_MEMORY_PRESSURE,
    DMR_STRESS_ENDURANCE,
    DMR_STRESS_MAX_TYPES
};

/* Phase 3.2C: Stress test worker thread context */
struct dmr_stress_worker {
    struct task_struct *thread;     /* Worker thread */
    int worker_id;                  /* Unique worker ID */
    enum dmr_stress_test_type type; /* Test type */
    struct dm_target *ti;           /* Target instance */
    
    /* Performance metrics */
    atomic64_t operations_completed;
    atomic64_t bytes_processed;
    atomic64_t total_latency_ns;
    atomic64_t max_latency_ns;
    atomic64_t min_latency_ns;
    atomic64_t errors_encountered;
    
    /* Control */
    bool should_stop;
    struct completion completion;
    
    /* Test-specific parameters */
    sector_t start_sector;
    sector_t end_sector;
    u32 io_size;
    u32 delay_ms;
} ____cacheline_aligned;

/* Phase 3.2C: Comprehensive stress test manager */
struct dmr_stress_test_manager {
    /* Test configuration */
    struct dm_target *target;
    enum dmr_stress_test_type test_type;
    u32 num_workers;
    u32 test_duration_ms;
    u32 target_latency_ns;
    u32 target_throughput_mb;
    
    /* Worker threads */
    struct dmr_stress_worker workers[DMR_STRESS_MAX_THREADS];
    
    /* Global test metrics */
    atomic64_t total_operations;
    atomic64_t total_bytes;
    atomic64_t total_errors;
    atomic64_t peak_concurrent_ios;
    
    /* Test control */
    struct timer_list test_timer;
    bool test_running;
    struct completion test_completion;
    ktime_t test_start_time;
    ktime_t test_end_time;
    
    /* Performance monitoring */
    struct workqueue_struct *monitor_wq;
    struct delayed_work monitor_work;
    u32 monitor_interval_ms;
    
    /* Memory pressure simulation */
    void **memory_pressure_buffers;
    u32 memory_pressure_count;
    size_t memory_pressure_size;
} ____cacheline_aligned;

/* Phase 3.2C: Performance regression test results */
struct dmr_performance_regression_results {
    /* Baseline vs current comparison */
    u64 baseline_avg_latency_ns;
    u64 current_avg_latency_ns;
    s64 latency_regression_ns;
    s32 latency_regression_percent;
    
    u64 baseline_throughput_mb;
    u64 current_throughput_mb;
    s64 throughput_regression_mb;
    s32 throughput_regression_percent;
    
    /* Test outcome */
    bool passed;
    char failure_reason[256];
    
    /* Detailed statistics */
    u64 total_operations;
    u64 total_bytes;
    u64 total_errors;
    u64 test_duration_ms;
    u32 worker_threads;
    u32 concurrent_ios_peak;
};

/* Phase 3.2C: Large dataset validation parameters */
struct dmr_large_dataset_test_params {
    u64 dataset_size_gb;            /* Total dataset size in GB */
    u32 remap_density_percent;      /* Percentage of sectors remapped */
    u32 access_pattern;             /* Sequential, random, or mixed */
    u32 concurrent_threads;         /* Number of concurrent threads */
    bool enable_verification;       /* Enable data integrity checks */
    bool simulate_failures;         /* Simulate device failures */
};

/* Function declarations */

/* Stress test management */
int dmr_stress_test_init(void);
void dmr_stress_test_cleanup(void);
void dmr_stress_test_set_target(struct dm_target *ti);
int dmr_stress_test_start(struct dm_target *ti, enum dmr_stress_test_type type,
                         u32 num_workers, u32 duration_ms);
void dmr_stress_test_stop(void);
bool dmr_stress_test_is_running(void);

/* Performance regression testing */
int dmr_performance_regression_test(struct dm_target *ti,
                                   struct dmr_performance_regression_results *results);
int dmr_compare_with_baseline(struct dmr_performance_regression_results *results);

/* Large dataset validation */
int dmr_large_dataset_validation(struct dm_target *ti,
                                struct dmr_large_dataset_test_params *params);

/* Production workload simulation */
int dmr_simulate_database_workload(struct dm_target *ti, u32 duration_ms);
int dmr_simulate_file_server_workload(struct dm_target *ti, u32 duration_ms);
int dmr_simulate_virtualization_workload(struct dm_target *ti, u32 duration_ms);

/* Stress test results and monitoring */
void dmr_stress_test_get_results(struct dmr_performance_regression_results *results);
void dmr_stress_test_print_summary(void);
int dmr_stress_test_export_results(char *buffer, size_t buffer_size);

/* Memory and resource pressure testing */
int dmr_memory_pressure_test(struct dm_target *ti, size_t pressure_mb, u32 duration_ms);
int dmr_resource_exhaustion_test(struct dm_target *ti);

/* Endurance and stability testing */
int dmr_endurance_test_start(struct dm_target *ti, u32 hours);
void dmr_endurance_test_stop(void);
bool dmr_endurance_test_is_running(void);

/* Sysfs interface functions */
int dmr_stress_sysfs_init(void);
void dmr_stress_sysfs_cleanup(void);
void dmr_stress_sysfs_set_target(struct dm_target *ti);

#endif /* DM_REMAP_STRESS_TEST_H */