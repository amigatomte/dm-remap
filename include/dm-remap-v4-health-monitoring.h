/*
 * dm-remap v4.0 Health Monitoring and Predictive Analytics System
 * 
 * Advanced health monitoring system for proactive device management,
 * predictive failure analysis, and intelligent maintenance scheduling.
 * Integrates with existing validation and version control systems.
 * 
 * Author: dm-remap development team
 * Date: October 14, 2025
 */

#ifndef DM_REMAP_V4_HEALTH_MONITORING_H
#define DM_REMAP_V4_HEALTH_MONITORING_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include "dm-remap-v4-metadata.h"
#include "dm-remap-v4-validation.h"

/* Health monitoring constants */
#define DM_REMAP_V4_HEALTH_MAGIC         0x484C5448  /* "HLTH" */
#define DM_REMAP_V4_MAX_HEALTH_SAMPLES   256         /* Health history depth */
#define DM_REMAP_V4_MAX_PREDICTIVE_MODELS 16         /* Maximum prediction models */
#define DM_REMAP_V4_HEALTH_SCAN_INTERVAL 300         /* Default scan interval (5 min) */
#define DM_REMAP_V4_CRITICAL_THRESHOLD   20          /* Critical health threshold */
#define DM_REMAP_V4_WARNING_THRESHOLD    50          /* Warning health threshold */

/* Health monitoring operation types */
#define DM_REMAP_V4_HEALTH_OP_SCAN       0x01       /* Active health scan */
#define DM_REMAP_V4_HEALTH_OP_PREDICT    0x02       /* Predictive analysis */
#define DM_REMAP_V4_HEALTH_OP_ALERT      0x04       /* Health alert generation */
#define DM_REMAP_V4_HEALTH_OP_MAINTENANCE 0x08      /* Maintenance scheduling */
#define DM_REMAP_V4_HEALTH_OP_REMEDIATE  0x10       /* Automatic remediation */

/* Health status flags */
#define DM_REMAP_V4_HEALTH_EXCELLENT     100         /* Perfect health */
#define DM_REMAP_V4_HEALTH_GOOD          80          /* Good health */
#define DM_REMAP_V4_HEALTH_FAIR          60          /* Fair health */
#define DM_REMAP_V4_HEALTH_POOR          40          /* Poor health */
#define DM_REMAP_V4_HEALTH_CRITICAL      20          /* Critical health */
#define DM_REMAP_V4_HEALTH_FAILING       0           /* Device failing */

/* Health metric types */
#define DM_REMAP_V4_METRIC_IO_ERRORS     0x01       /* I/O error rate */
#define DM_REMAP_V4_METRIC_LATENCY       0x02       /* Average I/O latency */
#define DM_REMAP_V4_METRIC_THROUGHPUT    0x04       /* I/O throughput */
#define DM_REMAP_V4_METRIC_TEMPERATURE   0x08       /* Device temperature */
#define DM_REMAP_V4_METRIC_SMART_DATA    0x10       /* SMART attributes */
#define DM_REMAP_V4_METRIC_BAD_BLOCKS    0x20       /* Bad block count */
#define DM_REMAP_V4_METRIC_WEAR_LEVEL    0x40       /* Wear leveling data */
#define DM_REMAP_V4_METRIC_POWER_CYCLES  0x80       /* Power cycle count */

/* Predictive model types */
#define DM_REMAP_V4_MODEL_LINEAR         0x01       /* Linear regression */
#define DM_REMAP_V4_MODEL_EXPONENTIAL    0x02       /* Exponential decay */
#define DM_REMAP_V4_MODEL_THRESHOLD      0x04       /* Threshold-based */
#define DM_REMAP_V4_MODEL_PATTERN        0x08       /* Pattern recognition */
#define DM_REMAP_V4_MODEL_ENSEMBLE       0x10       /* Ensemble model */

/* Alert severity levels */
#define DM_REMAP_V4_ALERT_INFO           1          /* Informational */
#define DM_REMAP_V4_ALERT_WARNING        2          /* Warning */
#define DM_REMAP_V4_ALERT_ERROR          3          /* Error */
#define DM_REMAP_V4_ALERT_CRITICAL       4          /* Critical */
#define DM_REMAP_V4_ALERT_EMERGENCY      5          /* Emergency */

/*
 * Health metric sample structure
 * Stores a single health measurement with timestamp and context
 */
