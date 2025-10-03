# Build dm-remap v3.0 module with metadata persistence support
obj-m := dm_remap.o
dm_remap-y := dm_remap_main.o dm_remap_io.o dm_remap_error.o dm_remap_messages.o dm_remap_sysfs.o dm_remap_production.o dm_remap_performance.o dm_remap_debug.o dm-remap-metadata.o dm-remap-io.o dm-remap-autosave.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

