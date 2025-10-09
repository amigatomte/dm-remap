#!/bin/bash
# Systematic Core Optimization Testing
# Tests each Week 9-10 optimization individually to isolate hanging cause

set -e

echo "=== SYSTEMATIC CORE OPTIMIZATION TESTING ==="
echo "Week 7-8 baseline works, Week 9-10 hangs"
echo "Testing each optimization individually..."
echo ""

# Test 1: Memory Pool ONLY (disable hotpath)
echo "TEST 1: Memory Pool ONLY (hotpath disabled)"
echo "============================================"

# Disable hotpath init
sed -i '/ret = dmr_hotpath_init(rc);/s/^/    \/\* DISABLED: /' src/dm_remap_main.c
sed -i '/ret = dmr_hotpath_init(rc);/a\    ret = 0; \/\* Skip hotpath for testing *\/' src/dm_remap_main.c

echo "Building with hotpath disabled..."
cd src && make clean > /dev/null && make > /dev/null && cd ..

echo "Loading module..."
sudo insmod src/dm_remap.ko

echo "Testing device creation (Memory Pool only)..."
timeout 10s sudo dmsetup create dm_remap_test1 --table "0 2048 remap /dev/loop20 /dev/loop21 0 1024" && echo "✅ Memory Pool works!" || echo "❌ Memory Pool hangs!"

# Cleanup
sudo dmsetup remove --force dm_remap_test1 2>/dev/null || true
sudo rmmod dm_remap 2>/dev/null || true

echo ""
echo "TEST 2: Hotpath ONLY (memory pool disabled)"
echo "==========================================="

# Restore hotpath, disable memory pool
git checkout -- src/dm_remap_main.c
sed -i '/ret = dmr_pool_manager_init(rc);/s/^/    \/\* DISABLED: /' src/dm_remap_main.c
sed -i '/ret = dmr_pool_manager_init(rc);/a\    ret = 0; \/\* Skip memory pool for testing *\/' src/dm_remap_main.c

echo "Building with memory pool disabled..."
cd src && make clean > /dev/null && make > /dev/null && cd ..

echo "Loading module..."
sudo insmod src/dm_remap.ko

echo "Testing device creation (Hotpath only)..."
timeout 10s sudo dmsetup create dm_remap_test2 --table "0 2048 remap /dev/loop20 /dev/loop21 0 1024" && echo "✅ Hotpath works!" || echo "❌ Hotpath hangs!"

# Cleanup
sudo dmsetup remove --force dm_remap_test2 2>/dev/null || true
sudo rmmod dm_remap 2>/dev/null || true

echo ""
echo "TEST 3: BOTH disabled (should work like baseline)"
echo "================================================"

# Disable both
sed -i '/ret = dmr_hotpath_init(rc);/s/^/    \/\* DISABLED: /' src/dm_remap_main.c
sed -i '/ret = dmr_hotpath_init(rc);/a\    ret = 0; \/\* Skip hotpath for testing *\/' src/dm_remap_main.c

echo "Building with both optimizations disabled..."
cd src && make clean > /dev/null && make > /dev/null && cd ..

echo "Loading module..."
sudo insmod src/dm_remap.ko

echo "Testing device creation (no optimizations)..."
timeout 10s sudo dmsetup create dm_remap_test3 --table "0 2048 remap /dev/loop20 /dev/loop21 0 1024" && echo "✅ Both disabled works!" || echo "❌ Still hangs - deeper issue!"

echo ""
echo "=== TEST COMPLETE ==="
echo "Results will show which optimization causes the hang"