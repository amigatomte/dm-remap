obj-m := dm-remap-v4-real.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y := -Wall -Wno-unused-parameter -I$(PWD)
ccflags-y += -DDM_REMAP_VERSION=\"4.0.0-real\"

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
