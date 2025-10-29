# dm-remap Root Directory Structure

**Last Organized:** October 29, 2025

## Root Directory (Clean & Organized)

### Essential Project Files
- `README.md` - Project overview and quick start
- `LICENSE` - GPL-2.0 license
- `Makefile` - Build configuration

### Configuration
- `c_cpp_properties.json` - VS Code C/C++ configuration
- `.gitignore` - Git ignore rules

### Documentation (in root)
- `DOCUMENTATION_COMPLETE.md` - Documentation suite completion summary
- `SESSION_FINAL_SUMMARY.md` - Session completion and status
- `TESTING_COMPLETE.md` - Final testing status
- `OPTION_C_COMPLETE.md` - Option C validation complete

### Source Code Directories
- `src/` - Kernel module source code (~2,600 lines)
- `include/` - Header files
- `tests/` - Test suites and validation scripts
- `scripts/` - Utility scripts
- `demos/` - Interactive demonstrations
- `docs/` - Complete user and developer documentation

---

## docs/ Directory Structure

### User Documentation
- `docs/user/` - User guides and reference
  - `README.md` - Index
  - `QUICK_START.md` - 5-minute setup
  - `INSTALLATION.md` - Installation guide
  - `USER_GUIDE.md` - Complete reference
  - `MONITORING.md` - Observability guide
  - `FAQ.md` - Frequently asked questions
  - `API_REFERENCE.md` - Command reference
  - `STATISTICS_MONITORING.md` - Monitoring integration

### Developer Documentation
- `docs/development/` - Developer guides
  - `ARCHITECTURE.md` - System design and internals
  - (Other development docs)

### Archived Documentation
- `docs/archive/` - Old development notes and debug logs
  - `old_archive/` - Previous archive
  - `backups/` - Old backups
  - `backup_*` - Timestamped backups
  - `documentation/` - Old documentation
  - `v4_development/` - Old v4 development
  - `analysis/` - Old analysis files
  - `phase3/` - Phase 3 archive
  - `test-logs/` - Test execution logs
  - Plus 50+ debug and development notes

---

## Directory Summary

```
dm-remap/                           ← Clean root
├── README.md                        ← Start here
├── LICENSE
├── Makefile
├── c_cpp_properties.json
├── .gitignore
│
├── DOCUMENTATION_COMPLETE.md        ← Project status
├── SESSION_FINAL_SUMMARY.md
├── TESTING_COMPLETE.md
├── OPTION_C_COMPLETE.md
│
├── src/                             ← Source code
│   ├── dm-remap-v4-real-main.c
│   ├── (other modules)
│   └── Makefile
│
├── include/                         ← Header files
│
├── tests/                           ← Test scripts
│   ├── validate_hash_resize.sh
│   ├── runtime_verify.sh
│   ├── runtime_resize_test_fixed.sh
│   └── (other tests)
│
├── docs/                            ← Documentation
│   ├── README.md
│   ├── user/                        ← User guides (8 guides, 2,000+ lines)
│   │   ├── QUICK_START.md
│   │   ├── INSTALLATION.md
│   │   ├── USER_GUIDE.md
│   │   ├── MONITORING.md
│   │   ├── FAQ.md
│   │   ├── API_REFERENCE.md
│   │   └── (other guides)
│   │
│   ├── development/                 ← Developer docs
│   │   ├── ARCHITECTURE.md
│   │   └── (other dev docs)
│   │
│   ├── progress/                    ← Progress reports
│   ├── specifications/              ← Technical specs
│   │
│   └── archive/                     ← Old development docs
│       ├── *.md (50+ debug files)
│       ├── backups/
│       ├── v4_development/
│       ├── analysis/
│       ├── test-logs/
│       └── (other old docs)
│
├── scripts/                         ← Utility scripts
├── demos/                           ← Interactive demos
└── .git/                            ← Git repository
```

---

## Key Points

✅ **Root directory is clean** - Only essential files
✅ **Documentation centralized** - All in `docs/`
✅ **Old files archived** - Development notes in `docs/archive/`
✅ **Easy to navigate** - Clear structure
✅ **Production ready** - No clutter

---

## Navigation Guide

- **For users:** Start with `README.md` → `docs/user/QUICK_START.md`
- **For developers:** `docs/development/ARCHITECTURE.md`
- **For complete guides:** `docs/user/` directory
- **For historical info:** `docs/archive/` directory

---

**Status:** Root directory organized and clean ✅
