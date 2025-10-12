# dm-remap v4.0 Background Scanning Architecture Design

**Document Version**: 1.0  
**Date**: October 12, 2025  
**Phase**: 4.0 Week 1-2 Deliverable  
**Purpose**: Non-intrusive background health scanning with intelligent scheduling

---

## 🎯 Overview

The v4.0 background scanning system provides **continuous device health monitoring** using kernel work queues, intelligent I/O scheduling, and configurable scan policies to detect potential issues before they cause data loss, while maintaining <2% performance impact.

## 🏗️ Architecture Components

### **Core System Architecture**
```
┌─────────────────────────────────────────────────────────┐
│                   Background Scanner                    │
├─────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │   Work      │  │   I/O       │  │   Health    │     │
│  │   Queue     │  │ Scheduler   │  │ Analyzer    │     │
│  │ Manager     │  │             │  │             │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
├─────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │   Scan      │  │ Performance │  │    Sysfs    │     │
│  │ Progress    │  │  Monitor    │  │ Interface   │     │
│  │ Tracker     │  │             │  │             │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### **Data Structures**
```c
/**
 * Background scanner configuration and state
 */
struct dm_remap_background_scanner {
    // Configuration
    uint32_t scan_interval_seconds;     // Full scan interval (default: 86400 = 24h)
    uint32_t scan_chunk_sectors;        // Sectors per scan iteration (default: 1024)
    uint32_t max_io_latency_ms;        // Max allowed I/O latency during scan
    uint32_t scan_priority;             // Scan priority (0=lowest, 10=highest)
    bool enabled;                       // Scanner enabled/disabled
    
    // Runtime state
    struct workqueue_struct *scan_wq;   // Dedicated work queue
    struct delayed_work scan_work;      // Delayed work structure
    struct dm_remap_device *target;     // Current scan target
    
    // Progress tracking
    uint64_t current_sector;            // Current scan position
    uint64_t total_sectors;             // Total device sectors
    uint64_t last_scan_start;           // Last full scan start time
    uint64_t last_scan_complete;        // Last full scan completion time
    uint32_t scan_progress_percent;     // Current scan progress (0-100)
    
    // Performance monitoring
    struct {
        atomic64_t sectors_scanned;
        atomic64_t scan_time_ms;
        atomic64_t io_errors_detected;
        atomic64_t predictive_remaps;
        atomic64_t scan_interruptions;
    } stats;
    
    // Health analysis
    struct {
        uint32_t overall_health_score;  // 0-100 health score
        uint32_t error_sectors_found;   // Number of problematic sectors
        uint32_t slow_sectors_found;    // Number of slow-responding sectors
        uint64_t last_health_update;    // Last health score calculation
    } health;
    
    // Synchronization
    struct mutex scan_mutex;            // Protects scanner state
    atomic_t scan_active;               // Atomic scan activity flag
};
```

## ⚙️ Work Queue Management

### **Dedicated Work Queue Setup**
```c
/**
 * dm_remap_scanner_init() - Initialize background scanner system
 */
static int dm_remap_scanner_init(struct dm_remap_background_scanner *scanner,
                                struct dm_remap_device *device)
{
    int ret;
    
    // Initialize scanner structure
    memset(scanner, 0, sizeof(*scanner));
    scanner->target = device;
    
    // Set default configuration
    scanner->scan_interval_seconds = 86400;  // 24 hours
    scanner->scan_chunk_sectors = 1024;      // 512KB chunks
    scanner->max_io_latency_ms = 100;        // 100ms max latency
    scanner->scan_priority = 3;              // Medium-low priority
    scanner->enabled = true;
    
    // Initialize synchronization
    mutex_init(&scanner->scan_mutex);
    atomic_set(&scanner->scan_active, 0);
    
    // Create dedicated work queue
    scanner->scan_wq = alloc_workqueue("dm_remap_scanner", 
                                      WQ_MEM_RECLAIM | WQ_UNBOUND,
                                      1); // Single worker thread
    if (!scanner->scan_wq) {
        DMR_DEBUG(0, "Failed to create scanner work queue");
        return -ENOMEM;
    }
    
    // Initialize delayed work
    INIT_DELAYED_WORK(&scanner->scan_work, dm_remap_scan_worker);
    
