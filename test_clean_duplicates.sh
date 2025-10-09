#!/bin/bash
# Create a clean version by removing duplicate definitions
set -e

echo "CREATING CLEAN HOTPATH FILE WITHOUT DUPLICATES"
echo "=============================================="

# Start with the original
cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c

# Remove the first duplicate function (lines 75-82) and duplicate struct (lines 85-102)
echo "Removing duplicate function and struct definitions..."

# Use awk to remove the duplicates
awk '
    # Skip the test version comment and first function
    /Test version of dmr_hotpath_init/ { skip_until_brace = 1; next }
    skip_until_brace && /^}$/ { skip_until_brace = 0; next }
    skip_until_brace { next }
    
    # Skip the duplicate struct definition (around line 85)
    /^struct dmr_hotpath_manager {/ && !seen_struct {
        seen_struct = 1
        print
        next
    }
    /^struct dmr_hotpath_manager {/ && seen_struct {
        skip_struct = 1
        next
    }
    skip_struct && /^} ____cacheline_aligned;/ {
        skip_struct = 0
        next
    }
    skip_struct { next }
    
    # Print everything else
    { print }
' src/dm-remap-hotpath-optimization.c > temp_clean.c

mv temp_clean.c src/dm-remap-hotpath-optimization.c

echo "Building clean version..."
cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "❌ Build failed with clean version"
    cd ..
    exit 1
fi
cd ..

echo "Testing clean version..."
timeout 5s sudo insmod src/dm_remap.ko || {
    echo "🚨 STILL HANGS with clean version!"
    echo "The problem is in the REMAINING function implementation"
    exit 1
}

echo "✅ SUCCESS! Clean version works"
sudo rmmod dm_remap

echo ""
echo "🎯 CONCLUSION: The duplicate definitions were NOT the cause"
echo "   The clean version works, so the problem was in compilation conflicts"