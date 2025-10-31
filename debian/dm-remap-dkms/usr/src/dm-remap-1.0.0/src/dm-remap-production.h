/*
 * dm-remap-production.h - Production hardening features for dm-remap v2.0
 * 
 * This header defines production-ready enhancements including:
 * - Enhanced error recovery and classification
 * - Memory allocation failure handling
 * - Performance monitoring and throttling
 * - Comprehensive logging and audit trails
 * - Resource leak prevention
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#ifndef DM_REMAP_PRODUCTION_H
#define DM_REMAP_PRODUCTION_H

#include <linux/types.h>
#include <linux/time.h>

/*
 * PRODUCTION ERROR CLASSIFICATION
 * 
 * More sophisticated error classification for better recovery strategies
 */
#define DMR_ERROR_TRANSIENT    0x01  /* Temporary error, likely recoverable */
#define DMR_ERROR_PERSISTENT   0x02  /* Persistent error, needs remapping */
#define DMR_ERROR_CRITICAL     0x04  /* Critical error, immediate attention */
#define DMR_ERROR_HARDWARE     0x08  /* Hardware failure indicated */
#define DMR_ERROR_TIMEOUT      0x10  /* Timeout-related error */
#define DMR_ERROR_MEMORY       0x20  /* Memory allocation failure */
#define DMR_ERROR_DEVICE       0x40  /* Device unavailable/disconnected */

/*
 * PRODUCTION RETRY POLICY
 * 
 * Enhanced retry logic with exponential backoff and adaptive thresholds
 */
struct dmr_retry_policy {
    u32 max_retries;              /* Maximum retry attempts */
    u32 base_delay_ms;            /* Base delay in milliseconds */
    u32 max_delay_ms;             /* Maximum delay cap */
    u32 backoff_multiplier;       /* Exponential backoff multiplier */
    u32 adaptive_threshold;       /* Adaptive retry threshold */
    bool enable_fast_fail;        /* Enable fast failure for critical errors */
};

/*
 * PRODUCTION HEALTH METRICS
 * 
 * Comprehensive health metrics for production monitoring
 */
struct dmr_health_metrics {
    /* Error rate tracking */
    u64 total_ios;                /* Total I/O operations */
    u64 total_errors;             /* Total errors encountered */
    u64 error_rate_per_million;   /* Errors per million operations */
    
    /* Latency tracking */
    u64 total_latency_ns;         /* Total latency in nanoseconds */
    u64 avg_latency_ns;           /* Average latency */
    u64 max_latency_ns;           /* Maximum latency observed */
    
    /* Resource usage */
    u32 active_bio_contexts;      /* Currently active bio contexts */
    u32 peak_bio_contexts;        /* Peak bio contexts */
    u32 memory_usage_kb;          /* Estimated memory usage */
    
    /* Auto-remap effectiveness */
    u32 successful_remaps;        /* Successful automatic remaps */
    u32 failed_remaps;            /* Failed remap attempts */
    u32 remap_success_rate;       /* Success rate percentage */
    
    /* Timestamps */
    u64 last_error_time;          /* Last error timestamp */
    u64 last_remap_time;          /* Last successful remap */
    u64 uptime_seconds;           /* System uptime */
};

/*
 * PRODUCTION THROTTLING
 * 
 * I/O throttling to prevent system overload during recovery
 */
struct dmr_throttle_config {
    bool enable_throttling;       /* Enable I/O throttling */
    u32 error_threshold;          /* Error count to trigger throttling */
    u32 throttle_delay_ms;        /* Delay to inject per I/O */
    u32 throttle_duration_sec;    /* How long to maintain throttling */
    u64 last_throttle_time;       /* Last throttling activation */
};

/*
 * PRODUCTION AUDIT LOG
 * 
 * Structured audit logging for production environments
 */
struct dmr_audit_entry {
    u64 timestamp;                /* Event timestamp */
    u32 event_type;               /* Event type identifier */
    sector_t sector;              /* Affected sector */
    u32 error_code;               /* Error code if applicable */
    char description[128];        /* Human-readable description */
};

#define DMR_AUDIT_MAX_ENTRIES 1000
#define DMR_AUDIT_EVENT_ERROR    1
#define DMR_AUDIT_EVENT_REMAP    2
#define DMR_AUDIT_EVENT_RECOVERY 3
#define DMR_AUDIT_EVENT_THROTTLE 4

/*
 * PRODUCTION CONTEXT EXTENSION
 * 
 * Additional fields for production hardening in the main context
 */
struct dmr_production_context {
    /* Enhanced error recovery */
    struct dmr_retry_policy retry_policy;
    struct dmr_health_metrics health_metrics;
    struct dmr_throttle_config throttle_config;
    
    /* Audit logging */
    struct dmr_audit_entry *audit_log;
    u32 audit_head;               /* Circular buffer head */
    u32 audit_count;              /* Number of entries */
    spinlock_t audit_lock;        /* Audit log protection */
    
    /* Memory management */
    atomic_t bio_context_count;   /* Active bio context counter */
    u32 memory_pressure_level;    /* Current memory pressure (0-100) */
    bool emergency_mode;          /* Emergency mode flag */
    
    /* Performance monitoring */
    u64 performance_baseline_ns;  /* Performance baseline */
    u32 degradation_threshold;    /* Performance degradation threshold */
    bool performance_alert;       /* Performance alert flag */
};

/*
 * PRODUCTION FUNCTION DECLARATIONS
 */

/* Error classification and recovery */
u32 dmr_classify_error(int error_code, u32 retry_count);
bool dmr_should_retry_with_policy(struct dmr_retry_policy *policy, 
                                  u32 error_class, u32 retry_count);
u32 dmr_calculate_retry_delay_adaptive(struct dmr_retry_policy *policy, 
                                       u32 retry_count, u32 error_class);

/* Health metrics and monitoring */
void dmr_update_health_metrics(struct dmr_production_context *prod_ctx,
                               bool was_error, u64 latency_ns);
void dmr_check_performance_degradation(struct dmr_production_context *prod_ctx);
bool dmr_should_activate_emergency_mode(struct dmr_production_context *prod_ctx);

/* Throttling and load management */
void dmr_evaluate_throttling(struct dmr_production_context *prod_ctx);
u32 dmr_calculate_throttle_delay(struct dmr_throttle_config *config);
void dmr_apply_io_throttling(u32 delay_ms);

/* Audit logging */
void dmr_audit_log_event(struct dmr_production_context *prod_ctx,
                         u32 event_type, sector_t sector, 
                         u32 error_code, const char *description);
void dmr_audit_log_export(struct dmr_production_context *prod_ctx, 
                          char *buffer, size_t buffer_size);

/* Memory management */
bool dmr_check_memory_pressure(void);
void dmr_emergency_cleanup(struct dmr_production_context *prod_ctx);
bool dmr_allocate_with_fallback(void **ptr, size_t size, gfp_t flags);

/* Production initialization and cleanup */
int dmr_production_init(struct dmr_production_context *prod_ctx);
void dmr_production_cleanup(struct dmr_production_context *prod_ctx);

#endif /* DM_REMAP_PRODUCTION_H */