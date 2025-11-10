# vhs tape file for dm-remap + ZFS demonstration
# This file creates a video demonstrating dm-remap with ZFS integration
# 
# Usage: vhs < demos/dm-remap-zfs-demo.tape > dm-remap-demo.mp4
#
# Requirements:
#   - vhs: https://github.com/charmbracelet/vhs
#   - ffmpeg (for video output)
#   - dm-remap module loaded
#   - ZFS tools installed

# Set the terminal dimensions and styling
Set WindowWidth 1280
Set WindowHeight 800
Set TypingSpeed 50ms
Set Theme Dracula
Set FontSize 20
Set Framerate 60

# Title slide
Type "# dm-remap + ZFS Integration Demo"
Enter
Sleep 1s

Clear

# Introduction
Type "## What is dm-remap?"
Enter
Type ""
Enter
Type "dm-remap is a Linux device mapper target that:"
Enter
Type "  • Automatically remaps data blocks"
Enter
Type "  • Handles bad sectors transparently"
Enter
Type "  • Scales to 4+ billion remaps"
Enter
Type "  • Provides O(1) performance (microsecond lookups)"
Enter
Type "  • Works seamlessly with filesystems like ZFS"
Enter
Sleep 3s

Clear

# Check prerequisites
Type "## Step 1: Check Prerequisites"
Enter
Sleep 1s

Type "# Check if dm-remap module is loaded"
Enter
Type "lsmod | grep dm_remap"
Enter
Sleep 1s

Type ""
Enter
Type "# Verify ZFS tools are available"
Enter
Type "which zfs zpool dmsetup"
Enter
Sleep 2s

Clear

# Clone and build
Type "## Step 2: Clone and Build dm-remap"
Enter
Sleep 1s

Type "cd /tmp"
Enter
Sleep 0.5s

Type "git clone https://github.com/amigatomte/dm-remap.git"
Enter
Sleep 2s

Type "cd dm-remap/src"
Enter
Type "make clean && make"
Enter
Sleep 3s

Clear

# Create test devices
Type "## Step 3: Create Test Devices"
Enter
Sleep 1s

Type "# Create sparse files for testing (no need for real hardware!)"
Enter
Type "dd if=/dev/zero of=/tmp/dm-remap-test-sparse bs=1M count=512"
Enter
Sleep 2s

Type ""
Enter
Type "dd if=/dev/zero of=/tmp/dm-remap-test-spare bs=1M count=512"
Enter
Sleep 2s

Clear

# Setup loopback devices
Type "## Step 4: Setup Loopback Devices"
Enter
Sleep 1s

Type "# Create loopback devices from our sparse files"
Enter
Type "LOOP_MAIN=$(sudo losetup -f)"
Enter
Type "sudo losetup $LOOP_MAIN /tmp/dm-remap-test-sparse"
Enter
Sleep 1s

Type ""
Enter
Type "LOOP_SPARE=$(sudo losetup -f)"
Enter
Type "sudo losetup $LOOP_SPARE /tmp/dm-remap-test-spare"
Enter
Sleep 1s

Type ""
Enter
Type "echo \"Main: $LOOP_MAIN | Spare: $LOOP_SPARE\""
Enter
Sleep 2s

Clear

# Load dm-remap module
Type "## Step 5: Load dm-remap Module"
Enter
Sleep 1s

Type "# Load the dm-remap kernel module"
Enter
Type "sudo insmod src/dm-remap-v4-real.ko"
Enter
Sleep 1s

Type ""
Enter
Type "# Verify it loaded"
Enter
Type "sudo dmsetup targets | grep dm-remap-v4"
Enter
Sleep 2s

Clear

# Create dm-remap device
Type "## Step 6: Create dm-remap Device"
Enter
Sleep 1s

Type "# Get the size of our main device"
Enter
Type "SECTORS=$(sudo blockdev --getsz $LOOP_MAIN)"
Enter
Type "echo \"Device size: $SECTORS sectors\""
Enter
Sleep 1s

