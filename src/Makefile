# Build dm-remap v2.0 module from multiple source files with sysfs interface
obj-m := dm_remap.o
dm_remap-y := dm_remap_main.o dm_remap_io.o dm_remap_error.o dm_remap_messages.o dm_remap_sysfs.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

