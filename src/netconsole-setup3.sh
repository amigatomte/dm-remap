#!/usr/bin/env bash
# Configure netconsole via configfs (works with CONFIG_NETCONSOLE_DYNAMIC=y)

set -e

# Local and remote settings
LOCAL_IF="ens160"
LOCAL_IP="192.168.1.122"
LOCAL_PORT=6665
REMOTE_IP="192.168.1.242"
REMOTE_MAC="bc:fc:e7:08:6e:89"
REMOTE_PORT=6666

TARGET_DIR="/sys/kernel/config/netconsole/target1"

echo "🧹 Cleaning up any old target..."
if [ -d "$TARGET_DIR" ]; then
    echo 0 | sudo tee "$TARGET_DIR/enabled" >/dev/null || true
    sudo rmdir "$TARGET_DIR" || true
fi

echo "📦 Loading netconsole module..."
sudo modprobe netconsole

echo "📂 Creating configfs target..."
sudo mkdir -p "$TARGET_DIR"

echo "🔧 Applying settings..."
echo $LOCAL_IF   | sudo tee "$TARGET_DIR/dev_name"
echo $LOCAL_IP   | sudo tee "$TARGET_DIR/local_ip"
echo $LOCAL_PORT | sudo tee "$TARGET_DIR/local_port"
echo $REMOTE_IP  | sudo tee "$TARGET_DIR/remote_ip"
echo $REMOTE_MAC | sudo tee "$TARGET_DIR/remote_mac"
echo $REMOTE_PORT| sudo tee "$TARGET_DIR/remote_port"

echo "✅ Enabling netconsole..."
echo 1 | sudo tee "$TARGET_DIR/enabled"

echo "📣 Sending test message..."
echo "hello via configfs setup" | sudo tee /dev/kmsg

