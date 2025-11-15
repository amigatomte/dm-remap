# VS Code High CPU Usage - Troubleshooting Guide

## Problem
VS Code was consuming 100% CPU on the dm-remap workspace.

## Root Causes (Identified)

1. **IntelliSense/Language Server** - C/C++ extension running aggressive analysis on 3,145 files
2. **File Watcher** - Watching too many files including build artifacts (`.o`, `.ko` files)
3. **Multiple VS Code Instances** - Several processes running simultaneously

## Solutions Applied

### 1. Updated `.vscode/settings.json` ✅

Added configuration to:

```json
{
  "files.watcherExclude": {
    "**/.git/**": true,
    "**/node_modules/**": true,
    "**/build/**": true,
    "src/**/*.o": true,
    "src/**/*.ko": true,
    "**/Module.symvers": true,
    "**/modules.order": true
  },
  "C_Cpp.intelliSenseEngine": "disabled",
  "search.exclude": {
    "**/.git": true,
    "**/node_modules": true,
    "**/build": true,
    "src/**/*.o": true
  }
}
```

**What this does:**
- Disables file watching for build artifacts (`.o`, `.ko` files that change frequently)
- Disables C/C++ IntelliSense (can be re-enabled if needed, but causes high CPU on kernel code)
- Excludes build directories from search
- Excludes git internals from monitoring

### 2. Manual Fixes (If needed)

If CPU usage persists, try these:

#### **Reload VS Code Window**
```
Ctrl+Shift+P → "Developer: Reload Window"
```

#### **Disable Problematic Extensions**
```
Ctrl+Shift+X → Search for extension → Click "Disable"
```

Common culprits:
- C/C++ IntelliSense extensions
- GitHub Copilot (if indexing)
- Bracket pair colorizers
- Any custom language server extensions

#### **Clean Build Artifacts**
```bash
cd /home/christian/kernel_dev/dm-remap
make clean
```

This removes all `.o` and `.ko` files that might trigger file watching.

#### **Disable Language Server Temporarily**
In `.vscode/settings.json`:
```json
{
  "[c]": {
    "editor.defaultFormatter": null
  }
}
```

#### **Check for Runaway Processes**
```bash
# Kill specific VS Code process
kill -9 34704  # Replace with PID from your top output

# Or gracefully quit all VS Code instances
killall code
```

### 3. Prevention Going Forward

#### **Keep `.vscode/settings.json` Updated**

When you have local build artifacts:
```bash
# After building, clean up
make clean

# Or add to .gitignore to prevent tracking
echo "*.o" >> .gitignore
echo "*.ko" >> .gitignore
```

#### **Use Performance Monitor**
```bash
# Monitor CPU while working
watch -n 1 'top -bn1 | head -20'
```

#### **Limit File Indexing**
If you have many files in a subdirectory you don't edit:
```json
{
  "files.watcherExclude": {
    "archive/**": true,
    "docs/archive/**": true,
    "backup_*/**": true
  }
}
```

## Technical Details

### Why This Happens

1. **File Watcher Overhead**
   - VS Code watches 3,145 files for changes
   - Build artifacts (`.o`, `.ko`) are recreated frequently
   - Each recreation triggers file watch events
   - Events are analyzed by IntelliSense

2. **IntelliSense on Kernel Code**
   - Kernel headers are complex (includes for 3.5M+ lines)
   - C/C++ extension struggles with kernel macros
   - Indexing can take significant CPU

3. **Multiple Processes**
   - Main process: Language server
   - Node services: Language analysis
   - Zygote processes: Renderer processes
   - Each consumes resources

### Metrics Before/After

**Before:**
- 100% CPU (PID 34704)
- 46% CPU (PID 35262)
- 23% CPU (PID 12542)
- Total: 169% CPU usage

**After (Expected):**
- Idle: ~0-5% CPU
- Active editing: ~10-20% CPU
- Saving: ~5-10% CPU

## Verification

Check if the fix worked:
```bash
# Show current CPU usage
top -bn1 | grep code

# Should show much lower values (< 10% when idle)
```

## If Issues Persist

### Nuclear Option: Full Reset
```bash
# Close VS Code
killall code

# Remove settings cache
rm -rf ~/.config/Code/Cache/*

# Reopen VS Code
code /home/christian/kernel_dev/dm-remap
```

### Check for Stuck Processes
```bash
# Find any zombie or sleeping processes
ps aux | grep code | grep -v grep

# Kill all code processes
killall -9 code

# Restart
code /home/christian/kernel_dev/dm-remap &
```

## Recommended Settings Profile

For kernel module development, use:

```json
{
  // File watching
  "files.watcherExclude": {
    "**/.git/**": true,
    "**/node_modules/**": true,
    "**/build/**": true,
    "src/**/*.o": true,
    "src/**/*.ko": true",
    "**/Module.symvers": true,
    "**/modules.order": true,
    "**/.*": true
  },
  
  // Disable heavy analyzers for C code
  "C_Cpp.intelliSenseEngine": "disabled",
  
  // Search exclusions
  "search.exclude": {
    "**/.git": true,
    "**/build": true,
    "src/**/*.o": true
  },
  
  // Makefile support
  "[makefile]": {
    "editor.insertSpaces": false
  },
  
  // Format on save (disabled to avoid overhead)
  "editor.formatOnSave": false,
  
  // Git integration (disable if not needed)
  "git.enabled": false  // Optional: if you use git from terminal
}
```

## Future Considerations

### As Project Grows

If the project grows and CPU usage increases again:

1. **Split workspace** into separate VS Code windows
2. **Use folder exclusions** for archive directories
3. **Use remote development** (VS Code Remote SSH)
4. **Consider lightweight editor** for large files (vim, nano for quick edits)

### Scaling Recommendations

| Workspace Size | Recommended Action |
|---|---|
| < 10K files | Use full IntelliSense (can disable if slow) |
| 10K-50K files | Disable IntelliSense, use syntax highlighting only |
| > 50K files | Use Remote SSH or lightweight editor |

## Summary

✅ **Applied:** Optimized `.vscode/settings.json`  
✅ **Disabled:** C/C++ IntelliSense (causing high CPU)  
✅ **Excluded:** Build artifacts from file watching  
✅ **Result:** Expected CPU usage drop from 100% to < 5% (idle)

---

**Configuration updated:** November 15, 2025  
**Location:** `/home/christian/kernel_dev/dm-remap/.vscode/settings.json`
