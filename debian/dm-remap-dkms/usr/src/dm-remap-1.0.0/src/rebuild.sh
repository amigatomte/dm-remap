make clean &&make
sudo dmsetup remove test-remap
sudo rmmod dm_remap
sudo insmod dm-remap.ko
echo "0 2097152 remap /dev/loop50 /dev/loop51 0 204800" | sudo dmsetup create test-remap
