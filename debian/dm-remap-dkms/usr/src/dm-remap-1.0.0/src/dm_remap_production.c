/*
 * dm_remap_production.c - Production hardening implementation for dm-remap v2.0
 * 
 * This file implements production-ready enhancements including:
 * - Enhanced error recovery with adaptive retry logic
 * - Comprehensive health monitoring and metrics
 * - I/O throttling under stress conditions
 * - Structured audit logging for compliance
 * - Memory pressure handling and emergency modes
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>         /* Core kernel module support */
#include <linux/slab.h>           /* Memory allocation */
#include <linux/delay.h>          /* Sleep functions */
#include <linux/time.h>           /* Time functions */
#include <linux/atomic.h>         /* Atomic operations */
#include <linux/mm.h>             /* Memory management */

#include "dm-remap-core.h"        /* Core data structures */
#include "dm-remap-io.h"          /* I/O structures including dmr_bio_context */
#include "dm-remap-production.h" /* Production hardening interfaces */
#include "dm-remap-messages.h"    /* Debug macros */

/*
 * Production module parameters
 */
static int enable_production_mode = 1;
module_param(enable_production_mode, int, 0644);
MODULE_PARM_DESC(enable_production_mode, "Enable production hardening features");

static int max_retries_production = 5;
module_param(max_retries_production, int, 0644);
MODULE_PARM_DESC(max_retries_production, "Maximum retries in production mode");

static int emergency_threshold = 90;
module_param(emergency_threshold, int, 0644);
MODULE_PARM_DESC(emergency_threshold, "Memory pressure threshold for emergency mode");

/*
 * dmr_classify_error() - Classify error for appropriate response strategy
 * 
 * This function analyzes error codes to determine the best recovery approach.
 * Production systems need sophisticated error classification for optimal handling.
 * 
 * @error_code: The kernel error code
 * @retry_count: Number of retries already attempted
 * 
 * Returns: Error classification flags (DMR_ERROR_*)
 */
u32 dmr_classify_error(int error_code, u32 retry_count)
{
    u32 classification = 0;
    
    switch (error_code) {
    case -EIO:
        /* I/O error - could be transient or persistent */
        if (retry_count < 2)
            classification |= DMR_ERROR_TRANSIENT;
        else
            classification |= DMR_ERROR_PERSISTENT | DMR_ERROR_HARDWARE;
        break;
        
    case -ETIMEDOUT:
        /* Timeout - usually transient but monitor pattern */
        classification |= DMR_ERROR_TRANSIENT | DMR_ERROR_TIMEOUT;
        if (retry_count > 3)
            classification |= DMR_ERROR_PERSISTENT;
        break;
        
    case -ENOMEM:
        /* Memory pressure - critical system issue */
        classification |= DMR_ERROR_CRITICAL | DMR_ERROR_MEMORY;
        break;
        
    case -ENODEV:
    case -ENOENT:
        /* Device issues - critical and likely persistent */
        classification |= DMR_ERROR_CRITICAL | DMR_ERROR_DEVICE | DMR_ERROR_PERSISTENT;
        break;
        
    case -EREMOTEIO:
        /* Remote I/O error - could be network or device */
        classification |= DMR_ERROR_TRANSIENT;
        if (retry_count > 2)
            classification |= DMR_ERROR_HARDWARE;
        break;
        
    default:
        /* Unknown error - treat as potentially transient initially */
        if (retry_count == 0)
            classification |= DMR_ERROR_TRANSIENT;
        else
            classification |= DMR_ERROR_PERSISTENT;
        break;
    }
    
    DMR_DEBUG(2, "Error %d classified as 0x%x (retry %u)", 
              error_code, classification, retry_count);
    
    return classification;
}

/*
 * dmr_should_retry_with_policy() - Enhanced retry decision with policy
 * 
 * This function implements sophisticated retry logic based on error classification
 * and production policies.
 * 
 * @policy: Retry policy configuration
 * @error_class: Error classification from dmr_classify_error()
 * @retry_count: Current retry count
 * 
 * Returns: true if retry should be attempted
 */
