/*
 * dm-remap-health-predict.c - Predictive Analysis for Health Scanning
 * 
 * Predictive Analysis Engine for dm-remap Background Health Scanning
 * Implements failure prediction algorithms and proactive warning systems
 * 
 * This module provides advanced predictive analysis capabilities that use
 * historical health data and trends to predict potential sector failures
 * before they occur, enabling proactive data protection measures.
 * 
 * Author: Christian (with AI assistance)
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/sort.h>

#include "dm-remap-health-core.h"

/* Module metadata */
MODULE_AUTHOR("Christian (with AI assistance)");
MODULE_DESCRIPTION("Predictive Analysis for dm-remap Health Scanning");
MODULE_LICENSE("GPL");

/* Prediction algorithm constants */
#define DMR_PREDICT_MIN_SAMPLES          10    /* Minimum samples for prediction */
#define DMR_PREDICT_CONFIDENCE_HIGH      80    /* High confidence threshold */
#define DMR_PREDICT_CONFIDENCE_MEDIUM    50    /* Medium confidence threshold */
#define DMR_PREDICT_SEVERITY_CRITICAL    9     /* Critical severity level */
#define DMR_PREDICT_SEVERITY_HIGH        7     /* High severity level */
#define DMR_PREDICT_SEVERITY_MEDIUM      4     /* Medium severity level */

/* Time constants for failure prediction */
#define DMR_PREDICT_HOURS_TO_JIFFIES(h)  ((u64)(h) * 3600ULL * HZ)
#define DMR_PREDICT_DAYS_TO_JIFFIES(d)   ((u64)(d) * 24ULL * 3600ULL * HZ)

/**
 * struct dmr_trend_analysis - Trend analysis data
 * 
 * Stores trend analysis information for predictive algorithms
 */
struct dmr_trend_analysis {
    u32 error_rate_trend;        /* Error rate change per unit time */
    u32 health_score_slope;      /* Health score change slope */
    u32 access_pattern_score;    /* Access pattern analysis score */
    u64 time_window;             /* Analysis time window (jiffies) */
    u8 reliability_score;        /* Trend reliability (0-100) */
};

/**
 * dmr_analyze_sector_trend - Analyze health trends for a sector
 * @health: Sector health data
 * @trend: Output trend analysis
 * 
 * Analyzes historical health data to identify trends that may indicate
 * impending failure.
 * 
 * Returns: 0 on success, negative error code on failure
 */
static int dmr_analyze_sector_trend(struct dmr_sector_health *health,
                                   struct dmr_trend_analysis *trend)
{
    u64 current_time = jiffies;
    u64 time_since_last_scan, time_since_last_access;
    u32 total_errors;

    if (!health || !trend) {
        return -EINVAL;
    }

    memset(trend, 0, sizeof(*trend));

    /* Calculate time windows */
    time_since_last_scan = health->last_scan_time ? 
                          current_time - health->last_scan_time : 0;
    time_since_last_access = health->last_access_time ? 
                            current_time - health->last_access_time : 0;

    total_errors = health->read_errors + health->write_errors;

    /* Calculate error rate trend */
    if (health->access_count > 0) {
        trend->error_rate_trend = (total_errors * 1000) / health->access_count;
        
        /* Adjust for recent activity */
        if (time_since_last_access < DMR_PREDICT_HOURS_TO_JIFFIES(1)) {
            trend->error_rate_trend *= 2;  /* Recent errors are more significant */
        }
    }

    /* Analyze health score trend */
    if (health->health_score < DMR_HEALTH_SCORE_WARNING_THRESHOLD) {
        u32 score_deficit = DMR_HEALTH_SCORE_PERFECT - health->health_score;
        trend->health_score_slope = score_deficit;
        
        /* Factor in the rate of decline */
        if (health->scan_count > 5) {
            trend->health_score_slope = (trend->health_score_slope * 100) / 
                                       health->scan_count;
        }
    }

    /* Access pattern analysis */
    if (health->access_count > 100) {
        /* Frequently accessed sectors with errors are higher risk */
        trend->access_pattern_score = min_t(u32, 100, 
                                           (health->access_count / 10) + 
                                           (total_errors * 5));
    } else if (health->access_count > 0 && total_errors > 0) {
        /* Low access with errors still concerning */
        trend->access_pattern_score = total_errors * 10;
    }

