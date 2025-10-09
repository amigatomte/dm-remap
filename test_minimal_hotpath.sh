#!/bin/bash
# Simplified approach: Test specific suspected lines in dmr_hotpath_init()
set -e

echo "TESTING SUSPECTED HANGING LINES IN dmr_hotpath_init()"
echo "==================================================="
echo ""

# First, let's see what the original function looks like
echo "1. Checking original dmr_hotpath_init() function..."
grep -A 10 "int dmr_hotpath_init" src/dm-remap-hotpath-optimization.c

echo ""
echo "2. Let's test the most suspected line: dmr_alloc_cache_aligned()"
echo ""

# Create a minimal test by commenting out most of the function content
echo "3. Creating minimal test version..."

# Backup original
cp src/dm-remap-hotpath-optimization.c src/dm-remap-hotpath-optimization.c.original

# Create a version that comments out everything except the suspected allocation
sed -i '/int dmr_hotpath_init/,/^}$/{
    # Keep function signature
    /int dmr_hotpath_init/!{
        /^}$/!{
            # Comment out everything except basic validation and allocation
            /if (!rc)/!{
                /struct dmr_hotpath_manager \*manager;/!{
                    /manager = dmr_alloc_cache_aligned/!{
                        /return/!s/^/    \/\* COMMENTED: /
                    }
                }
            }
        }
    }
}' src/dm-remap-hotpath-optimization.c

echo "4. Building minimal test version..."
cd src && make clean && make > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "❌ Build failed - restoring original"
    cp dm-remap-hotpath-optimization.c.original dm-remap-hotpath-optimization.c
    exit 1
fi
cd ..

echo "5. Testing minimal version (5s timeout)..."
timeout 5s sudo insmod src/dm_remap.ko || {
    echo ""
    echo "🚨 HANGING STILL DETECTED!"
    echo "The issue is in one of the remaining lines:"
    echo "- if (!rc) return -EINVAL;"
    echo "- struct dmr_hotpath_manager *manager;"
    echo "- manager = dmr_alloc_cache_aligned(sizeof(*manager));"
    sudo pkill -f insmod 2>/dev/null || true
    cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c
    exit 1
}

echo "✅ Minimal version works - unloading..."
sudo rmmod dm_remap

echo ""
echo "🤔 Interesting - the minimal version works!"
echo "This means the hanging is in the LATER parts of dmr_hotpath_init()"
echo ""

# Restore original
cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c

echo "The hanging line is NOT in the basic allocation, but somewhere else in the function."