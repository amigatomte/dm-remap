#!/bin/bash
# Integration target for dmremap-status tool build

# This makefile target builds the dmremap-status userspace tool
# It's meant to be called from the main dm-remap Makefile

DMREMAP_STATUS_DIR="tools/dmremap-status"

# Build the tool
build-dmremap-status:
	@echo "Building dmremap-status userspace tool..."
	$(MAKE) -C $(DMREMAP_STATUS_DIR)
	@echo "✓ dmremap-status built successfully"

# Install the tool
install-dmremap-status: build-dmremap-status
	@echo "Installing dmremap-status..."
	$(MAKE) -C $(DMREMAP_STATUS_DIR) install
	@echo "✓ dmremap-status installed successfully"

# Clean the tool
clean-dmremap-status:
	$(MAKE) -C $(DMREMAP_STATUS_DIR) clean
	@echo "✓ dmremap-status cleaned"

# Run tests for the tool
test-dmremap-status: build-dmremap-status
	@echo "Testing dmremap-status with test data..."
	$(DMREMAP_STATUS_DIR)/dmremap-status --input $(DMREMAP_STATUS_DIR)/test_status.txt --format human
	@echo ""
	@echo "✓ All dmremap-status tests passed"

.PHONY: build-dmremap-status install-dmremap-status clean-dmremap-status test-dmremap-status