    // Get device geometry
    scanner->total_sectors = get_capacity(device->spare_dev->bd_disk);
    scanner->current_sector = 0;
    
    DMR_DEBUG(1, "Background scanner initialized: %llu sectors to scan",
              scanner->total_sectors);
    
    return 0;
}

/**
 * dm_remap_scanner_start() - Start background scanning
 */
static int dm_remap_scanner_start(struct dm_remap_background_scanner *scanner)
{
    if (!scanner->enabled) {
        return -ENODEV;
    }
    
    // Schedule first scan with initial delay (30 seconds)
    queue_delayed_work(scanner->scan_wq, &scanner->scan_work, 
                      msecs_to_jiffies(30000));
    
    DMR_DEBUG(1, "Background scanner started with %u second interval",
              scanner->scan_interval_seconds);
    
    return 0;
}
```

### **Scan Worker Implementation**
```c
/**
 * dm_remap_scan_worker() - Main scanning work function
 */
static void dm_remap_scan_worker(struct work_struct *work)
{
    struct delayed_work *dwork = to_delayed_work(work);
    struct dm_remap_background_scanner *scanner = 
        container_of(dwork, struct dm_remap_background_scanner, scan_work);
    
    ktime_t scan_start_time = ktime_get();
    uint64_t sectors_to_scan;
    int ret;
    
    // Check if scan should proceed
    if (!scanner->enabled || !scanner->target) {
        return;
    }
    
    mutex_lock(&scanner->scan_mutex);
    
    // Set scan active flag
    atomic_set(&scanner->scan_active, 1);
    
    // Determine scan chunk size based on I/O load
    sectors_to_scan = calculate_adaptive_chunk_size(scanner);
    
    // Perform actual scanning
    ret = perform_sector_scan(scanner, scanner->current_sector, sectors_to_scan);
    
    if (ret == 0) {
        // Update progress
        scanner->current_sector += sectors_to_scan;
        
        // Check if full scan completed
        if (scanner->current_sector >= scanner->total_sectors) {
            // Full scan cycle completed
            scanner->current_sector = 0;
            scanner->last_scan_complete = ktime_get_real_seconds();
            update_health_score(scanner);
            
            DMR_DEBUG(1, "Full scan cycle completed: %llu sectors scanned",
                      scanner->total_sectors);
        }
        
        // Update progress percentage
        scanner->scan_progress_percent = 
            (scanner->current_sector * 100) / scanner->total_sectors;
    }
    
    // Update statistics
    ktime_t scan_end_time = ktime_get();
    s64 scan_duration_ms = ktime_to_ms(ktime_sub(scan_end_time, scan_start_time));
    atomic64_add(scan_duration_ms, &scanner->stats.scan_time_ms);
    atomic64_add(sectors_to_scan, &scanner->stats.sectors_scanned);
    
    // Clear scan active flag
    atomic_set(&scanner->scan_active, 0);
    
    mutex_unlock(&scanner->scan_mutex);
    
    // Schedule next scan iteration
    schedule_next_scan(scanner);
}
```

## 📊 Intelligent I/O Scheduling

### **Adaptive Chunk Size Calculation**
```c
/**
 * calculate_adaptive_chunk_size() - Adapt scan chunk size based on system load
 */
static uint64_t calculate_adaptive_chunk_size(struct dm_remap_background_scanner *scanner)
{
    uint64_t base_chunk = scanner->scan_chunk_sectors;
    uint64_t adaptive_chunk;
    
    // Monitor current I/O load
    struct dm_remap_device *device = scanner->target;
    uint32_t current_io_load = get_device_io_load(device);
    
    // Adapt chunk size based on I/O load
    if (current_io_load < 20) {
        // Low load: increase chunk size for efficiency
        adaptive_chunk = base_chunk * 2;
    } else if (current_io_load < 50) {
        // Medium load: use default chunk size
        adaptive_chunk = base_chunk;
    } else if (current_io_load < 80) {
        // High load: reduce chunk size
        adaptive_chunk = base_chunk / 2;
    } else {
        // Very high load: minimal chunk size
        adaptive_chunk = base_chunk / 4;
    }
    
    // Ensure minimum chunk size
    adaptive_chunk = max(adaptive_chunk, 64ULL); // Minimum 32KB
    
    DMR_DEBUG(3, "Adaptive chunk size: %llu sectors (load=%u%%)",
              adaptive_chunk, current_io_load);
    
    return adaptive_chunk;
}

