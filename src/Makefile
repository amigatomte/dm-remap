obj-m := dm-remap-v4-real.o dm-remap-minimal-test.o
# Temporarily removed dm-remap-v4-validation.o due to structural incompatibility
# Temporarily removed dm-remap-v4-version-control.o due to header conflicts (FIXED - can re-enable)
# Temporarily removed dm-remap-v4-health-monitoring.o due to floating-point math (needs integer conversion)
obj-m += dm-remap-v4-spare-pool.o dm-remap-v4-setup-reassembly.o
obj-m += dm-remap-v4-stats.o

# Multi-object modules
dm-remap-v4-real-objs := dm-remap-v4-real-main.o dm-remap-v4-metadata.o dm-remap-v4-metadata-creation.o dm-remap-v4-metadata-utils.o
# dm-remap-v4-version-control-objs := dm-remap-v4-version-control.o dm-remap-v4-version-control-utils.o
dm-remap-v4-health-monitoring-objs := dm-remap-v4-health-monitoring.o dm-remap-v4-health-monitoring-utils.o
dm-remap-v4-setup-reassembly-objs := dm-remap-v4-setup-reassembly-core.o dm-remap-v4-setup-reassembly-storage.o dm-remap-v4-setup-reassembly-discovery.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -Wall -Wno-unused-parameter -I$(PWD) -I$(PWD)/../include
ccflags-y += -DDM_REMAP_VERSION=\"4.0.0-real\"

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
