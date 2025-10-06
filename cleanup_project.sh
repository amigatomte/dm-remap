#!/bin/bash
#
# cleanup_project.sh - Clean up outdated documentation, tests, and unused code
# 
# This script organizes the dm-remap project by:
# - Moving outdated v2.0 historical docs to archive/
# - Removing obviously unused/duplicate files
# - Cleaning up build artifacts
# - Organizing debug utilities

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
ARCHIVE_DIR="$SCRIPT_DIR/archive"
BACKUP_DIR="$SCRIPT_DIR/backup_$(date +%Y%m%d_%H%M%S)"

echo -e "${BLUE}================================================================="
echo -e "           dm-remap v3.0 PROJECT CLEANUP"
echo -e "=================================================================${NC}"
echo -e "${YELLOW}Date: $(date)${NC}"
echo -e "${YELLOW}Creating backup at: $BACKUP_DIR${NC}"
echo ""

# Create directories
mkdir -p "$ARCHIVE_DIR/v2_historical"
mkdir -p "$ARCHIVE_DIR/outdated_tests"
mkdir -p "$ARCHIVE_DIR/debug_utilities"
mkdir -p "$BACKUP_DIR"

echo -e "${BLUE}📁 Creating project structure...${NC}"

# Function to move file with logging
move_file() {
    local src="$1"
    local dst="$2"
    if [ -f "$src" ]; then
        echo -e "${YELLOW}Moving: $(basename "$src") → $dst${NC}"
        # Create backup first
        cp "$src" "$BACKUP_DIR/" 2>/dev/null || true
        mv "$src" "$dst"
    fi
}

# Function to remove file with logging
remove_file() {
    local file="$1"
    if [ -f "$file" ]; then
        echo -e "${RED}Removing: $(basename "$file")${NC}"
        # Create backup first
        cp "$file" "$BACKUP_DIR/" 2>/dev/null || true
        rm "$file"
    fi
}

echo -e "${BLUE}📄 Archiving outdated documentation...${NC}"

# Move v2.0 historical documentation
move_file "WHATS_NEXT_ROADMAP.md" "$ARCHIVE_DIR/v2_historical/"
move_file "V2_AUTO_REMAP_INTELLIGENCE_COMPLETE.md" "$ARCHIVE_DIR/v2_historical/"
move_file "V3_PERSISTENCE_PLAN.md" "$ARCHIVE_DIR/v2_historical/"
move_file "PHASE2_COMPLETE.md" "$ARCHIVE_DIR/v2_historical/"
move_file "PRE_MERGE_VALIDATION_REPORT.md" "$ARCHIVE_DIR/v2_historical/"

echo -e "${BLUE}🧪 Archiving outdated test files...${NC}"

# Move legacy v1 tests
move_file "simple_test_v1.sh" "$ARCHIVE_DIR/outdated_tests/"
move_file "test_v1.1.sh" "$ARCHIVE_DIR/outdated_tests/"
move_file "tests/complete_test_suite_v1.sh" "$ARCHIVE_DIR/outdated_tests/"

# Move obviously outdated test files
move_file "tests/run_dm_remap_tests.sh_old" "$ARCHIVE_DIR/outdated_tests/"
move_file "tests/corrected_remap_verification.sh" "$ARCHIVE_DIR/outdated_tests/"
move_file "tests/explain_dm_linear_error.sh" "$ARCHIVE_DIR/outdated_tests/"
move_file "tests/intelligence_test_v2.sh" "$ARCHIVE_DIR/outdated_tests/"

# Move debug-only test files
move_file "tests/debug_data_integrity.sh" "$ARCHIVE_DIR/debug_utilities/"
move_file "tests/debug_message.sh" "$ARCHIVE_DIR/debug_utilities/"
move_file "tests/debug_metadata.sh" "$ARCHIVE_DIR/debug_utilities/"
move_file "tests/debug_main.img" "$ARCHIVE_DIR/debug_utilities/"
move_file "tests/debug_spare.img" "$ARCHIVE_DIR/debug_utilities/"

echo -e "${BLUE}💻 Archiving debug/development utilities...${NC}"