    /* Set analysis time window */
    trend->time_window = max(time_since_last_scan, time_since_last_access);

    /* Calculate reliability score based on data quality */
    trend->reliability_score = 50; /* Base reliability */
    
    if (health->scan_count >= DMR_PREDICT_MIN_SAMPLES) {
        trend->reliability_score += 30;
    }
    
    if (health->access_count >= 50) {
        trend->reliability_score += 20;
    }
    
    if (trend->time_window < DMR_PREDICT_DAYS_TO_JIFFIES(7)) {
        trend->reliability_score = max_t(u8, 20, trend->reliability_score - 30);
    }

    return 0;
}

/**
 * dmr_calculate_failure_probability - Calculate failure probability
 * @health: Sector health data
 * @trend: Trend analysis data
 * 
 * Calculates the probability of sector failure within a given timeframe
 * based on current health status and trends.
 * 
 * Returns: Failure probability (0-100%)
 */
static u32 dmr_calculate_failure_probability(struct dmr_sector_health *health,
                                           struct dmr_trend_analysis *trend)
{
    u32 probability = 0;
    u32 base_risk, trend_risk, pattern_risk;

    if (!health || !trend) {
        return 0;
    }

    /* Base risk from current health score */
    if (health->health_score >= DMR_HEALTH_SCORE_WARNING_THRESHOLD) {
        base_risk = 5;  /* Low base risk for healthy sectors */
    } else if (health->health_score >= DMR_HEALTH_SCORE_DANGER_THRESHOLD) {
        base_risk = 20; /* Medium base risk */
    } else {
        base_risk = 50; /* High base risk for low-health sectors */
    }

    /* Risk from error trends */
    trend_risk = min_t(u32, 40, trend->error_rate_trend / 10);
    
    /* Risk from health score decline */
    if (trend->health_score_slope > 0) {
        trend_risk += min_t(u32, 30, trend->health_score_slope / 20);
    }

    /* Risk from access patterns */
    pattern_risk = min_t(u32, 30, trend->access_pattern_score / 5);

    /* Combine risk factors */
    probability = base_risk + trend_risk + pattern_risk;

    /* Apply reliability factor */
    probability = (probability * trend->reliability_score) / 100;

    /* Cap at 100% */
    probability = min_t(u32, 100, probability);

    return probability;
}

/**
 * dmr_estimate_failure_time - Estimate time until failure
 * @health: Sector health data
 * @trend: Trend analysis data
 * @probability: Failure probability
 * 
 * Estimates the time until potential failure based on current trends.
 * 
 * Returns: Estimated time until failure (jiffies from now)
 */
static u64 dmr_estimate_failure_time(struct dmr_sector_health *health,
                                    struct dmr_trend_analysis *trend,
                                    u32 probability)
{
    u64 estimated_time;
    u32 degradation_rate;

    if (!health || !trend || probability == 0) {
        return 0; /* Cannot estimate */
    }

    /* Calculate degradation rate from health score and trends */
    degradation_rate = trend->health_score_slope + trend->error_rate_trend;
    
    if (degradation_rate == 0) {
        /* No degradation trend - use probability-based estimate */
        if (probability >= 80) {
            estimated_time = DMR_PREDICT_DAYS_TO_JIFFIES(7);    /* 1 week */
        } else if (probability >= 50) {
            estimated_time = DMR_PREDICT_DAYS_TO_JIFFIES(30);   /* 1 month */
        } else if (probability >= 20) {
            estimated_time = DMR_PREDICT_DAYS_TO_JIFFIES(90);   /* 3 months */
        } else {
            estimated_time = DMR_PREDICT_DAYS_TO_JIFFIES(365);  /* 1 year */
        }
    } else {
        /* Use degradation rate to estimate time */
        u32 health_remaining = health->health_score;
        u64 time_per_unit_degradation;
        
        /* Estimate time for health to degrade to critical level */
        if (health->scan_count > 0) {
            time_per_unit_degradation = trend->time_window / 
                                       max_t(u32, 1, degradation_rate);
            estimated_time = (health_remaining * time_per_unit_degradation) / 
                            DMR_HEALTH_SCORE_DANGER_THRESHOLD;
        } else {
            estimated_time = DMR_PREDICT_DAYS_TO_JIFFIES(30);
        }

        /* Apply probability factor */
        estimated_time = estimated_time * (100 - probability) / 100;
    }

    /* Ensure reasonable bounds */
    estimated_time = max_t(u64, DMR_PREDICT_HOURS_TO_JIFFIES(1),   /* Min 1 hour */
                          min_t(u64, DMR_PREDICT_DAYS_TO_JIFFIES(365), estimated_time));

    return jiffies + estimated_time;
}

