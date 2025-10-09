#!/bin/bash
# Minimal test: Check if the issue is the kmalloc call itself
set -e

echo "MINIMAL HOTPATH ALLOCATION TEST"
echo "==============================="
echo "Testing just the memory allocation part of dmr_hotpath_init"
echo ""

# Create a minimal version that only tests allocation
echo "1. Creating minimal hotpath test..."

# Start with baseline
cp src/dm_remap_main.c.baseline src/dm_remap_main.c.minimal

# Add minimal test code
sed -i '/dm_remap_autosave_start.*metadata/a\\n    \/\* MINIMAL TEST: Just test allocation \*\/\n    {\n        void *test_ptr = kmalloc(1024, GFP_KERNEL | __GFP_ZERO | __GFP_COMP);\n        if (test_ptr) {\n            printk(KERN_INFO "Minimal test: allocation successful\\n");\n            kfree(test_ptr);\n        } else {\n            printk(KERN_ERR "Minimal test: allocation failed\\n");\n        }\n    }' src/dm_remap_main.c.minimal

# Install minimal version
cp src/dm_remap_main.c.minimal src/dm_remap_main.c

echo "   ✅ Minimal test version created"

# Build minimal test
echo "2. Building minimal test..."
cd src && make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi
cd ..

echo "3. Testing minimal allocation (should work)..."
timeout 5s sudo insmod src/dm_remap.ko || {
    echo "🚨 EVEN MINIMAL ALLOCATION HANGS!"
    echo "This suggests a fundamental issue with memory allocation or module structure"
    exit 1
}

echo "✅ Minimal test passed - basic allocation works"

# Clean up
sudo rmmod dm_remap
echo "Module unloaded successfully"

echo ""
echo "RESULT: Basic allocation works, issue is deeper in hotpath init"