struct dm_remap_v4_health_sample {
    uint64_t timestamp;                              /* Sample timestamp */
    uint32_t metric_type;                            /* Type of metric */
    uint32_t value;                                  /* Metric value */
    uint32_t quality;                                /* Sample quality/confidence */
    uint32_t context_flags;                          /* Context information */
    char device_path[64];                            /* Device identifier */
    uint32_t sample_crc32;                           /* Sample integrity check */
} __packed;

/*
 * Health history structure
 * Maintains circular buffer of health samples for trend analysis
 */
struct dm_remap_v4_health_history {
    uint32_t magic;                                  /* Health magic number */
    uint32_t sample_count;                           /* Number of samples */
    uint32_t head_index;                             /* Current head position */
    uint32_t tail_index;                             /* Current tail position */
    uint64_t first_sample_time;                      /* Timestamp of first sample */
    uint64_t last_sample_time;                       /* Timestamp of last sample */
    
    /* Sample storage */
    struct dm_remap_v4_health_sample samples[DM_REMAP_V4_MAX_HEALTH_SAMPLES];
    
    /* Summary statistics */
    uint32_t min_value;                              /* Minimum observed value */
    uint32_t max_value;                              /* Maximum observed value */
    uint32_t avg_value;                              /* Average value */
    uint32_t trend_direction;                        /* Trend direction (0=stable, 1=up, 2=down) */
    
    uint32_t history_crc32;                          /* History integrity check */
} __packed;

/*
 * Predictive model structure
 * Stores parameters and state for predictive health analysis
 */
struct dm_remap_v4_predictive_model {
    uint32_t model_type;                             /* Type of predictive model */
    uint32_t model_id;                               /* Unique model identifier */
    uint64_t created_timestamp;                      /* Model creation time */
    uint64_t last_update_timestamp;                  /* Last model update */
    
    /* Model parameters */
    float coefficients[8];                           /* Model coefficients */
    float intercept;                                 /* Model intercept */
    float confidence_level;                          /* Model confidence */
    uint32_t training_samples;                       /* Samples used for training */
    
    /* Prediction results */
    uint32_t predicted_failure_time;                 /* Predicted failure time (days) */
    uint32_t prediction_confidence;                  /* Prediction confidence (0-100) */
    uint32_t recommended_action;                     /* Recommended action flags */
    
    /* Model validation */
    float accuracy_score;                            /* Model accuracy */
    float precision_score;                           /* Model precision */
    float recall_score;                              /* Model recall */
    
    char model_notes[128];                           /* Model description */
    uint32_t model_crc32;                            /* Model integrity check */
} __packed;

/*
 * Health alert structure
 * Represents a health-related alert or notification
 */
struct dm_remap_v4_health_alert {
    uint32_t alert_id;                               /* Unique alert identifier */
    uint64_t timestamp;                              /* Alert generation time */
    uint32_t severity;                               /* Alert severity level */
    uint32_t alert_type;                             /* Type of alert */
    uint32_t device_affected;                        /* Device index affected */
    
    /* Alert details */
    uint32_t metric_type;                            /* Related metric type */
    uint32_t current_value;                          /* Current metric value */
    uint32_t threshold_value;                        /* Threshold that was crossed */
    uint32_t trend_data;                             /* Trend analysis data */
    
    /* Alert status */
    uint32_t status;                                 /* Alert status (active, acknowledged, resolved) */
    uint64_t acknowledged_time;                      /* When alert was acknowledged */
    uint64_t resolved_time;                          /* When alert was resolved */
    
    char alert_message[256];                         /* Human-readable alert message */
    char recommended_actions[256];                   /* Recommended actions */
    
    uint32_t alert_crc32;                            /* Alert integrity check */
} __packed;

/*
 * Health monitoring configuration
 * Configures health monitoring behavior and thresholds
 */
struct dm_remap_v4_health_config {
    uint32_t magic;                                  /* Configuration magic */
    uint32_t monitoring_enabled;                     /* Enable/disable monitoring */
    uint32_t scan_interval;                          /* Health scan interval (seconds) */
    uint32_t prediction_enabled;                     /* Enable predictive analysis */
    
    /* Threshold configuration */
    uint32_t critical_threshold;                     /* Critical health threshold */
    uint32_t warning_threshold;                      /* Warning health threshold */
    uint32_t alert_threshold;                        /* Alert generation threshold */
    
