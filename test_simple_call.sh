#!/bin/bash
# Simple test: Replace dmr_hotpath_init() call with a return 0
set -e

echo "SIMPLE TEST: Remove dmr_hotpath_init() call entirely"
echo "===================================================="
echo ""

# Create a version without the call
cp src/dm_remap_main.c src/dm_remap_main.c.original

# Use a more careful replacement
awk '
/ret = dmr_hotpath_init\(rc\);/ {
    print "    /* REMOVED: ret = dmr_hotpath_init(rc); */"
    print "    ret = 0; /* Skip hotpath init for testing */"
    next
}
/if \(ret\) {/ && seen_hotpath {
    print "    if (0) { /* Skip hotpath error handling */"
    seen_hotpath = 0
    next
}
/ret = dmr_hotpath_init\(rc\);/ { seen_hotpath = 1 }
{ print }
' src/dm_remap_main.c.original > src/dm_remap_main.c

echo "1. Building without dmr_hotpath_init() call..."
cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "❌ Build failed - restoring"
    cp dm_remap_main.c.original dm_remap_main.c
    cd ..
    exit 1
fi
cd ..

echo "2. Testing module load..."
timeout 5s sudo insmod src/dm_remap.ko || {
    echo "🚨 STILL HANGS without dmr_hotpath_init() call!"
    echo "The problem is NOT the function call - it's something else!"
    sudo pkill -f insmod 2>/dev/null || true
    cp src/dm_remap_main.c.original src/dm_remap_main.c
    exit 1
}

echo "✅ SUCCESS! Module loads without dmr_hotpath_init() call"
sudo rmmod dm_remap

echo ""
echo "🎯 CONFIRMED: The dmr_hotpath_init() call is causing the hang!"
echo ""
echo "Now testing if it's the function definition or just the call..."

# Restore the call but comment out the function body
cp src/dm_remap_main.c.original src/dm_remap_main.c

echo "3. Creating stub dmr_hotpath_init() function..."
# Replace function with just return 0
sed -i '/^int dmr_hotpath_init(struct remap_c \*rc)$/,/^}$/{
    /^int dmr_hotpath_init(struct remap_c \*rc)$/!{
        /^}$/!d
    }
    /^int dmr_hotpath_init(struct remap_c \*rc)$/a\
{\
    return 0;\
}
}' src/dm-remap-hotpath-optimization.c

echo "4. Building with stub function..."
cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "❌ Build failed with stub"
    cd ..
    exit 1
fi
cd ..

echo "5. Testing with stub function..."
timeout 5s sudo insmod src/dm_remap.ko || {
    echo "🚨 HANGS even with stub function!"
    echo "The problem is in the CALL CONTEXT or function ENTRY!"
    sudo pkill -f insmod 2>/dev/null || true
    exit 1
}

echo "✅ SUCCESS with stub function!"
sudo rmmod dm_remap

# Restore original
cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c
cp src/dm_remap_main.c.original src/dm_remap_main.c

echo ""
echo "🎯 CONCLUSION:"
echo "   - Module works WITHOUT dmr_hotpath_init() call"
echo "   - Module works WITH stub dmr_hotpath_init() function"
echo "   - Problem is in the ORIGINAL function implementation"