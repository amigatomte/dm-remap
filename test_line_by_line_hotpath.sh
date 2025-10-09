#!/bin/bash
# Line-by-line test of dmr_hotpath_init() to find exact hanging line
set -e

echo "LINE-BY-LINE DEBUGGING OF dmr_hotpath_init()"
echo "============================================="
echo "Goal: Find the exact line in dmr_hotpath_init() that causes hanging"
echo ""

# Create test versions by adding lines incrementally
test_lines() {
    local test_name="$1"
    local lines_to_add="$2"
    
    echo "Testing: $test_name"
    echo "Lines: $lines_to_add"
    
    # Create test version of hotpath init function
    cat > src/dmr_hotpath_init_test.c << EOF
/*
 * Test version of dmr_hotpath_init() - $test_name
 */

int dmr_hotpath_init(struct remap_c *rc)
{
    $lines_to_add
    
    return 0;
}
EOF
    
    # Replace the original function with our test version
    cp src/dm-remap-hotpath-optimization.c src/dm-remap-hotpath-optimization.c.backup
    
    # Extract everything before dmr_hotpath_init
    sed -n '1,/^int dmr_hotpath_init/p' src/dm-remap-hotpath-optimization.c.backup | head -n -1 > src/dm-remap-hotpath-optimization.c.temp
    
    # Add our test function
    cat src/dmr_hotpath_init_test.c >> src/dm-remap-hotpath-optimization.c.temp
    
    # Add everything after the original function
    sed -n '/^}/,$p' src/dm-remap-hotpath-optimization.c.backup | tail -n +2 >> src/dm-remap-hotpath-optimization.c.temp
    
    # Install test version
    mv src/dm-remap-hotpath-optimization.c.temp src/dm-remap-hotpath-optimization.c
    
    # Build and test
    echo "  Building..."
    cd src && make clean && make > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "  ❌ Build failed for $test_name"
        cd ..
        return 1
    fi
    cd ..
    
    echo "  Loading module (5s timeout)..."
    timeout 5s sudo insmod src/dm_remap.ko || {
        echo "  🚨 HANGING DETECTED in $test_name!"
        echo "  The hanging line is in: $lines_to_add"
        return 1
    }
    
    echo "  ✅ $test_name works - unloading..."
    sudo rmmod dm_remap
    return 0
}

echo "Starting line-by-line tests..."
echo ""

# Test 1: Basic parameter validation
test_lines "Basic validation" 'if (!rc) return -EINVAL;' || exit 1

# Test 2: Add struct declaration
test_lines "Variable declaration" '
    struct dmr_hotpath_manager *manager;
    if (!rc) return -EINVAL;' || exit 1

# Test 3: Add allocation (SUSPECTED HANGING LINE)
test_lines "Cache-aligned allocation" '
    struct dmr_hotpath_manager *manager;
    if (!rc) return -EINVAL;
    manager = dmr_alloc_cache_aligned(sizeof(*manager));' || exit 1

echo ""
echo "🎉 All basic tests passed - the issue might be deeper in the function"
echo "Let's test the allocation line more specifically..."