    /* Metric collection settings */
    uint32_t enabled_metrics;                        /* Bitmask of enabled metrics */
    uint32_t sample_frequency;                       /* Sample collection frequency */
    uint32_t history_retention;                      /* History retention period (days) */
    
    /* Predictive model settings */
    uint32_t model_update_frequency;                 /* Model update frequency */
    uint32_t prediction_horizon;                     /* Prediction time horizon (days) */
    float min_confidence_threshold;                  /* Minimum prediction confidence */
    
    /* Alert configuration */
    uint32_t alert_enabled;                          /* Enable alert generation */
    uint32_t alert_methods;                          /* Alert delivery methods */
    uint32_t alert_escalation;                       /* Alert escalation settings */
    
    uint32_t config_crc32;                           /* Configuration integrity */
} __packed;

/*
 * Health monitoring context
 * Runtime context for health monitoring operations
 */
struct dm_remap_v4_health_context {
    struct dm_remap_v4_health_config config;        /* Configuration */
    struct dm_remap_v4_health_history *device_histories; /* Per-device histories */
    struct dm_remap_v4_predictive_model *models;    /* Predictive models */
    struct dm_remap_v4_health_alert *active_alerts; /* Active alerts */
    
    /* Runtime state */
    uint32_t num_devices;                            /* Number of monitored devices */
    uint32_t num_models;                             /* Number of active models */
    uint32_t num_alerts;                             /* Number of active alerts */
    uint64_t last_scan_time;                         /* Last health scan time */
    uint64_t next_scan_time;                         /* Next scheduled scan */
    
    /* Workqueue and timer management */
    struct workqueue_struct *health_wq;              /* Health monitoring workqueue */
    struct delayed_work health_scan_work;            /* Scheduled health scan work */
    struct timer_list prediction_timer;              /* Prediction update timer */
    
    /* Statistics */
    uint64_t total_scans;                            /* Total scans performed */
    uint64_t total_predictions;                      /* Total predictions made */
    uint64_t total_alerts;                           /* Total alerts generated */
    uint64_t successful_predictions;                 /* Successful predictions */
    
    /* Device references */
    struct dm_dev **monitored_devices;               /* Array of monitored devices */
    struct dm_remap_v4_metadata **device_metadata;  /* Associated metadata */
    
    spinlock_t context_lock;                         /* Context protection lock */
    atomic_t reference_count;                        /* Reference counting */
} __packed;

/*
 * Core health monitoring functions
 */

/* Initialize health monitoring system */
int dm_remap_v4_health_init(
    struct dm_remap_v4_health_context *context,
    struct dm_dev **devices,
    uint32_t num_devices,
    const struct dm_remap_v4_health_config *config
);

/* Shutdown health monitoring system */
void dm_remap_v4_health_shutdown(
    struct dm_remap_v4_health_context *context
);

/* Perform health scan on all monitored devices */
int dm_remap_v4_health_scan_devices(
    struct dm_remap_v4_health_context *context
);

/* Scan health of a specific device */
int dm_remap_v4_health_scan_device(
    struct dm_remap_v4_health_context *context,
    uint32_t device_index,
    struct dm_remap_v4_health_sample *sample
);

/* Add health sample to history */
int dm_remap_v4_health_add_sample(
    struct dm_remap_v4_health_history *history,
    const struct dm_remap_v4_health_sample *sample
);

/* Get current health score for device */
uint32_t dm_remap_v4_health_get_score(
    const struct dm_remap_v4_health_history *history,
    uint32_t metric_type
);

/*
 * Predictive analytics functions
 */

/* Create predictive model for device */
int dm_remap_v4_health_create_model(
    struct dm_remap_v4_health_context *context,
    uint32_t device_index,
    uint32_t model_type,
    struct dm_remap_v4_predictive_model *model
);

/* Update predictive model with new data */
int dm_remap_v4_health_update_model(
    struct dm_remap_v4_predictive_model *model,
    const struct dm_remap_v4_health_history *history
);

/* Generate prediction using model */
int dm_remap_v4_health_predict_failure(
    const struct dm_remap_v4_predictive_model *model,
    const struct dm_remap_v4_health_history *history,
    uint32_t *days_to_failure,
    uint32_t *confidence
);

/* Validate model accuracy */
float dm_remap_v4_health_validate_model(
    const struct dm_remap_v4_predictive_model *model,
    const struct dm_remap_v4_health_history *history
);