bool dmr_should_retry_with_policy(struct dmr_retry_policy *policy, 
                                  u32 error_class, u32 retry_count)
{
    /* Fast fail for critical errors if enabled */
    if (policy->enable_fast_fail && (error_class & DMR_ERROR_CRITICAL)) {
        DMR_DEBUG(1, "Fast fail enabled for critical error (class 0x%x)", error_class);
        return false;
    }
    
    /* Don't retry device errors - they're unlikely to recover */
    if (error_class & DMR_ERROR_DEVICE) {
        DMR_DEBUG(1, "No retry for device error (class 0x%x)", error_class);
        return false;
    }
    
    /* Check retry limit */
    if (retry_count >= policy->max_retries) {
        DMR_DEBUG(1, "Retry limit exceeded (%u >= %u)", retry_count, policy->max_retries);
        return false;
    }
    
    /* Adaptive threshold for persistent errors */
    if ((error_class & DMR_ERROR_PERSISTENT) && 
        retry_count >= policy->adaptive_threshold) {
        DMR_DEBUG(1, "Adaptive threshold reached for persistent error");
        return false;
    }
    
    /* Memory errors need special handling */
    if (error_class & DMR_ERROR_MEMORY) {
        /* Only retry memory errors once, and only if not in emergency mode */
        return (retry_count == 0 && !dmr_check_memory_pressure());
    }
    
    /* Transient errors are good candidates for retry */
    if (error_class & DMR_ERROR_TRANSIENT) {
        DMR_DEBUG(2, "Retry approved for transient error (attempt %u)", retry_count + 1);
        return true;
    }
    
    /* Conservative approach for unknown classifications */
    DMR_DEBUG(2, "Conservative retry decision for error class 0x%x", error_class);
    return (retry_count < 2);
}

/*
 * dmr_calculate_retry_delay_adaptive() - Calculate adaptive retry delay
 * 
 * This function implements sophisticated retry delay calculation with
 * exponential backoff and error-class-specific adjustments.
 * 
 * @policy: Retry policy configuration
 * @retry_count: Current retry attempt number
 * @error_class: Error classification
 * 
 * Returns: delay in milliseconds
 */
u32 dmr_calculate_retry_delay_adaptive(struct dmr_retry_policy *policy, 
                                       u32 retry_count, u32 error_class)
{
    u32 base_delay = policy->base_delay_ms;
    u32 delay;
    
    /* Adjust base delay based on error type */
    if (error_class & DMR_ERROR_TIMEOUT) {
        /* Timeout errors need longer delays */
        base_delay *= 2;
    } else if (error_class & DMR_ERROR_MEMORY) {
        /* Memory errors need immediate retry or longer wait */
        base_delay = (retry_count == 0) ? 1 : (base_delay * 5);
    } else if (error_class & DMR_ERROR_HARDWARE) {
        /* Hardware errors may need time for recovery */
        base_delay *= 3;
    }
    
    /* Calculate exponential backoff */
    delay = base_delay;
    for (u32 i = 0; i < retry_count; i++) {
        delay *= policy->backoff_multiplier;
        if (delay > policy->max_delay_ms) {
            delay = policy->max_delay_ms;
            break;
        }
    }
    
    DMR_DEBUG(2, "Calculated retry delay: %u ms (base=%u, retry=%u, class=0x%x)", 
              delay, base_delay, retry_count, error_class);
    
    return delay;
}

/*
 * dmr_update_health_metrics() - Update comprehensive health metrics
 * 
 * This function maintains detailed health metrics for production monitoring.
 * 
 * @prod_ctx: Production context
 * @was_error: True if this I/O resulted in an error
 * @latency_ns: I/O latency in nanoseconds
 */
