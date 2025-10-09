#!/bin/bash
# Test specific lines in dmr_hotpath_init() one by one
set -e

echo "TARGETED LINE TESTING FOR dmr_hotpath_init()"
echo "=============================================="
echo ""

function test_version() {
    local test_name="$1"
    local test_code="$2"
    
    echo "Testing: $test_name"
    
    # Create the test function
    cat > temp_function.c << EOF
int dmr_hotpath_init(struct remap_c *rc)
{
$test_code
    return 0;
}
EOF
    
    # Replace the function in the file
    cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c
    
    # Find the function and replace it
    awk '
    /^int dmr_hotpath_init\(struct remap_c \*rc\)$/ {
        in_function = 1
        system("cat temp_function.c")
        next
    }
    in_function && /^}$/ {
        in_function = 0
        next
    }
    !in_function {
        print
    }
    ' src/dm-remap-hotpath-optimization.c > temp_file.c
    
    mv temp_file.c src/dm-remap-hotpath-optimization.c
    
    # Build
    cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "❌ Build failed"
        cd ..
        return 1
    fi
    cd ..
    
    # Test with timeout
    echo -n "  Loading module... "
    timeout 3s sudo insmod src/dm_remap.ko 2>/dev/null || {
        echo "🚨 HANG DETECTED!"
        sudo pkill -f insmod 2>/dev/null || true
        return 2
    }
    
    echo "✅ Success - unloading..."
    sudo rmmod dm_remap
    echo ""
    return 0
}

# Test 1: Empty function
test_version "Empty function" ""
if [ $? -eq 2 ]; then
    echo "🚨 CRITICAL: Even empty function hangs!"
    echo "The problem is NOT in the function body - it's in the CALL to dmr_hotpath_init()"
    exit 1
fi

# Test 2: Just the printk
test_version "Just first printk" '    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\\n");'
if [ $? -eq 2 ]; then
    echo "🚨 First printk causes hang!"
    exit 1
fi

# Test 3: Parameter check
test_version "Parameter validation" '
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\\n");
        return -EINVAL;
    }'
if [ $? -eq 2 ]; then
    echo "🚨 Parameter validation causes hang!"
    exit 1
fi

# Test 4: Add variable declaration
test_version "Variable declaration" '
    struct dmr_hotpath_manager *manager;
    
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\\n");
        return -EINVAL;
    }'
if [ $? -eq 2 ]; then
    echo "🚨 Variable declaration causes hang!"
    exit 1
fi

# Test 5: Add allocation printk
test_version "Pre-allocation printk" '
    struct dmr_hotpath_manager *manager;
    
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: About to allocate manager\\n");'
if [ $? -eq 2 ]; then
    echo "🚨 Pre-allocation printk causes hang!"
    exit 1
fi

# Test 6: THE SUSPECT - allocation
test_version "THE ALLOCATION LINE" '
    struct dmr_hotpath_manager *manager;
    
    printk(KERN_INFO "dmr_hotpath_init: Starting TEST 1 - allocation test\\n");
    
    if (!rc) {
        printk(KERN_ERR "dmr_hotpath_init: Invalid remap context\\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "dmr_hotpath_init: About to allocate manager\\n");
    
    /* THIS IS THE SUSPECT LINE */
    manager = dmr_alloc_cache_aligned(sizeof(*manager));'
if [ $? -eq 2 ]; then
    echo "🎯 FOUND IT! The allocation line causes the hang!"
    echo "manager = dmr_alloc_cache_aligned(sizeof(*manager));"
    exit 1
fi

echo "🤔 None of the lines cause hanging individually..."
echo "Testing the full function..."

# Restore original and test
cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c

cd src && make clean > /dev/null 2>&1 && make > /dev/null 2>&1 && cd ..

timeout 3s sudo insmod src/dm_remap.ko 2>/dev/null || {
    echo "🚨 Full function still hangs!"
    echo "The issue must be in the combination or later parts of the function"
}

# Cleanup
rm -f temp_function.c
cp src/dm-remap-hotpath-optimization.c.original src/dm-remap-hotpath-optimization.c