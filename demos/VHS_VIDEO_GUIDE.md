# Creating Video Demonstrations with vhs

This guide explains how to create terminal recording videos from the provided `.tape` files using [vhs](https://github.com/charmbracelet/vhs).

## What is vhs?

vhs is a tool by Charm Bracelet that records terminal sessions and converts them into videos. It uses a simple scripting language (`.tape` files) to automate terminal interactions.

**Official Repository**: https://github.com/charmbracelet/vhs

## Installation

### macOS

```bash
brew install charmbracelet/tap/vhs
```

### Linux (Ubuntu/Debian)

```bash
# Install vhs via GitHub releases or build from source
wget https://github.com/charmbracelet/vhs/releases/download/v0.7.2/vhs_0.7.2_Linux_x86_64.tar.gz
tar xzf vhs_0.7.2_Linux_x86_64.tar.gz
sudo mv vhs /usr/local/bin/
```

### Build from Source

```bash
git clone https://github.com/charmbracelet/vhs.git
cd vhs
go build
sudo mv vhs /usr/local/bin/
```

### Verify Installation

```bash
vhs --version
```

## Video Output Requirements

For MP4 output, you need ffmpeg:

```bash
# Ubuntu/Debian
sudo apt-get install ffmpeg

# macOS
brew install ffmpeg

# Verify
ffmpeg -version
```

## Available Tape Files

### 1. dm-remap-zfs-demo.tape

A comprehensive walkthrough of dm-remap + ZFS integration.

**What it demonstrates**:
- Prerequisites checking
- Repository cloning and building
- Device creation (sparse files)
- Loopback device setup
- dm-remap kernel module loading
- Device creation and configuration
- ZFS pool creation
- ZFS dataset configuration with compression
- Test data writing
- Monitoring and inspection
- Cleanup procedures

**Duration**: ~5-7 minutes of terminal recording

**Generate video**:
```bash
cd /home/christian/kernel_dev/dm-remap
vhs < demos/dm-remap-zfs-demo.tape > dm-remap-zfs-demo.mp4
```

**Output**: `dm-remap-zfs-demo.mp4` (suitable for YouTube, documentation, etc.)

### 2. dm-remap-test-execution.tape

The complete integration test suite execution showing actual test output.

**What it demonstrates**:
- Full 10-step test sequence
- Title and introduction
- Step-by-step test execution with timestamps
- Device creation and configuration
- ZFS pool status
- Test file creation and verification
- Bad sector injection
- ZFS scrub operation
- Comprehensive filesystem tests
- Test report generation
- Before/after cleanup pause points
- Final summary and results

**Duration**: ~4-5 minutes of terminal recording

**Generate video**:
```bash
cd /home/christian/kernel_dev/dm-remap
vhs < demos/dm-remap-test-execution.tape > dm-remap-test-execution.mp4
```

**Output**: `dm-remap-test-execution.mp4` (realistic test demonstration)

## Step-by-Step: Creating a Video

### Example: Generate the ZFS Demo Video

1. **Navigate to the project directory**:
   ```bash
   cd /home/christian/kernel_dev/dm-remap
   ```

2. **Generate the video** (takes ~5-10 minutes):
   ```bash
   vhs < demos/dm-remap-zfs-demo.tape > dm-remap-zfs-demo.mp4
   ```

3. **Check the output**:
   ```bash
   ls -lh dm-remap-zfs-demo.mp4
   file dm-remap-zfs-demo.mp4
   ```

4. **Preview the video**:
   ```bash
   # Using mpv (if available)
   mpv dm-remap-zfs-demo.mp4
   
   # Or any video player
   vlc dm-remap-zfs-demo.mp4
   firefox dm-remap-zfs-demo.mp4
   ```

### Example: Generate Both Videos

```bash
cd /home/christian/kernel_dev/dm-remap

echo "Generating ZFS demo..."
vhs < demos/dm-remap-zfs-demo.tape > dm-remap-zfs-demo.mp4

echo "Generating test execution demo..."
vhs < demos/dm-remap-test-execution.tape > dm-remap-test-execution.mp4

echo "Done! Videos are ready."
ls -lh dm-remap-*.mp4
```

## Video Customization

### Changing Typing Speed

Edit the `.tape` file and modify the `TypingSpeed` parameter:

```bash
# Fast typing (default: 50ms)
Set TypingSpeed 30ms

# Slow typing
Set TypingSpeed 100ms
```

### Changing Theme

Options: `Dracula`, `Monokai`, `Nord`, `Gruvbox`, `Catppuccin`, etc.

```bash
Set Theme Catppuccin
```

### Changing Resolution

```bash
Set WindowWidth 1920
Set WindowHeight 1080
```

### Changing Font Size

```bash
Set FontSize 24
```

### Adjusting Framerate

```bash
Set Framerate 60    # Smooth video
Set Framerate 30    # Smaller file size
```

## Complete Tape File Reference

### Basic Commands

- `Type "text"` - Type text (characters will appear as if typed)
- `Enter` - Press Enter key
- `Sleep 1s` - Pause for 1 second (supports ms, s, m)
- `Clear` - Clear the screen
- `Run "command"` - Execute a shell command

### Settings

- `Set WindowWidth 1280`
- `Set WindowHeight 800`
- `Set TypingSpeed 50ms`
- `Set Theme Dracula`
- `Set FontSize 20`
- `Set Framerate 60`

### Example Tape File Structure

```
# Comment
Set WindowWidth 1280
Set WindowHeight 800
Set TypingSpeed 50ms
Set Theme Dracula

Clear

Type "# My Demo"
Enter
Sleep 1s

Type "echo 'Hello, World!'"
Enter
Sleep 2s
```

## Troubleshooting

### "vhs: command not found"

Make sure vhs is installed and in your PATH:
```bash
which vhs
```

If not found, install it or add to PATH.

### "ffmpeg: command not found"

Install ffmpeg:
```bash
# Ubuntu/Debian
sudo apt-get install ffmpeg

# macOS
brew install ffmpeg
```

### Video playback issues

- Ensure ffmpeg is properly installed
- Check disk space for video files
- Try a different video player

### Recording hangs

The recording will timeout after 30 minutes by default. Tape files should not exceed this duration.

## Using Generated Videos

### Documentation

Embed in README or documentation:
```markdown
![dm-remap Demo](demos/dm-remap-zfs-demo.mp4)
```

### Presentations

- Include in PowerPoint/Google Slides
- Use in recorded presentations
- Share in team communications

### Social Media

- YouTube: Upload and share
- Twitter: Convert to GIF for previews
- LinkedIn: Share as demonstration

### CI/CD Integration

Generate videos automatically during release:
```bash
#!/bin/bash
vhs < demos/dm-remap-zfs-demo.tape > artifacts/demo-$(date +%Y%m%d).mp4
```

## Performance Tips

- Smaller window sizes render faster
- Lower framerate (30 fps) reduces rendering time
- Type slower (higher ms values) gives viewers time to read
- Use `Sleep` strategically between sections

## Creating Custom Demonstrations

### Template

```
Set WindowWidth 1280
Set WindowHeight 800
Set TypingSpeed 50ms
Set Theme Dracula
Set FontSize 20
Set Framerate 60

Clear

# Title
Type "# Your Demo Title"
Enter
Sleep 2s

Clear

# Section 1
Type "## Section 1"
Enter
Type "Description of what you're showing"
Enter
Sleep 2s

# Commands
Type "your command"
Enter
Sleep 1s

Clear

# Conclusion
Type "✓ Demo complete!"
Enter
Sleep 2s
```

## Advanced: Combining with Actual Commands

The tape files in this project simulate output for demonstration purposes. For actual command execution videos:

1. Run commands in real time (use `Run` command)
2. Or pre-record terminal sessions and replay

```bash
# Real command execution in tape file
Run "ls -la"

# Or type and execute
Type "bash tests/dm_remap_zfs_integration_test.sh"
Enter
```

## Batch Processing

Generate all videos:

```bash
#!/bin/bash
cd /home/christian/kernel_dev/dm-remap

for tape_file in demos/*.tape; do
    basename="${tape_file##*/}"
    name="${basename%.tape}"
    echo "Generating $name..."
    vhs < "$tape_file" > "${name}.mp4"
done

echo "All videos generated!"
ls -lh *.mp4
```

## Additional Resources

- **vhs Documentation**: https://github.com/charmbracelet/vhs
- **Charm Bracelet**: https://charm.sh/
- **Terminal UI Best Practices**: https://charm.sh/

---

**Ready to create amazing terminal demos?** 🎬

For more information, see the [`demos/README.md`](README.md).