/**
 * dmr_determine_failure_reason - Determine the most likely failure reason
 * @health: Sector health data
 * @trend: Trend analysis data
 * @reason: Output buffer for failure reason
 * @reason_size: Size of reason buffer
 * 
 * Analyzes health data to determine the most likely cause of potential failure.
 */
static void dmr_determine_failure_reason(struct dmr_sector_health *health,
                                       struct dmr_trend_analysis *trend,
                                       char *reason, size_t reason_size)
{
    if (!health || !trend || !reason || reason_size == 0) {
        return;
    }

    /* Analyze primary failure indicators */
    if (health->read_errors > health->write_errors * 2) {
        snprintf(reason, reason_size, "Excessive read errors detected");
    } else if (health->write_errors > health->read_errors * 2) {
        snprintf(reason, reason_size, "Excessive write errors detected");
    } else if (health->read_errors > 0 && health->write_errors > 0) {
        snprintf(reason, reason_size, "Mixed I/O errors indicating media degradation");
    } else if (trend->health_score_slope > 50) {
        snprintf(reason, reason_size, "Rapid health score decline detected");
    } else if (trend->error_rate_trend > 100) {
        snprintf(reason, reason_size, "Increasing error rate trend");
    } else if (health->health_score < DMR_HEALTH_SCORE_DANGER_THRESHOLD) {
        snprintf(reason, reason_size, "Health score below critical threshold");
    } else if (trend->access_pattern_score > 50) {
        snprintf(reason, reason_size, "Problematic access patterns detected");
    } else {
        snprintf(reason, reason_size, "General health degradation");
    }
}

/**
 * dmr_predict_sector_failure - Predict potential sector failure
 * @scanner: Health scanner instance
 * @sector: Sector to analyze
 * @prediction: Output prediction data
 * 
 * Performs comprehensive failure prediction analysis for a specific sector.
 * Uses multiple algorithms and data sources to generate accurate predictions.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_predict_sector_failure(struct dmr_health_scanner *scanner,
                              sector_t sector, struct dmr_failure_prediction *prediction)
{
    struct dmr_sector_health *health;
    struct dmr_trend_analysis trend;
    u32 probability;
    u64 failure_time;
    u8 confidence, severity;
    int ret;

    if (!scanner || !prediction) {
        return -EINVAL;
    }

    memset(prediction, 0, sizeof(*prediction));

    /* Get health data for sector */
    health = dmr_get_sector_health(scanner->health_map, sector);
    if (!health) {
        /* No health data available - cannot predict */
        return -ENODATA;
    }

    /* Perform trend analysis */
    ret = dmr_analyze_sector_trend(health, &trend);
    if (ret) {
        return ret;
    }

    /* Calculate failure probability */
    probability = dmr_calculate_failure_probability(health, &trend);
    
    /* Determine confidence level */
    confidence = trend.reliability_score;
    
    /* Adjust confidence based on data quality */
    if (health->scan_count < DMR_PREDICT_MIN_SAMPLES) {
        confidence = max_t(u8, 10, confidence - 30);
    }
    
    if (health->access_count < 10) {
        confidence = max_t(u8, 10, confidence - 20);
    }

    /* Determine severity level */
    if (probability >= 80) {
        severity = DMR_PREDICT_SEVERITY_CRITICAL;
    } else if (probability >= 50) {
        severity = DMR_PREDICT_SEVERITY_HIGH;
    } else if (probability >= 20) {
        severity = DMR_PREDICT_SEVERITY_MEDIUM;
    } else {
        severity = 1; /* Low severity */
    }

    /* Estimate failure time */
    failure_time = dmr_estimate_failure_time(health, &trend, probability);

    /* Determine failure reason */
    dmr_determine_failure_reason(health, &trend, prediction->reason, 
                                sizeof(prediction->reason));

    /* Fill prediction structure */
    prediction->failure_probability = probability;
    prediction->estimated_failure_time = failure_time;
    prediction->confidence_level = confidence;
    prediction->severity = severity;

    /* Update scanner statistics */
    atomic64_inc(&scanner->stats.predictions_made);

    /* Log high-risk predictions */
    if (probability >= 50 && confidence >= DMR_PREDICT_CONFIDENCE_MEDIUM) {
        printk(KERN_WARNING "dm-remap-health-predict: HIGH RISK sector %llu: "
               "probability=%u%%, confidence=%u%%, reason=%s\n",
               (unsigned long long)sector, probability, confidence, 
               prediction->reason);
        
        /* Update risk statistics */
        if (severity >= DMR_PREDICT_SEVERITY_HIGH) {
            atomic_inc(&scanner->stats.high_risk_sectors);
        }
        atomic64_inc(&scanner->stats.warnings_issued);
    }

    return 0;
}

