# Quick Reference: Using vhs Tape Files

Quick commands to generate demonstration videos from the dm-remap project.

## Installation (One-time)

```bash
# Install vhs
brew install charmbracelet/tap/vhs    # macOS
# or
wget https://github.com/charmbracelet/vhs/releases/download/v0.7.2/vhs_0.7.2_Linux_x86_64.tar.gz
tar xzf vhs_0.7.2_Linux_x86_64.tar.gz && sudo mv vhs /usr/local/bin/

# Install ffmpeg
sudo apt-get install ffmpeg    # Ubuntu/Debian
brew install ffmpeg            # macOS
```

## Generate Videos

### Generate One Video

```bash
cd /home/christian/kernel_dev/dm-remap

# ZFS Integration Demo
vhs < demos/dm-remap-zfs-demo.tape > dm-remap-zfs-demo.mp4

# OR: Full Test Execution
vhs < demos/dm-remap-test-execution.tape > dm-remap-test-execution.mp4
```

### Generate Both Videos

```bash
cd /home/christian/kernel_dev/dm-remap

echo "Generating demos..."
vhs < demos/dm-remap-zfs-demo.tape > dm-remap-zfs-demo.mp4 &
vhs < demos/dm-remap-test-execution.tape > dm-remap-test-execution.mp4 &
wait

echo "✓ Done! Videos ready in current directory."
ls -lh dm-remap-*.mp4
```

## Tape Files

| File | Description | Duration |
|------|-------------|----------|
| `dm-remap-zfs-demo.tape` | Step-by-step ZFS integration walkthrough | ~5-7 min |
| `dm-remap-test-execution.tape` | Complete integration test execution | ~4-5 min |

## Preview Video

```bash
mpv dm-remap-zfs-demo.mp4           # mpv player
vlc dm-remap-test-execution.mp4     # VLC player
firefox dm-remap-*.mp4               # Firefox
```

## Common Customizations

Edit the `.tape` file to modify:

```bash
# Typing speed (faster = less time)
Set TypingSpeed 30ms        # Fast
Set TypingSpeed 50ms        # Default
Set TypingSpeed 100ms       # Slow

# Theme
Set Theme Dracula           # Default
Set Theme Nord              # Alternative
Set Theme Monokai           # Modern
Set Theme Catppuccin        # Warm

# Video size
Set WindowWidth 1920        # HD
Set WindowHeight 1080       # HD
Set FontSize 24             # Larger

# Performance
Set Framerate 30            # Smaller files
Set Framerate 60            # Smoother video
```

## Use Cases

- **Documentation**: Embed in README.md or wiki
- **Tutorials**: Share step-by-step guides
- **Marketing**: Create promotional content
- **Testing**: Document test procedures
- **Training**: Teach users how to use dm-remap

## Batch Script

```bash
#!/bin/bash
# generate-demos.sh

cd /home/christian/kernel_dev/dm-remap

echo "📹 Generating dm-remap demonstration videos..."

for tape in demos/*.tape; do
    name=$(basename "$tape" .tape)
    echo "  Generating $name.mp4..."
    vhs < "$tape" > "${name}.mp4"
    echo "  ✓ $name.mp4 complete"
done

echo ""
echo "✓ All videos generated successfully!"
echo ""
echo "Videos created:"
ls -lh *.mp4 | awk '{print "  " $NF " (" $5 ")"}'
```

Save as `generate-demos.sh` and run:
```bash
chmod +x generate-demos.sh
./generate-demos.sh
```

## Troubleshooting

**vhs not found**: Install or add to PATH
```bash
which vhs
export PATH="$PATH:$(go env GOPATH)/bin"
```

**ffmpeg not found**: Install ffmpeg package
```bash
sudo apt-get install ffmpeg  # Ubuntu/Debian
```

**Recording is slow**: Reduce resolution or framerate
```bash
Set WindowWidth 1280        # Smaller
Set Framerate 30            # Lower
```

**Video won't play**: Check format and try different player
```bash
file dm-remap-*.mp4
ffprobe dm-remap-zfs-demo.mp4
```

## Documentation

For detailed information:
- **VHS_VIDEO_GUIDE.md** - Comprehensive video generation guide
- **demos/README.md** - Overview of all demonstrations
- **vhs repository** - https://github.com/charmbracelet/vhs

---

**Need help?** See `demos/VHS_VIDEO_GUIDE.md` for complete documentation.
