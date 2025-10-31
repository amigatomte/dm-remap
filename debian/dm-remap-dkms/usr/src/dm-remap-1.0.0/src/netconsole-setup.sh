#!/usr/bin/env bash
echo 0 | sudo tee /sys/kernel/config/netconsole/target1/enabled
sudo rmdir /sys/kernel/config/netconsole/target1
sudo modprobe -r netconsole
sudo dmesg -n 8
sudo ip neigh replace 192.168.1.242 lladdr bc:fc:e7:08:6e:89 dev ens160 nud permanent
sudo modprobe netconsole netconsole=@192.168.1.122/ens160,6665@192.168.1.242/bc:fc:e7:08:6e:89,6666
echo "hello via modprobe" | sudo tee /dev/kmsg