# Move debug source files
move_file "src/dmmsg.c" "$ARCHIVE_DIR/debug_utilities/"
move_file "src/dmmsg_fixed.c" "$ARCHIVE_DIR/debug_utilities/"
move_file "src/debug_buffer.c" "$ARCHIVE_DIR/debug_utilities/"
move_file "src/debug_comprehensive.c" "$ARCHIVE_DIR/debug_utilities/"

# Move development utilities
move_file "src/dmmsg" "$ARCHIVE_DIR/debug_utilities/"
move_file "src/dmmsg_fixed" "$ARCHIVE_DIR/debug_utilities/"
move_file "src/debug_buffer" "$ARCHIVE_DIR/debug_utilities/"
move_file "src/debug_comprehensive" "$ARCHIVE_DIR/debug_utilities/"

echo -e "${BLUE}🧹 Cleaning up build artifacts...${NC}"

# Remove build artifacts (these should be in .gitignore anyway)
find src/ -name "*.o" -type f -exec rm -f {} \; 2>/dev/null || true
find src/ -name "*.ko" -type f -exec rm -f {} \; 
find src/ -name "*.mod.c" -type f -exec rm -f {} \;
find src/ -name "*.mod" -type f -exec rm -f {} \;
find src/ -name ".*.cmd" -type f -exec rm -f {} \; 2>/dev/null || true
find . -name "modules.order" -type f -exec rm -f {} \; 2>/dev/null || true
find . -name "Module.symvers" -type f -exec rm -f {} \; 2>/dev/null || true

echo -e "${BLUE}📋 Checking for potential duplicates...${NC}"

# Check for potential duplicate tests (just report, don't remove automatically)
echo -e "${YELLOW}Potential duplicate test files (manual review needed):${NC}"
echo "  - tests/simple_io_test.sh vs tests/simple_sector_test.sh"
echo "  - tests/specific_sector_test.sh vs tests/sector_specific_errors.sh"
echo "  - tests/simple_message_test.sh vs tests/working_message_test.sh vs tests/fixed_message_test.sh"

echo -e "${BLUE}🔧 Updating .gitignore...${NC}"

# Create/update .gitignore to prevent build artifacts from being committed
cat >> .gitignore << 'EOF'

# Build artifacts
src/*.o
src/*.ko
src/*.mod
src/*.mod.c
src/.*.cmd
*.order
Module.symvers
.modules.order.cmd
.Module.symvers.cmd

# Debug binaries
src/dmmsg
src/dmmsg_fixed
src/debug_buffer
src/debug_comprehensive

# Test artifacts
tests/debug_*.img
/tmp/dm-remap-*

EOF

echo -e "${GREEN}✅ Cleanup completed successfully!${NC}"
echo ""
echo -e "${BLUE}📊 Summary:${NC}"
echo -e "${YELLOW}  Archived files moved to: $ARCHIVE_DIR${NC}"
echo -e "${YELLOW}  Backup created at: $BACKUP_DIR${NC}"
echo -e "${YELLOW}  Build artifacts cleaned up${NC}"
echo -e "${YELLOW}  .gitignore updated${NC}"
echo ""
echo -e "${BLUE}📋 Archive structure:${NC}"
echo -e "${YELLOW}  archive/v2_historical/     - Old v2.0 documentation${NC}"
echo -e "${YELLOW}  archive/outdated_tests/    - Legacy test files${NC}"
echo -e "${YELLOW}  archive/debug_utilities/   - Debug tools and utilities${NC}"
echo ""
echo -e "${GREEN}Next steps:${NC}"
echo -e "${YELLOW}  1. Review the potential duplicates mentioned above${NC}"
echo -e "${YELLOW}  2. Test that all active functionality still works${NC}"
echo -e "${YELLOW}  3. Commit the cleaned up project structure${NC}"
echo ""
echo -e "${BLUE}Active test suites after cleanup:${NC}"
echo -e "${GREEN}  ✅ tests/complete_test_suite_v3.sh (main v3.0 suite)${NC}"
echo -e "${GREEN}  ✅ tests/complete_test_suite_v2.sh (v2.0 compatibility)${NC}"
echo -e "${GREEN}  ✅ All core functionality and verification tests${NC}"