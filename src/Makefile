# Build dm-remap v4.0 module with Week 9-10 memory optimization support
obj-m := dm_remap.o
dm_remap-y := dm_remap_main.o dm_remap_io.o dm_remap_error.o dm_remap_messages.o dm_remap_sysfs.o dm_remap_production.o dm_remap_performance.o dm_remap_debug.o dm-remap-metadata.o dm-remap-io.o dm-remap-autosave.o dm-remap-recovery.o dm_remap_reservation.o dm-remap-health-scanner.o dm-remap-health-map.o dm-remap-health-sysfs.o dm-remap-health-predict.o dm-remap-memory-pool.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