/**
 * get_device_io_load() - Estimate current I/O load percentage
 */
static uint32_t get_device_io_load(struct dm_remap_device *device)
{
    struct request_queue *q = bdev_get_queue(device->spare_dev);
    
    if (!q) {
        return 0;
    }
    
    // Simple load estimation based on queue depth
    uint32_t queue_depth = q->nr_requests;
    uint32_t active_requests = queue_depth - q->rq.count[READ] - q->rq.count[WRITE];
    
    // Calculate load percentage
    uint32_t load_percent = (active_requests * 100) / max(queue_depth, 1U);
    
    return min(load_percent, 100U);
}
```

### **Scan Scheduling Strategy**
```c
/**
 * schedule_next_scan() - Schedule next scan iteration with intelligent timing
 */
static void schedule_next_scan(struct dm_remap_background_scanner *scanner)
{
    unsigned long delay_ms;
    uint32_t io_load = get_device_io_load(scanner->target);
    
    // Base delay between scan iterations
    unsigned long base_delay_ms = 5000; // 5 seconds
    
    // Adjust delay based on I/O load
    if (io_load < 20) {
        delay_ms = base_delay_ms / 2;        // More frequent scanning
    } else if (io_load < 50) {
        delay_ms = base_delay_ms;            // Normal frequency
    } else if (io_load < 80) {
        delay_ms = base_delay_ms * 2;        // Less frequent scanning
    } else {
        delay_ms = base_delay_ms * 4;        // Much less frequent
    }
    
    // Add random jitter to avoid thundering herd
    delay_ms += prandom_u32() % (delay_ms / 4);
    
    // Schedule next iteration
    queue_delayed_work(scanner->scan_wq, &scanner->scan_work,
                      msecs_to_jiffies(delay_ms));
    
    DMR_DEBUG(3, "Next scan scheduled in %lu ms (load=%u%%)", delay_ms, io_load);
}
```

## 🔍 Sector Health Assessment

### **Sector Scanning Implementation**
```c
/**
 * perform_sector_scan() - Scan sectors for health assessment
 */
static int perform_sector_scan(struct dm_remap_background_scanner *scanner,
                              uint64_t start_sector, uint64_t sector_count)
{
    struct dm_remap_device *device = scanner->target;
    struct bio *bio;
    struct page *page;
    ktime_t io_start_time, io_end_time;
    s64 latency_ms;
    int ret = 0;
    uint64_t sector;
    
    // Allocate page for read buffer
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        return -ENOMEM;
    }
    
    DMR_DEBUG(3, "Scanning sectors %llu-%llu (%llu sectors)",
              start_sector, start_sector + sector_count - 1, sector_count);
    
    // Scan sectors in smaller chunks to minimize latency impact
    for (sector = start_sector; sector < start_sector + sector_count; sector += 8) {
        uint64_t chunk_size = min(8ULL, start_sector + sector_count - sector);
        
        // Check if we should yield to high-priority I/O
        if (should_yield_to_io(scanner)) {
            DMR_DEBUG(3, "Yielding to high-priority I/O at sector %llu", sector);
            atomic64_inc(&scanner->stats.scan_interruptions);
            break;
        }
        
        // Create bio for reading
        bio = bio_alloc(GFP_KERNEL, 1);
        if (!bio) {
            ret = -ENOMEM;
            break;
        }
        
        bio_set_dev(bio, device->spare_dev);
        bio->bi_iter.bi_sector = sector;
        bio->bi_opf = REQ_OP_READ;
        bio_add_page(bio, page, chunk_size * 512, 0);
        
        // Measure I/O latency
        io_start_time = ktime_get();
        ret = submit_bio_wait(bio);
        io_end_time = ktime_get();
        
        latency_ms = ktime_to_ms(ktime_sub(io_end_time, io_start_time));
        
        // Analyze results
        if (ret != 0) {
            // I/O error detected
            handle_scan_io_error(scanner, sector, chunk_size, ret);
            atomic64_inc(&scanner->stats.io_errors_detected);
        } else if (latency_ms > scanner->max_io_latency_ms) {
            // Slow sector detected
            handle_slow_sector(scanner, sector, chunk_size, latency_ms);
        }
        
        bio_put(bio);
        
        // Throttle scanning if needed
        if (latency_ms > scanner->max_io_latency_ms / 2) {
            msleep(1); // Brief pause to reduce I/O pressure
        }
    }
    
    __free_page(page);
    return ret;
}

