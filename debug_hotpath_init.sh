#!/bin/bash
# Debug Real dmr_hotpath_init with Debug Logging
set -e

echo "DEBUG: TESTING REAL dmr_hotpath_init() WITH LOGGING"
echo "==================================================="
echo "Goal: Add debug logging to identify exact hanging point"
echo ""

# Create version that calls real dmr_hotpath_init but with debug logging
echo "1. Creating debug version of dmr_hotpath_init()..."

# First, let's add debug logging to the actual dmr_hotpath_init function
cp src/dm-remap-hotpath-optimization.c src/dm-remap-hotpath-optimization.c.backup

# Add debug logging at each major step in dmr_hotpath_init
cat > debug_hotpath_patch.txt << 'EOF'
int dmr_hotpath_init(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    int i;

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Starting\n");

    if (!rc) {
        printk(KERN_ERR "DEBUG: dmr_hotpath_init() - Invalid remap context\n");
        return -EINVAL;
    }

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - About to allocate manager\n");
    
    /* Allocate cache-aligned hotpath manager */
    manager = dmr_alloc_cache_aligned(sizeof(*manager));
    if (!manager) {
        printk(KERN_ERR "DEBUG: dmr_hotpath_init() - Manager allocation failed\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Manager allocated, initializing context\n");

    /* Initialize hotpath context */
    manager->context.sector = 0;
    manager->context.flags = 0;
    manager->context.bio_size = 0;
    manager->context.batch_count = 0;
    
    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - About to init spinlock\n");
    spin_lock_init(&manager->context.batch_lock);

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Initializing atomic counters\n");
    
    /* Initialize performance counters */
    atomic64_set(&manager->context.fast_reads, 0);
    atomic64_set(&manager->context.fast_writes, 0);
    atomic64_set(&manager->context.slow_path_fallbacks, 0);
    atomic64_set(&manager->context.cache_hits, 0);

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Initializing statistics\n");
    
    /* Initialize statistics */
    memset(&manager->stats, 0, sizeof(manager->stats));

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Setting optimization parameters\n");
    
    /* Set up optimization parameters */
    manager->prefetch_distance = 8;     /* Prefetch 8 sectors ahead */
    manager->batch_timeout_ms = 1;      /* 1ms batch timeout */
    manager->adaptive_batching = true;
    
    /* Initialize cache optimization state */
    manager->last_accessed_sector = 0;
    manager->sequential_count = 0;
    manager->last_stats_time = jiffies;

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Clearing batch bio array\n");
    
    /* Clear batch bio array */
    for (i = 0; i < DMR_HOTPATH_BATCH_SIZE; i++) {
        manager->context.batch_bios[i] = NULL;
    }

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Clearing prefetch targets\n");
    
    /* Clear prefetch targets */
    for (i = 0; i < DMR_HOTPATH_PREFETCH_LINES; i++) {
        manager->context.prefetch_targets[i] = NULL;
    }

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Setting manager in rc\n");
    
    rc->hotpath_manager = manager;

    printk(KERN_INFO "DEBUG: dmr_hotpath_init() - Completed successfully\n");
    return 0;
}
EOF

# Replace the function (find line number first)
INIT_LINE=$(grep -n "int dmr_hotpath_init" src/dm-remap-hotpath-optimization.c | cut -d: -f1)
if [ -n "$INIT_LINE" ]; then
    # Get the function end (find the closing brace)
    END_LINE=$(tail -n +$INIT_LINE src/dm-remap-hotpath-optimization.c | grep -n "^}" | head -1 | cut -d: -f1)
    END_LINE=$((INIT_LINE + END_LINE - 1))
    
    echo "   Found dmr_hotpath_init at lines $INIT_LINE-$END_LINE"
    
    # Replace the function
    head -n $((INIT_LINE - 1)) src/dm-remap-hotpath-optimization.c > temp_hotpath.c
    cat debug_hotpath_patch.txt >> temp_hotpath.c
    tail -n +$((END_LINE + 1)) src/dm-remap-hotpath-optimization.c >> temp_hotpath.c
    mv temp_hotpath.c src/dm-remap-hotpath-optimization.c
    
    echo "   ✅ Added debug logging to dmr_hotpath_init()"
else
    echo "   ❌ Could not find dmr_hotpath_init function"
    exit 1
fi

rm debug_hotpath_patch.txt

# Create main file that calls the debug version
cp src/dm_remap_main.c.baseline src/dm_remap_main.c.debug_hotpath

# Add hotpath includes
sed -i '/dm-remap-performance.h/a #include "dm-remap-hotpath-optimization.h"' src/dm_remap_main.c.debug_hotpath

# Add hotpath initialization call
sed -i '/dm_remap_autosave_start.*metadata/a\\n    \/\* DEBUG: Call dmr_hotpath_init with logging \*\/\n    printk(KERN_INFO "DEBUG: About to call dmr_hotpath_init\\n");\n    ret = dmr_hotpath_init(rc);\n    if (ret) {\n        printk(KERN_ERR "DEBUG: dmr_hotpath_init failed: %d\\n", ret);\n        rc->hotpath_manager = NULL;\n    } else {\n        printk(KERN_INFO "DEBUG: dmr_hotpath_init returned successfully\\n");\n    }' src/dm_remap_main.c.debug_hotpath

# Install debug version
cp src/dm_remap_main.c.debug_hotpath src/dm_remap_main.c

echo ""
echo "2. Building debug version..."
cd src && make clean && make
cd ..

echo ""
echo "3. Testing debug version (monitoring kernel logs)..."
echo "Watch kernel logs in another terminal: sudo dmesg -w"
echo "Press Enter when ready to continue..."
read

echo "Loading module with debug logging..."
timeout 10s sudo insmod src/dm_remap.ko || {
    echo ""
    echo "🚨 Module loading timed out! Check kernel logs for last debug message"
    echo "This will show exactly where in dmr_hotpath_init() the hang occurs"
    exit 1
}

echo "✅ Module loaded successfully! Check logs for debug messages"
sudo rmmod dm_remap

echo ""
echo "Next: Check dmesg output to see where the hanging occurs"