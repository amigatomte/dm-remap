# Demonstrations Directory Index

This directory contains all demonstration materials for dm-remap.

## Quick Navigation

### 🎬 Video Demonstrations (vhs Terminal Recordings)

Generate professional terminal recording videos:

- **[dm-remap-zfs-demo.tape](dm-remap-zfs-demo.tape)** - Step-by-step ZFS integration walkthrough
  - Duration: 5-7 minutes
  - Best for: Tutorials, walkthroughs, documentation

- **[dm-remap-test-execution.tape](dm-remap-test-execution.tape)** - Full integration test execution
  - Duration: 4-5 minutes
  - Best for: Quality assurance, product showcase, demonstrations

### 📚 Video Generation Guides

Learn how to generate videos from tape files:

- **[VHS_QUICK_START.md](VHS_QUICK_START.md)** - 3-minute quick start guide
  - Installation commands
  - Single video generation
  - Quick troubleshooting

- **[VHS_VIDEO_GUIDE.md](VHS_VIDEO_GUIDE.md)** - Comprehensive video generation guide
  - Detailed setup instructions
  - Tape file customization
  - Advanced options
  - Batch processing
  - CI/CD integration

- **[TAPE_FILES_SUMMARY.md](TAPE_FILES_SUMMARY.md)** - Complete tape files overview
  - What was created and why
  - Use cases and examples
  - Technical details
  - Integration guide

### 🎯 Interactive Script Demonstrations

Run interactive demonstrations directly:

- **[v4_interactive_demo.sh](v4_interactive_demo.sh)** - 60-second interactive demo
  ```bash
  sudo ./demos/v4_interactive_demo.sh
  ```
  Features:
  - Environment setup
  - Module loading
  - Device creation
  - I/O operations
  - Performance testing
  - Module information

### 📖 Other Resources

- **[README.md](README.md)** - Overview of all demonstrations
- **[../docs/user/QUICK_START.md](../docs/user/QUICK_START.md)** - User quick start guide
- **[../TOOLS.md](../TOOLS.md)** - Tools documentation

## Getting Started

### Option 1: Watch Video Demonstrations (Easiest)

Generate a video in 2 commands:

```bash
# Install vhs (one-time)
brew install charmbracelet/tap/vhs

# Generate video
cd /home/christian/kernel_dev/dm-remap
vhs < demos/dm-remap-zfs-demo.tape > demo.mp4

# Watch
mpv demo.mp4
```

See [VHS_QUICK_START.md](VHS_QUICK_START.md) for details.

### Option 2: Run Interactive Demo (Fastest)

Try the live demonstration:

```bash
cd /home/christian/kernel_dev/dm-remap
sudo ./demos/v4_interactive_demo.sh
```

Takes about 60 seconds and shows real dm-remap in action.

### Option 3: Follow User Guide (Most Detailed)

Learn step-by-step with the comprehensive guide:

```
Read: ../docs/user/QUICK_START.md
```

## File Structure

```
demos/
├── INDEX.md                          ← You are here
├── README.md                         Overview of demos
├── v4_interactive_demo.sh            Interactive shell script
│
├── Video Tape Files:
├── dm-remap-zfs-demo.tape            Step-by-step walkthrough
├── dm-remap-test-execution.tape      Full test execution
│
├── Video Generation Guides:
├── VHS_QUICK_START.md                Quick reference (3 min read)
├── VHS_VIDEO_GUIDE.md                Comprehensive guide (15 min read)
└── TAPE_FILES_SUMMARY.md             Technical summary (10 min read)
```

## Common Tasks

### Generate a Video

```bash
vhs < demos/dm-remap-zfs-demo.tape > my-demo.mp4
```

See: [VHS_QUICK_START.md](VHS_QUICK_START.md#generate-videos)

### Generate Both Videos

```bash
vhs < demos/dm-remap-zfs-demo.tape > demo1.mp4 &
vhs < demos/dm-remap-test-execution.tape > demo2.mp4 &
wait
```

### Customize Video

Edit the `.tape` file and change:
- `TypingSpeed` - How fast text appears
- `Theme` - Color scheme
- `WindowWidth` / `WindowHeight` - Video dimensions
- `FontSize` - Text size

See: [VHS_VIDEO_GUIDE.md](VHS_VIDEO_GUIDE.md#video-customization)

### Embed in Documentation

```markdown
## Demo Video

![dm-remap demonstration](demos/dm-remap-zfs-demo.mp4)
```

### Share Video

1. Generate: `vhs < demos/dm-remap-zfs-demo.tape > demo.mp4`
2. Upload to YouTube/Vimeo
3. Share link in documentation, presentations, social media

## Troubleshooting

### vhs not found?
See: [VHS_VIDEO_GUIDE.md#troubleshooting](VHS_VIDEO_GUIDE.md#troubleshooting)

### Video won't play?
Check: [VHS_VIDEO_GUIDE.md#troubleshooting](VHS_VIDEO_GUIDE.md#troubleshooting)

### How to customize videos?
Read: [VHS_VIDEO_GUIDE.md#video-customization](VHS_VIDEO_GUIDE.md#video-customization)

## Project Status

✅ Interactive demo (v4_interactive_demo.sh) - Production ready
✅ Tape files - Complete and tested
✅ Documentation - Comprehensive

## Next Steps

1. **Install vhs** - Follow [VHS_QUICK_START.md](VHS_QUICK_START.md)
2. **Generate a video** - Run vhs command
3. **Watch video** - Opens in your player
4. **Share** - Upload or embed wherever you need

## Resources

- **vhs**: https://github.com/charmbracelet/vhs
- **dm-remap**: https://github.com/amigatomte/dm-remap
- **Charm Bracelet**: https://charm.sh/

---

**Ready to create amazing demonstrations?** 🚀

Start with [VHS_QUICK_START.md](VHS_QUICK_START.md) or [v4_interactive_demo.sh](v4_interactive_demo.sh)!
