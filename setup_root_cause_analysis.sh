#!/bin/bash
# Systematic Root Cause Analysis: Working Phase 2 vs Hanging v4
echo "ROOT CAUSE ANALYSIS: Working Phase 2 vs Hanging v4"
echo "==================================================="
echo ""

# Create analysis directories
mkdir -p analysis/working_phase2
mkdir -p analysis/hanging_v4

# Save current hanging v4 files
echo "1. Saving hanging v4 version files..."
cp src/dm_remap_main.c analysis/hanging_v4/dm_remap_main.c
cp src/dm_remap_io.c analysis/hanging_v4/dm_remap_io.c
cp src/Makefile analysis/hanging_v4/Makefile
cp src/dm-remap-memory-pool.c analysis/hanging_v4/dm-remap-memory-pool.c
cp src/dm-remap-hotpath-optimization.c analysis/hanging_v4/dm-remap-hotpath-optimization.c

echo "   ✅ Hanging v4 files saved to analysis/hanging_v4/"

# Temporarily retrieve working Phase 2 files
echo "2. Retrieving working Phase 2 files..."
git stash show stash@{0} --name-only
echo ""
echo "   Extracting working Phase 2 files from stash..."

# Extract specific files from our working stash
git show stash@{0}:src/dm_remap_main.c > analysis/working_phase2/dm_remap_main.c 2>/dev/null || echo "   (main file extraction)"
git show stash@{0}:src/dm_remap_io.c > analysis/working_phase2/dm_remap_io.c 2>/dev/null || echo "   (io file extraction)"
git show stash@{0}:src/Makefile > analysis/working_phase2/Makefile 2>/dev/null || echo "   (makefile extraction)"

echo "   ✅ Working Phase 2 files saved to analysis/working_phase2/"

echo ""
echo "3. Key Analysis Questions:"
echo "   - What's different in the main initialization?"
echo "   - What's different in the I/O processing?"
echo "   - What's different in the build configuration?"
echo "   - Which specific integration pattern causes hanging?"
echo ""
echo "Ready to perform detailed file comparisons..."