/**
 * should_yield_to_io() - Check if scanning should yield to normal I/O
 */
static bool should_yield_to_io(struct dm_remap_background_scanner *scanner)
{
    uint32_t io_load = get_device_io_load(scanner->target);
    
    // Yield if I/O load is high
    return (io_load > 75);
}
```

### **Health Analysis and Scoring**
```c
/**
 * handle_scan_io_error() - Process I/O error during scanning
 */
static void handle_scan_io_error(struct dm_remap_background_scanner *scanner,
                                uint64_t sector, uint64_t count, int error)
{
    DMR_DEBUG(1, "I/O error during scan: sector %llu, count %llu, error %d",
              sector, count, error);
    
    scanner->health.error_sectors_found += count;
    
    // Consider predictive remapping for problematic sectors
    if (should_predictive_remap(scanner, sector, error)) {
        schedule_predictive_remap(scanner, sector, count);
        atomic64_inc(&scanner->stats.predictive_remaps);
    }
}

/**
 * handle_slow_sector() - Process slow-responding sector
 */
static void handle_slow_sector(struct dm_remap_background_scanner *scanner,
                              uint64_t sector, uint64_t count, s64 latency_ms)
{
    DMR_DEBUG(2, "Slow sector detected: sector %llu, latency %lld ms",
              sector, latency_ms);
    
    scanner->health.slow_sectors_found += count;
    
    // Track slow sectors for health scoring
    if (latency_ms > scanner->max_io_latency_ms * 2) {
        // Very slow sector - consider for predictive remapping
        if (should_predictive_remap(scanner, sector, -ETIMEDOUT)) {
            schedule_predictive_remap(scanner, sector, count);
        }
    }
}

/**
 * update_health_score() - Calculate overall device health score
 */
static void update_health_score(struct dm_remap_background_scanner *scanner)
{
    uint32_t health_score = 100; // Start with perfect health
    uint64_t total_scanned = atomic64_read(&scanner->stats.sectors_scanned);
    
    if (total_scanned == 0) {
        return;
    }
    
    // Reduce score based on error rate
    uint32_t error_rate = (scanner->health.error_sectors_found * 10000) / total_scanned;
    health_score -= min(error_rate / 10, 50U); // Max 50 point reduction for errors
    
    // Reduce score based on slow sector rate
    uint32_t slow_rate = (scanner->health.slow_sectors_found * 10000) / total_scanned;
    health_score -= min(slow_rate / 20, 30U); // Max 30 point reduction for slow sectors
    
    scanner->health.overall_health_score = health_score;
    scanner->health.last_health_update = ktime_get_real_seconds();
    
    DMR_DEBUG(1, "Health score updated: %u%% (errors=%u, slow=%u, scanned=%llu)",
              health_score, scanner->health.error_sectors_found,
              scanner->health.slow_sectors_found, total_scanned);
}
```

## 🖥️ Sysfs Interface

### **Configuration Interface**
```c
/**
 * Sysfs attributes for background scanner configuration
 */

// /sys/kernel/dm_remap/background_scan_enabled
static ssize_t scan_enabled_show(struct kobject *kobj,
                                 struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", global_scanner.enabled ? 1 : 0);
}

static ssize_t scan_enabled_store(struct kobject *kobj,
                                 struct kobj_attribute *attr,
                                 const char *buf, size_t count)
{
    int enabled;
    int ret = kstrtoint(buf, 10, &enabled);
    
    if (ret) {
        return ret;
    }
    
    mutex_lock(&global_scanner.scan_mutex);
    
    if (enabled && !global_scanner.enabled) {
        global_scanner.enabled = true;
        dm_remap_scanner_start(&global_scanner);
        DMR_DEBUG(1, "Background scanning enabled");
    } else if (!enabled && global_scanner.enabled) {
        global_scanner.enabled = false;
        cancel_delayed_work_sync(&global_scanner.scan_work);
        DMR_DEBUG(1, "Background scanning disabled");
    }
    
    mutex_unlock(&global_scanner.scan_mutex);
    
    return count;
}

