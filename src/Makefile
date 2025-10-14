obj-m := dm-remap-v4-real.o dm-remap-v4-metadata.o dm-remap-v4-validation.o dm-remap-v4-version-control.o

# Multi-object modules
dm-remap-v4-metadata-objs := dm-remap-v4-metadata-creation.o dm-remap-v4-metadata-utils.o
dm-remap-v4-version-control-objs := dm-remap-v4-version-control.o dm-remap-v4-version-control-utils.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -Wall -Wno-unused-parameter -I$(PWD) -I$(PWD)/../include
ccflags-y += -DDM_REMAP_VERSION=\"4.0.0-real\"

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