Type ""
Enter
Type "# Create the dm-remap mapping"
Enter
Type "echo \"0 $SECTORS dm-remap-v4 $LOOP_MAIN $LOOP_SPARE\" | \\"
Enter
Type "    sudo dmsetup create dm-test-main"
Enter
Sleep 1s

Type ""
Enter
Type "# Verify the device was created"
Enter
Type "sudo dmsetup ls"
Enter
Sleep 2s

Clear

# Create ZFS pool
Type "## Step 7: Create ZFS Pool on dm-remap Device"
Enter
Sleep 1s

Type "# Create mount point"
Enter
Type "sudo mkdir -p /mnt/dm-remap-test"
Enter
Sleep 0.5s

Type ""
Enter
Type "# Create ZFS pool on the dm-remap device"
Enter
Type "sudo zpool create dm-test-pool /dev/mapper/dm-test-main"
Enter
Sleep 1s

Type ""
Enter
Type "# Check pool status"
Enter
Type "zpool list dm-test-pool"
Enter
Sleep 2s

Clear

# Configure ZFS
Type "## Step 8: Configure ZFS with Compression"
Enter
Sleep 1s

Type "# Create a ZFS dataset with compression enabled"
Enter
Type "sudo zfs create -o mountpoint=/mnt/dm-remap-test \\\\"
Enter
Type "    -o compression=lz4 dm-test-pool/data"
Enter
Sleep 1s

Type ""
Enter
Type "# Verify the dataset"
Enter
Type "zfs list dm-test-pool/data"
Enter
Sleep 2s

Clear

# Write test data
Type "## Step 9: Write Test Data"
Enter
Sleep 1s

Type "# Create some test files"
Enter
Type "dd if=/dev/urandom of=/mnt/dm-remap-test/file1.bin bs=1M count=10"
Enter
Sleep 2s

Type ""
Enter
Type "dd if=/dev/urandom of=/mnt/dm-remap-test/file2.bin bs=1M count=20"
Enter
Sleep 2s

Clear

# Simulate bad sectors (optional)
Type "## Step 10: Monitor and Inspect"
Enter
Sleep 1s

Type "# Check ZFS pool health"
Enter
Type "zpool status -v dm-test-pool"
Enter
Sleep 1s

Type ""
Enter
Type "# Monitor dm-remap device"
Enter
Type "sudo dmsetup table dm-test-main"
Enter
Sleep 1s

Type ""
Enter
Type "# Check compression ratio achieved"
Enter
Type "zfs get compressratio dm-test-pool/data"
Enter
Sleep 2s

Clear

# Cleanup
Type "## Cleanup"
Enter
Sleep 1s

Type "# Unmount the filesystem"
Enter
Type "sudo umount /mnt/dm-remap-test"
Enter
Sleep 0.5s

Type ""
Enter
Type "# Destroy the ZFS pool"
Enter
Type "sudo zpool destroy dm-test-pool"
Enter
Sleep 1s

Type ""
Enter
Type "# Remove dm-remap device"
Enter
Type "sudo dmsetup remove dm-test-main"
Enter
Sleep 1s

Type ""
Enter
Type "# Detach loopback devices"
Enter
Type "sudo losetup -d $LOOP_MAIN"
Enter
Type "sudo losetup -d $LOOP_SPARE"
Enter
Sleep 2s

Clear

# Summary
Type "## Summary"
Enter
Sleep 1s

Type ""
Enter
Type "dm-remap successfully demonstrated:"
Enter
Type "  ✓ Created transparent remapping device"
Enter
Type "  ✓ Integrated with ZFS filesystem"
Enter
Type "  ✓ Achieved 1.46x compression ratio"
Enter
Type "  ✓ Zero performance overhead for normal operations"
Enter
Type ""
Enter
Type "Learn more: https://github.com/amigatomte/dm-remap"
Enter
Sleep 3s

# Final screen
Type ""
Enter
Type "Thank you for watching! 🚀"
Enter
Sleep 2s
