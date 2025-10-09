#!/bin/bash
# Incremental Optimization Debug Strategy
# Goal: Identify which specific optimization function causes hanging

echo "INCREMENTAL OPTIMIZATION DEBUG STRATEGY"
echo "======================================="
echo "Strategy: Enable one optimization at a time to isolate hanging function"
echo ""

# Create debug versions of main file with selective initialization
echo "Creating debug versions with selective optimization initialization..."

# Get the baseline main file (working)
git show 8b3590e:src/dm_remap_main.c > src/dm_remap_main.c.baseline

# Get the v4 main file (hanging)
cp src/dm_remap_main.c src/dm_remap_main.c.v4_full

echo "Files created:"
echo "- dm_remap_main.c.baseline (working Week 7-8)"
echo "- dm_remap_main.c.v4_full (hanging with all optimizations)"
echo ""

echo "Debug Test Plan:"
echo "==============="
echo "Phase A: Test ONLY Memory Pool initialization"
echo "Phase B: Test ONLY Hotpath initialization" 
echo "Phase C: Test ONLY Hotpath Sysfs initialization"
echo "Phase D: Test combinations to find interaction issues"
echo ""

echo "Each phase will:"
echo "1. Create modified main file with only specific optimization"
echo "2. Build and test for hanging"
echo "3. Document results"
echo ""

echo "This will pinpoint the exact function causing the hang."
echo ""
echo "Ready to begin Phase A?"