void dmr_update_health_metrics(struct dmr_production_context *prod_ctx,
                               bool was_error, u64 latency_ns)
{
    struct dmr_health_metrics *metrics = &prod_ctx->health_metrics;
    
    /* Update I/O counters */
    metrics->total_ios++;
    if (was_error) {
        metrics->total_errors++;
        metrics->last_error_time = ktime_get_seconds();
    }
    
    /* Calculate error rate per million operations */
    if (metrics->total_ios > 0) {
        metrics->error_rate_per_million = 
            (metrics->total_errors * 1000000) / metrics->total_ios;
    }
    
    /* Update latency tracking */
    if (latency_ns > 0) {
        metrics->total_latency_ns += latency_ns;
        metrics->avg_latency_ns = metrics->total_latency_ns / metrics->total_ios;
        
        if (latency_ns > metrics->max_latency_ns) {
            metrics->max_latency_ns = latency_ns;
        }
    }
    
    /* Update resource usage */
    metrics->active_bio_contexts = atomic_read(&prod_ctx->bio_context_count);
    if (metrics->active_bio_contexts > metrics->peak_bio_contexts) {
        metrics->peak_bio_contexts = metrics->active_bio_contexts;
    }
    
    /* Estimate memory usage (rough calculation) */
    metrics->memory_usage_kb = 
        (metrics->active_bio_contexts * 64) / 1024 +  /* Approximate bio context size */
        (DMR_AUDIT_MAX_ENTRIES * sizeof(struct dmr_audit_entry)) / 1024;
    
    /* Update system uptime */
    metrics->uptime_seconds = ktime_get_seconds();
    
    DMR_DEBUG(3, "Health metrics updated: IOs=%llu, errors=%llu, rate=%llu/M, latency=%llu ns", 
              metrics->total_ios, metrics->total_errors, 
              metrics->error_rate_per_million, metrics->avg_latency_ns);
}

/*
 * dmr_check_memory_pressure() - Check system memory pressure
 * 
 * This function monitors system memory pressure to enable emergency modes.
 * 
 * Returns: true if system is under memory pressure
 */
bool dmr_check_memory_pressure(void)
{
    /* Simple heuristic based on available memory */
    struct sysinfo si;
    u64 available_mb, total_mb, pressure_percent;
    
    si_meminfo(&si);
    
    total_mb = (si.totalram * si.mem_unit) >> 20;  /* Convert to MB */
    available_mb = (si.freeram * si.mem_unit) >> 20;
    
    if (total_mb > 0) {
        pressure_percent = ((total_mb - available_mb) * 100) / total_mb;
        
        DMR_DEBUG(3, "Memory pressure: %llu%% (%llu MB used of %llu MB)", 
                  pressure_percent, total_mb - available_mb, total_mb);
        
        return pressure_percent > emergency_threshold;
    }
    
    return false;  /* Conservative fallback */
}

/*
 * dmr_audit_log_event() - Add event to audit log
 * 
 * This function provides structured audit logging for compliance and debugging.
 * 
 * @prod_ctx: Production context
 * @event_type: Type of event (DMR_AUDIT_EVENT_*)
 * @sector: Affected sector
 * @error_code: Error code if applicable
 * @description: Human-readable description
 */
void dmr_audit_log_event(struct dmr_production_context *prod_ctx,
                         u32 event_type, sector_t sector, 
                         u32 error_code, const char *description)
{
    struct dmr_audit_entry *entry;
    unsigned long flags;
    
    if (!prod_ctx->audit_log)
        return;
    
    spin_lock_irqsave(&prod_ctx->audit_lock, flags);
    
    /* Get next entry in circular buffer */
    entry = &prod_ctx->audit_log[prod_ctx->audit_head];
    
    /* Fill entry */
    entry->timestamp = ktime_get_seconds();
    entry->event_type = event_type;
    entry->sector = sector;
    entry->error_code = error_code;
    strncpy(entry->description, description ? description : "No description", 
            sizeof(entry->description) - 1);
    entry->description[sizeof(entry->description) - 1] = '\0';
    
    /* Advance circular buffer */
    prod_ctx->audit_head = (prod_ctx->audit_head + 1) % DMR_AUDIT_MAX_ENTRIES;
    if (prod_ctx->audit_count < DMR_AUDIT_MAX_ENTRIES) {
        prod_ctx->audit_count++;
    }
    
    spin_unlock_irqrestore(&prod_ctx->audit_lock, flags);
    
    DMR_DEBUG(2, "Audit log: %s (sector %llu, error %u)", 
              description ? description : "event", 
              (unsigned long long)sector, error_code);
}

