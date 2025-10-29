# ============================================================================
# dm-remap Kernel Module Build Configuration
# ============================================================================
#
# BUILD_MODE options:
#   INTEGRATED (default) - Single dm-remap.ko with all features
#                          (clean builds, simpler installation)
#   MODULAR              - Separate .ko files for features
#                          (requires proper MODULE_LICENSE in each file)
#
# Usage:
#   make                                # Build integrated (default)
#   make clean                          # Clean build
#   BUILD_MODE=MODULAR make             # Build with separate modules
#   BUILD_MODE=MODULAR make clean       # Clean modular build
#
# ============================================================================

BUILD_MODE ?= INTEGRATED

ifeq ($(BUILD_MODE),INTEGRATED)
  # ========== INTEGRATED BUILD (DEFAULT) ==========
  # Single kernel module with all features compiled together
  # Recommended for most installations - simplest deployment
  obj-m := dm-remap.o dm-remap-test.o
  
  # All components integrated into main module:
  # - Core remapping engine
  # - Metadata management
  # - Spare pool management
  # - Setup and reassembly
  # - Statistics collection
  dm-remap-objs := \
      dm-remap-core.o \
      dm-remap-v4-metadata.o \
      dm-remap-v4-metadata-creation.o \
      dm-remap-v4-metadata-utils.o \
      dm-remap-v4-repair.o \
      dm-remap-v4-spare-pool.o \
      dm-remap-v4-setup-reassembly-core.o \
      dm-remap-v4-setup-reassembly-storage.o \
      dm-remap-v4-setup-reassembly-discovery.o \
      dm-remap-v4-stats.o
  
else ifeq ($(BUILD_MODE),MODULAR)
  # ========== MODULAR BUILD ==========
  # Separate kernel modules for maximum flexibility
  # Each module can be independently loaded/unloaded
  # Note: Requires proper MODULE_LICENSE in each file
  obj-m := dm-remap.o dm-remap-test.o
  obj-m += dm-remap-spare-pool.o
  obj-m += dm-remap-setup-reassembly.o
  obj-m += dm-remap-stats.o
  
  # Core remapping module (required)
  dm-remap-objs := \
      dm-remap-core.o \
      dm-remap-v4-metadata.o \
      dm-remap-v4-metadata-creation.o \
      dm-remap-v4-metadata-utils.o \
      dm-remap-v4-repair.o
  
  # Optional feature modules (load as needed)
  dm-remap-spare-pool-objs := dm-remap-v4-spare-pool.o
  dm-remap-setup-reassembly-objs := \
      dm-remap-v4-setup-reassembly-core.o \
      dm-remap-v4-setup-reassembly-storage.o \
      dm-remap-v4-setup-reassembly-discovery.o
  dm-remap-stats-objs := dm-remap-v4-stats.o
  
else
  $(error Invalid BUILD_MODE: $(BUILD_MODE). Use INTEGRATED or MODULAR)
endif

# Optional: Disabled modules (can be re-enabled when fixed)
# dm-remap-v4-version-control-objs := dm-remap-v4-version-control.o dm-remap-v4-version-control-utils.o
# dm-remap-v4-health-monitoring-objs := dm-remap-v4-health-monitoring.o dm-remap-v4-health-monitoring-utils.o
# Note: version-control has header conflicts, health-monitoring needs floating-point to integer conversion

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -Wall -Wno-unused-parameter -I$(PWD) -I$(PWD)/../include
ccflags-y += -DDM_REMAP_VERSION=\"4.0.0-real\"

# ============================================================================
# Build targets
# ============================================================================

all: show-build-mode
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

show-build-mode:
	@echo "=========================================="
	@echo "dm-remap Build Configuration"
	@echo "=========================================="
	@echo "BUILD_MODE: $(BUILD_MODE)"
ifeq ($(BUILD_MODE),INTEGRATED)
	@echo "Output: dm-remap.ko (single module)"
	@echo "Features: All core features compiled in"
	@echo ""
	@echo "Benefits:"
	@echo "  ✓ Simple installation (single file)"
	@echo "  ✓ No dependency management"
	@echo "  ✓ Standard Linux practice"
	@echo "  ✓ Recommended for production"
else ifeq ($(BUILD_MODE),MODULAR)
	@echo "Output: Multiple kernel modules"
	@echo "Modules:"
	@echo "  • dm-remap.ko (core remapping engine)"
	@echo "  • dm-remap-spare-pool.ko (spare device management)"
	@echo "  • dm-remap-setup-reassembly.ko (device setup)"
	@echo "  • dm-remap-stats.ko (statistics collection)"
	@echo ""
	@echo "Benefits:"
	@echo "  ✓ Load features independently"
	@echo "  ✓ Easier testing and troubleshooting"
	@echo "  ✓ Update components separately"
endif
	@echo "=========================================="
	@echo ""

.PHONY: all clean show-build-mode