/*
 * Alert management functions
 */

/* Generate health alert */
int dm_remap_v4_health_generate_alert(
    struct dm_remap_v4_health_context *context,
    uint32_t device_index,
    uint32_t severity,
    uint32_t alert_type,
    const char *message
);

/* Process active alerts */
int dm_remap_v4_health_process_alerts(
    struct dm_remap_v4_health_context *context
);

/* Acknowledge alert */
int dm_remap_v4_health_acknowledge_alert(
    struct dm_remap_v4_health_context *context,
    uint32_t alert_id
);

/* Resolve alert */
int dm_remap_v4_health_resolve_alert(
    struct dm_remap_v4_health_context *context,
    uint32_t alert_id
);

/*
 * Health history management functions
 */

/* Initialize health history */
int dm_remap_v4_health_init_history(
    struct dm_remap_v4_health_history *history
);

/* Analyze health trends */
int dm_remap_v4_health_analyze_trends(
    const struct dm_remap_v4_health_history *history,
    uint32_t *trend_direction,
    float *trend_strength
);

/* Get health statistics */
int dm_remap_v4_health_get_statistics(
    const struct dm_remap_v4_health_history *history,
    uint32_t *min_value,
    uint32_t *max_value,
    uint32_t *avg_value,
    float *std_deviation
);

/* Compact health history */
int dm_remap_v4_health_compact_history(
    struct dm_remap_v4_health_history *history,
    uint32_t retention_days
);

/*
 * Configuration and maintenance functions
 */

/* Initialize health configuration */
void dm_remap_v4_health_init_config(
    struct dm_remap_v4_health_config *config
);

/* Update health configuration */
int dm_remap_v4_health_update_config(
    struct dm_remap_v4_health_context *context,
    const struct dm_remap_v4_health_config *new_config
);

/* Schedule maintenance operation */
int dm_remap_v4_health_schedule_maintenance(
    struct dm_remap_v4_health_context *context,
    uint32_t device_index,
    uint32_t maintenance_type,
    uint64_t scheduled_time
);

/* Perform automatic remediation */
int dm_remap_v4_health_auto_remediate(
    struct dm_remap_v4_health_context *context,
    uint32_t device_index,
    uint32_t issue_type
);

/*
 * Workqueue callback functions
 */

/* Health scan work function */
void dm_remap_v4_health_scan_work_fn(struct work_struct *work);

/* Prediction update timer callback */
void dm_remap_v4_health_prediction_timer_fn(struct timer_list *timer);

/*
 * Utility functions
 */

/* Convert health score to string */
const char *dm_remap_v4_health_score_to_string(uint32_t score);

/* Convert alert severity to string */
const char *dm_remap_v4_health_alert_severity_to_string(uint32_t severity);

/* Convert metric type to string */
const char *dm_remap_v4_health_metric_type_to_string(uint32_t metric_type);

/* Calculate health sample checksum */
uint32_t dm_remap_v4_health_calculate_sample_crc(
    const struct dm_remap_v4_health_sample *sample
);

/* Validate health history integrity */
int dm_remap_v4_health_validate_history_integrity(
    const struct dm_remap_v4_health_history *history
);

/*
 * Inline utility functions
 */

/* Check if health score is critical */
static inline bool dm_remap_v4_health_is_critical(uint32_t score)
{
    return score <= DM_REMAP_V4_CRITICAL_THRESHOLD;
}

/* Check if health score needs warning */
static inline bool dm_remap_v4_health_needs_warning(uint32_t score)
{
    return score <= DM_REMAP_V4_WARNING_THRESHOLD;
}

/* Check if alert is active */
static inline bool dm_remap_v4_health_alert_is_active(
    const struct dm_remap_v4_health_alert *alert)
{
    return alert->status == 1; /* Active status */
}

/* Get time since last health scan */
static inline uint64_t dm_remap_v4_health_time_since_scan(
    const struct dm_remap_v4_health_context *context)
{
    return ktime_get_real_seconds() - context->last_scan_time;
}

/* Check if prediction is confident */
static inline bool dm_remap_v4_health_prediction_confident(
    const struct dm_remap_v4_predictive_model *model)
{
    return model->prediction_confidence >= 70; /* 70% confidence threshold */
}

#endif /* DM_REMAP_V4_HEALTH_MONITORING_H */