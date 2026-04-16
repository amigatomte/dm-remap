# VHS Demos - dm-remap Video Demonstrations

This directory contains VHS tape files and generated videos that demonstrate dm-remap functionality.

## What is VHS?

[VHS](https://github.com/charmbracelet/vhs) is a tool for recording terminal sessions and generating videos (MP4, WebM, GIF).

## Available Videos

### dm-remap + ZFS Integration Demo

**File**: `dm-remap-zfs-demo.tape` / `dm-remap-zfs-demo.mp4`

This video demonstrates:
- Loading the dm-remap kernel module
- Creating test loop devices
- Setting up a dm-remap device for transparent block remapping
- Creating a ZFS pool on top of the dm-remap device
- Writing and reading data through the stack
- Monitoring device status and compression

**Duration**: ~2 minutes
**Resolution**: 1280x800 @ 60fps
**Size**: 3.0 MB

## Generating Videos

### Prerequisites

```bash
# Install vhs
go install github.com/charmbracelet/vhs@latest

# Install ttyd (for terminal recording)
# The script will look for ttyd in PATH

# For video output, you need ffmpeg
sudo apt-get install ffmpeg
```

### Generate a Video from Tape File

```bash
cd /home/christian/kernel_dev/dm-remap/demos

# Generate MP4 video
vhs < dm-remap-zfs-demo.tape > dm-remap-zfs-demo.mp4

# Generate WebM video
vhs < dm-remap-zfs-demo.tape > dm-remap-zfs-demo.webm

# Generate GIF
vhs < dm-remap-zfs-demo.tape > dm-remap-zfs-demo.gif
```

### Video Settings (Tape File Format)

The tape file controls video generation with these settings:

```vhs
Output dm-remap-zfs-demo.mp4    # Output file
Set Shell bash                   # Shell to use
Set FontSize 16                  # Terminal font size
Set Width 1280                   # Terminal width (pixels)
Set Height 800                   # Terminal height (pixels)
Set TypingSpeed 50ms             # Typing speed
Set PlaybackSpeed 1.5            # Playback speed (1.0 = real-time)
Set Theme Dracula                # Color theme
```

## Tape File Format

VHS tape files are text files with commands and timing:

### Basic Commands

```vhs
# Comments start with #

# Type text (without executing)
Type "command here"

# Send Enter key
Enter

# Pause for duration
Sleep 2s

# Set options
Set FontSize 20
```

### Example Tape File

```vhs
Output demo.mp4
Set Shell bash
Set FontSize 16
Set Width 1280
Set Height 800

# Type a command
Type "echo Hello, dm-remap!"
Enter
Sleep 2s

# Type another command
Type "ls -la"
Enter
Sleep 1s
```

## Creating Your Own Demo

### Step 1: Create a Tape File

```bash
cat > my-demo.tape << 'EOF'
Output my-demo.mp4
Set Shell bash
Set FontSize 16
Set Width 1280
Set Height 800
Set TypingSpeed 50ms
Set PlaybackSpeed 1.5
Set Theme Dracula

# Your commands here
Type "echo dm-remap demo"
Enter
Sleep 2s
EOF
```

### Step 2: Generate the Video

```bash
vhs < my-demo.tape
```

### Step 3: View the Result

```bash
# View the video
mpv my-demo.mp4

# Or with ffplay
ffplay my-demo.mp4
```

## Available Themes

VHS supports several color themes:

- Dracula
- Gruvbox
- Nord
- One Half Dark
- Solarized Dark
- Solarized Light
- Monoki
- GitHub Light
- GitHub Dark
- Catppuccin Frappe
- Catppuccin Mocha
- Catppuccin Latte

Use `Set Theme ThemeName` in your tape file.

## Customizing Videos

### Adjust Playback Speed

- `Set PlaybackSpeed 0.5` - Half speed (slower, better for learning)
- `Set PlaybackSpeed 1.0` - Real-time
- `Set PlaybackSpeed 2.0` - 2x speed (faster)

### Adjust Terminal Size

```vhs
Set Width 1920      # Full HD width
Set Height 1080     # Full HD height
Set FontSize 20     # Larger font
```

### Add Pauses Between Commands

```vhs
Type "command1"
Enter
Sleep 3s           # 3 second pause to read output

Type "command2"
Enter
Sleep 2s
```

## Sharing Videos

### Upload to vhs.charm.sh

```bash
vhs publish dm-remap-zfs-demo.mp4
```

This generates a shareable link to your video.

### Convert to Other Formats

```bash
# MP4 to WebM (better for web)
ffmpeg -i demo.mp4 -c:v libvpx-vp9 -crf 30 demo.webm

# MP4 to GIF
ffmpeg -i demo.mp4 -vf "fps=10,scale=1280:-1:flags=lanczos" demo.gif

# MP4 to animated PNG
ffmpeg -i demo.mp4 -plays 0 demo.apng
```

## Tips for Great Demos

1. **Keep it short** - 2-3 minutes is ideal for tutorials
2. **Use comments** - Add `Type "#"` before sections to explain what's happening
3. **Add pauses** - Use `Sleep` to let viewers read output
4. **Speed up repetitive parts** - Adjust `PlaybackSpeed` for different sections
5. **Use clear commands** - Make commands easy to read and understand
6. **Show results** - Always include output verification

## Troubleshooting

### vhs: command not found

```bash
# Add Go bin directory to PATH
export PATH="$PATH:$(go env GOPATH)/bin"

# Or install globally
sudo go install github.com/charmbracelet/vhs@latest
```

### ttyd not found

The script uses ttyd for terminal recording. Make sure it's available:

```bash
# Option 1: Add to PATH
export PATH="/path/to/ttyd:$PATH"

# Option 2: Create a symlink
ln -s /path/to/ttyd.x86_64 /usr/local/bin/ttyd

# Option 3: Install from source
git clone https://github.com/tsl0922/ttyd.git
cd ttyd && make install
```

### Video output is blank

- Check that the tape file has valid syntax
- Ensure terminal is at least 80x24
- Try increasing `Sleep` durations
- Verify all commands work in your shell

### Performance issues

- Reduce terminal size (`Set Width` / `Set Height`)
- Lower playback speed (`Set PlaybackSpeed`)
- Use fewer colors in output
- Reduce font size

## Integration with CI/CD

Generate demos automatically in your CI pipeline:

```yaml
# Example GitHub Actions workflow
- name: Generate demo videos
  run: |
    cd demos
    vhs < dm-remap-zfs-demo.tape
    
- name: Upload artifacts
  uses: actions/upload-artifact@v2
  with:
    name: demo-videos
    path: demos/*.mp4
```

## Reference

- **VHS Documentation**: https://github.com/charmbracelet/vhs
- **VHS Charm Blog**: https://charm.sh/blog/vhs/

## Resources

- [dm-remap GitHub](https://github.com/amigatomte/dm-remap)
- [Device Mapper Documentation](https://github.com/lvmeteam/lvm2/blob/master/device-mapper/)
- [ZFS on Linux](https://github.com/openzfs/zfs)