// /sys/kernel/dm_remap/scan_interval_hours
static ssize_t scan_interval_show(struct kobject *kobj,
                                 struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n", global_scanner.scan_interval_seconds / 3600);
}

static ssize_t scan_interval_store(struct kobject *kobj,
                                  struct kobj_attribute *attr,
                                  const char *buf, size_t count)
{
    uint32_t hours;
    int ret = kstrtou32(buf, 10, &hours);
    
    if (ret || hours > 168) { // Max 1 week
        return -EINVAL;
    }
    
    mutex_lock(&global_scanner.scan_mutex);
    global_scanner.scan_interval_seconds = hours * 3600;
    mutex_unlock(&global_scanner.scan_mutex);
    
    DMR_DEBUG(1, "Scan interval set to %u hours", hours);
    
    return count;
}

// /sys/kernel/dm_remap/scan_status
static ssize_t scan_status_show(struct kobject *kobj,
                               struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,
        "Background Scan Status:\n"
        "  Enabled: %s\n"
        "  Active: %s\n"
        "  Progress: %u%% (%llu / %llu sectors)\n"
        "  Current sector: %llu\n"
        "  Last scan completed: %llu seconds ago\n"
        "  Health score: %u%%\n"
        "  Errors found: %u sectors\n"
        "  Slow sectors: %u sectors\n"
        "  Total scanned: %llu sectors\n"
        "  Scan time: %llu ms\n"
        "  Predictive remaps: %llu\n",
        global_scanner.enabled ? "Yes" : "No",
        atomic_read(&global_scanner.scan_active) ? "Yes" : "No",
        global_scanner.scan_progress_percent,
        global_scanner.current_sector,
        global_scanner.total_sectors,
        global_scanner.current_sector,
        ktime_get_real_seconds() - global_scanner.last_scan_complete,
        global_scanner.health.overall_health_score,
        global_scanner.health.error_sectors_found,
        global_scanner.health.slow_sectors_found,
        atomic64_read(&global_scanner.stats.sectors_scanned),
        atomic64_read(&global_scanner.stats.scan_time_ms),
        atomic64_read(&global_scanner.stats.predictive_remaps)
    );
}
```

## 📋 Implementation Checklist

### Week 1-2 Deliverables
- [ ] Implement work queue management with dedicated scanner thread
- [ ] Create adaptive I/O scheduling based on system load
- [ ] Build sector health assessment with latency monitoring
- [ ] Add health scoring algorithm with error/slow sector tracking
- [ ] Implement sysfs interface for configuration and monitoring
- [ ] Add predictive remapping trigger logic
- [ ] Create comprehensive performance monitoring

### Integration Requirements
- [ ] Integrate with existing dm-remap remap logic
- [ ] Add to metadata storage for health data persistence
- [ ] Include in module initialization and cleanup
- [ ] Add to existing sysfs interface structure

## 🎯 Success Criteria

### **Performance Targets**
- [ ] <2% I/O performance impact during scanning
- [ ] Adaptive chunk sizing based on system load
- [ ] <100ms average scan latency per iteration

### **Functionality Targets**
- [ ] Configurable scan intervals (1-168 hours)
- [ ] Real-time health scoring (0-100 scale)
- [ ] Automatic yield to high-priority I/O

### **Enterprise Features**
- [ ] Comprehensive sysfs monitoring interface
- [ ] Predictive remapping based on scan results
- [ ] Detailed statistics and performance metrics

---

## 📚 Technical References

- **Linux Work Queues**: `include/linux/workqueue.h`
- **Block I/O Interface**: `include/linux/bio.h`
- **Time Management**: `include/linux/ktime.h`
- **Sysfs Interface**: `include/linux/sysfs.h`

This background scanning architecture provides intelligent, non-intrusive device health monitoring that enables predictive maintenance while maintaining excellent I/O performance for dm-remap v4.0.