/**
 * dmr_health_risk_assessment - Perform comprehensive risk assessment
 * @scanner: Health scanner instance
 * @assessment_results: Output buffer for results
 * @max_results: Maximum number of results to return
 * 
 * Performs risk assessment across all tracked sectors and returns
 * the highest-risk sectors for attention.
 * 
 * Returns: Number of high-risk sectors found, negative on error
 */
int dmr_health_risk_assessment(struct dmr_health_scanner *scanner,
                              struct dmr_failure_prediction *assessment_results,
                              int max_results)
{
    /* This would be implemented with callback iteration over health map */
    /* For now, return placeholder implementation */
    
    if (!scanner || !assessment_results || max_results <= 0) {
        return -EINVAL;
    }

    /* TODO: Implement full risk assessment iteration */
    /* This would use dmr_health_map_iterate with a callback that:
     * 1. Generates predictions for each sector
     * 2. Sorts by risk level
     * 3. Returns top N results
     */

    return 0; /* Placeholder - no high-risk sectors found */
}

/**
 * dmr_health_trend_monitor - Monitor health trends across device
 * @scanner: Health scanner instance
 * 
 * Monitors overall device health trends and generates system-wide
 * health warnings if concerning patterns are detected.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int dmr_health_trend_monitor(struct dmr_health_scanner *scanner)
{
    u32 total_warnings, total_high_risk;
    u64 total_scans;
    bool system_warning = false;

    if (!scanner) {
        return -EINVAL;
    }

    /* Get current statistics */
    total_warnings = atomic_read(&scanner->stats.active_warnings);
    total_high_risk = atomic_read(&scanner->stats.high_risk_sectors);
    total_scans = atomic64_read(&scanner->stats.total_scans);

    /* Check for system-wide concerning trends */
    if (total_high_risk > 10) {
        printk(KERN_WARNING "dm-remap-health-predict: SYSTEM WARNING: "
               "%u high-risk sectors detected\n", total_high_risk);
        system_warning = true;
    }

    if (total_warnings > 50) {
        printk(KERN_WARNING "dm-remap-health-predict: SYSTEM WARNING: "
               "%u active health warnings\n", total_warnings);
        system_warning = true;
    }

    /* Check scan coverage and effectiveness */
    if (total_scans > 1000 && scanner->stats.scan_coverage_percent < 25) {
        printk(KERN_INFO "dm-remap-health-predict: NOTICE: "
               "Low scan coverage (%u%%) after %llu scans\n",
               scanner->stats.scan_coverage_percent, total_scans);
    }

    return system_warning ? 1 : 0;
}

/**
 * dmr_health_prediction_cleanup - Clean up prediction resources
 * @scanner: Health scanner instance
 * 
 * Cleans up any resources used by the prediction system.
 */
void dmr_health_prediction_cleanup(struct dmr_health_scanner *scanner)
{
    if (!scanner) {
        return;
    }

    /* Reset prediction-related statistics */
    atomic64_set(&scanner->stats.predictions_made, 0);
    atomic_set(&scanner->stats.high_risk_sectors, 0);

    printk(KERN_INFO "dm-remap-health-predict: Prediction system cleaned up\n");
}