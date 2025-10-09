#!/bin/bash
# Progressive test: Add back dmr_hotpath_init() lines one by one
set -e

echo "PROGRESSIVE LINE-BY-LINE HANGING ISOLATION"
echo "=========================================="
echo "Strategy: Add back original dmr_hotpath_init() lines until we find the hanging line"
echo ""

# Test 1: Add just the struct allocation
echo "TEST 1: Adding struct allocation line..."

cat > temp_init_test1.c << 'EOF'
int dmr_hotpath_init(struct remap_c *rc)
{
    struct dmr_hotpath_manager *manager;
    
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: About to allocate manager\n");
    
    /* THIS IS THE SUSPECT LINE - cache-aligned allocation */
    manager = dmr_alloc_cache_aligned(sizeof(*manager));
    
    printk(KERN_INFO "dmr_hotpath_init: Allocation completed\n");
    
    if (!manager) {
        printk(KERN_ERR "dmr_hotpath_init: Failed to allocate hotpath manager\n");
        return -ENOMEM;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: Setting manager and returning\n");
    rc->hotpath_manager = manager;
    
    printk(KERN_INFO "dmr_hotpath_init: TEST 1 completed successfully\n");
    return 0;
}
EOF

# Replace the function
sed -i '/^int dmr_hotpath_init/,/^}$/c\
int dmr_hotpath_init(struct remap_c *rc)\
{\
    struct dmr_hotpath_manager *manager;\
    \
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\\n");\
    \
    if (!rc) {\
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\\n");\
        return -EINVAL;\
    }\
    \
    printk(KERN_INFO "dmr_hotpath_init: About to allocate manager\\n");\
    \
    \/\* THIS IS THE SUSPECT LINE - cache-aligned allocation \*\/\
    manager = dmr_alloc_cache_aligned(sizeof(*manager));\
    \
    printk(KERN_INFO "dmr_hotpath_init: Allocation completed\\n");\
    \
    if (!manager) {\
        printk(KERN_ERR "dmr_hotpath_init: Failed to allocate hotpath manager\\n");\
        return -ENOMEM;\
    }\
    \
    printk(KERN_INFO "dmr_hotpath_init: Setting manager and returning\\n");\
    rc->hotpath_manager = manager;\
    \
    printk(KERN_INFO "dmr_hotpath_init: TEST 1 completed successfully\\n");\
    return 0;\
}' src/dm-remap-hotpath-optimization.c

echo "Building TEST 1..."
cd src && make clean && make >/dev/null 2>&1
cd ..

echo "Testing TEST 1 (allocation only)..."
echo "If this hangs, the issue is in dmr_alloc_cache_aligned() or sizeof(*manager)"

timeout 5s sudo insmod src/dm_remap.ko && {
    echo "✅ TEST 1 SUCCESS: Allocation works fine"
    sudo rmmod dm_remap
    echo "The hanging is NOT in the allocation line"
    echo "Need to test initialization lines..."
} || {
    echo "🚨 TEST 1 HANG: The issue is in dmr_alloc_cache_aligned() or sizeof(*manager)!"
    echo "FOUND THE HANGING LINE!"
    exit 1
}