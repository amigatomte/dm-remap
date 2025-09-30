# Build dm-remap v2.0 module from multiple source files with sysfs interface
dm_remap-objs := dm-remap.o dm-remap-io.o dm-remap-error.o dm-remap-messages.o dm-remap-sysfs.o
obj-m := dm_remap.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

