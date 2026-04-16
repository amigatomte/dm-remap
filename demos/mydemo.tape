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

# Terminal configuration
Output mydemo.mp4
Set Shell bash
Set FontSize 16
Set Width 1280
Set Height 800
Set TypingSpeed 50ms
Set PlaybackSpeed 1.5
Set Theme Dracula
Set WindowBar Colorful

# Title
Type "# dm-remap Demo"
Enter
Sleep 2s
Enter
Enter

Type "# Clone and build"
Enter
Type "git clone https://github.com/amigatomte/dm-remap.git"
Sleep 1s
Enter
Wait
Type "cd dm-remap"
Enter
Type "make && make dkms-deb >/dev/null"
Sleep 1s
Enter
Wait@30s

Type "sudo apt install ../dm-remap-dkms*.deb"
Sleep 1s
Enter
Wait@50s

Type "# Create test device with dm-remap (completely automated)"
Enter
Type "sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --no-bad-sectors -v"
Sleep 1s
Enter
Wait

Type "# Check device status with dmremap-status tool"
Sleep 1s
Enter
Type "tools/dmremap-status/dmremap-status --device dm-remap-main"
Sleep 1s
Enter
Wait

Type "# Cleanup..."
Enter
Type "sudo tools/dm-remap-loopback-setup/setup-dm-remap-test.sh --cleanup"
Sleep 1s
Enter
Wait

# Final message
Type "Demo complete! 🚀"
Enter
Sleep 2s
