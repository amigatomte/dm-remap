#!/bin/bash
# Minimal test: Comment out dmr_hotpath_init contents to find hanging line
set -e

echo "MINIMAL HANGING ISOLATION TEST"
echo "=============================="
echo "Strategy: Empty dmr_hotpath_init() and add back line by line"
echo ""

# Create baseline version with empty hotpath init
echo "1. Creating minimal hotpath init (empty function)..."

# Start with baseline
cp src/dm_remap_main.c.baseline src/dm_remap_main.c

# Add minimal hotpath initialization
sed -i '/dm-remap-performance.h/a #include "dm-remap-hotpath-optimization.h" \/\/ Minimal test' src/dm_remap_main.c

# Add hotpath init call
sed -i '/dm_remap_autosave_start.*metadata/a\\n    \/\* Minimal test: Call hotpath init \*\/\n    ret = dmr_hotpath_init(rc);\n    if (ret) {\n        DMR_DEBUG(0, "Hotpath init failed: %d", ret);\n        rc->hotpath_manager = NULL;\n    } else {\n        DMR_DEBUG(0, "Hotpath init succeeded");\n    }' src/dm_remap_main.c

echo "2. Creating EMPTY dmr_hotpath_init() function..."

# Backup original
cp src/dm-remap-hotpath-optimization.c src/dm-remap-hotpath-optimization.c.original

# Create empty version of dmr_hotpath_init
cat > temp_empty_init.c << 'EOF'
int dmr_hotpath_init(struct remap_c *rc)
{
    printk(KERN_INFO "dmr_hotpath_init: Starting (empty version)\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: Setting NULL hotpath_manager\n");
    rc->hotpath_manager = NULL;
    
    printk(KERN_INFO "dmr_hotpath_init: Completed successfully (empty version)\n");
    return 0;
}
EOF

# Replace the function in the source file
sed -i '/^int dmr_hotpath_init/,/^}$/c\
int dmr_hotpath_init(struct remap_c *rc)\
{\
    printk(KERN_INFO "dmr_hotpath_init: Starting (empty version)\\n");\
    \
    if (!rc) {\
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\\n");\
        return -EINVAL;\
    }\
    \
    printk(KERN_INFO "dmr_hotpath_init: Setting NULL hotpath_manager\\n");\
    rc->hotpath_manager = NULL;\
    \
    printk(KERN_INFO "dmr_hotpath_init: Completed successfully (empty version)\\n");\
    return 0;\
}' src/dm-remap-hotpath-optimization.c

echo "3. Building with EMPTY hotpath init..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi
cd ..

echo "4. Testing EMPTY hotpath init (should NOT hang)..."
echo "   If this hangs, the issue is elsewhere"
echo "   If this works, the issue is inside dmr_hotpath_init()"

timeout 5s sudo insmod src/dm_remap.ko && echo "✅ EMPTY version loads successfully" || {
    echo "🚨 EMPTY version still hangs - issue is NOT in dmr_hotpath_init content!"
    exit 1
}

# Clean up
sudo rmmod dm_remap
echo ""
echo "🎉 SUCCESS: Empty dmr_hotpath_init() works!"
echo "The hanging is caused by specific code inside the original dmr_hotpath_init()"
echo ""
echo "Next step: Add back original dmr_hotpath_init() lines one by one"