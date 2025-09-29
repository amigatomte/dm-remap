# Build dm-remap module from multiple source files
dm-remap-objs := dm-remap-main.o dm-remap-io.o dm-remap-messages.o
obj-m := dm-remap.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