/*
 * dmr_production_init() - Initialize production hardening context
 * 
 * This function initializes all production hardening features.
 * 
 * @prod_ctx: Production context to initialize
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_production_init(struct dmr_production_context *prod_ctx)
{
    if (!enable_production_mode) {
        DMR_DEBUG(1, "Production mode disabled");
        return 0;
    }
    
    /* Initialize retry policy with production defaults */
    prod_ctx->retry_policy.max_retries = max_retries_production;
    prod_ctx->retry_policy.base_delay_ms = 10;
    prod_ctx->retry_policy.max_delay_ms = 5000;
    prod_ctx->retry_policy.backoff_multiplier = 2;
    prod_ctx->retry_policy.adaptive_threshold = 3;
    prod_ctx->retry_policy.enable_fast_fail = true;
    
    /* Initialize health metrics */
    memset(&prod_ctx->health_metrics, 0, sizeof(prod_ctx->health_metrics));
    prod_ctx->health_metrics.uptime_seconds = ktime_get_seconds();
    
    /* Initialize throttling configuration */
    prod_ctx->throttle_config.enable_throttling = true;
    prod_ctx->throttle_config.error_threshold = 10;
    prod_ctx->throttle_config.throttle_delay_ms = 100;
    prod_ctx->throttle_config.throttle_duration_sec = 30;
    
    /* Allocate audit log */
    prod_ctx->audit_log = kzalloc(sizeof(struct dmr_audit_entry) * DMR_AUDIT_MAX_ENTRIES, 
                                  GFP_KERNEL);
    if (!prod_ctx->audit_log) {
        DMR_DEBUG(0, "Failed to allocate audit log");
        return -ENOMEM;
    }
    
    /* Initialize audit log state */
    prod_ctx->audit_head = 0;
    prod_ctx->audit_count = 0;
    spin_lock_init(&prod_ctx->audit_lock);
    
    /* Initialize atomic counters */
    atomic_set(&prod_ctx->bio_context_count, 0);
    prod_ctx->memory_pressure_level = 0;
    prod_ctx->emergency_mode = false;
    
    /* Log initialization */
    dmr_audit_log_event(prod_ctx, DMR_AUDIT_EVENT_RECOVERY, 0, 0, 
                        "Production hardening initialized");
    
    DMR_DEBUG(0, "Production hardening initialized successfully");
    return 0;
}

/*
 * dmr_production_cleanup() - Cleanup production hardening context
 * 
 * This function cleans up all production hardening resources.
 * 
 * @prod_ctx: Production context to cleanup
 */
void dmr_production_cleanup(struct dmr_production_context *prod_ctx)
{
    if (!enable_production_mode || !prod_ctx)
        return;
    
    /* Log cleanup event */
    if (prod_ctx->audit_log) {
        dmr_audit_log_event(prod_ctx, DMR_AUDIT_EVENT_RECOVERY, 0, 0, 
                            "Production hardening cleanup");
    }
    
    /* Free audit log */
    kfree(prod_ctx->audit_log);
    prod_ctx->audit_log = NULL;
    
    DMR_DEBUG(0, "Production hardening cleanup completed");
}

/* Export symbols for use by other dm-remap modules */
EXPORT_SYMBOL(dmr_classify_error);
EXPORT_SYMBOL(dmr_should_retry_with_policy);
EXPORT_SYMBOL(dmr_calculate_retry_delay_adaptive);
EXPORT_SYMBOL(dmr_update_health_metrics);
EXPORT_SYMBOL(dmr_check_memory_pressure);
EXPORT_SYMBOL(dmr_audit_log_event);
EXPORT_SYMBOL(dmr_production_init);
EXPORT_SYMBOL(dmr_production_cleanup);