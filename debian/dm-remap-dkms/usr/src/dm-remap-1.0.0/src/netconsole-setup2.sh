sudo modprobe netconsole
sudo mkdir -p /sys/kernel/config/netconsole/target1
echo ens160 | sudo tee /sys/kernel/config/netconsole/target1/dev_name
echo 192.168.1.122 | sudo tee /sys/kernel/config/netconsole/target1/local_ip
echo 6665 | sudo tee /sys/kernel/config/netconsole/target1/local_port
echo 192.168.1.242 | sudo tee /sys/kernel/config/netconsole/target1/remote_ip
echo bc:fc:e7:08:6e:89 | sudo tee /sys/kernel/config/netconsole/target1/remote_mac
echo 6666 | sudo tee /sys/kernel/config/netconsole/target1/remote_port
echo 1 | sudo tee /sys/kernel/config/netconsole/target1/enabled
echo 'hello netconsole33' | sudo tee /dev/kmsg
