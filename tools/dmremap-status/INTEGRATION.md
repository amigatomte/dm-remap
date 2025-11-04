# Userspace Tool Integration Guide

## Overview

The `dmremap-status` tool is a userspace utility for formatting and interpreting dm-remap kernel module status output. It has been designed to be:

- **Independent**: Works standalone without modifying the kernel module
- **Lightweight**: Minimal dependencies (just standard C)
- **Extensible**: Easy to add new output formats
- **Non-intrusive**: Read-only access to kernel data

## Architecture

```
┌─────────────────────────────────────────────────────┐
│ Kernel Module (dm-remap-v4)                         │
│ ───────────────────────────────────────────────────│
│ Collects statistics and status                     │
│ Outputs 31 fields via dmsetup status              │
└───────────────┬─────────────────────────────────────┘
                │ dmsetup status <device>
                │ Raw 31-field output
                ↓
┌─────────────────────────────────────────────────────┐
│ dmremap-status Userspace Tool                       │
│ ───────────────────────────────────────────────────│
│ ├─ parser.c      → Parse 31-field format          │
│ ├─ formatter.c   → Human-readable output          │
│ ├─ json.c        → JSON/CSV output                │
│ └─ main          → CLI, watch mode, file I/O      │
└────────┬──────────────────────┬──────────┬──────────┘
         │                      │          │
    ┌────▼──┐          ┌────────▼──┐  ┌───▼────┐
    │Human  │          │Automation │  │Logging │
    │Output │          │(JSON/CSV) │  │(CSV)   │
    └───────┘          └───────────┘  └────────┘
```

## Design Decisions

### Why Userspace Tool Instead of Kernel Module Changes?

| Aspect | Kernel Changes | Userspace Tool |
|--------|---|---|
| **Performance** | ✓ Direct | ✗ Slight overhead |
| **Flexibility** | ✗ Recompile needed | ✓ Easy updates |
| **Backward compatibility** | ✗ Risk | ✓ Safe |
| **Customization** | ✗ Limited | ✓ User scripts |
| **Installation** | ✗ Module rebuild | ✓ Simple binary |
| **Dependency management** | ✗ Complex | ✓ Standalone |

**Conclusion**: Userspace tool provides better user experience with minimal overhead.

### Design Principles

1. **Separation of Concerns**
   - Kernel: Collect raw data efficiently
   - Userspace: Interpret and present data

2. **No Kernel Modifications**
   - Tool works with existing kernel module output
   - Can be updated independently

3. **Multiple Output Formats**
   - Same tool serves different use cases
   - Reduce user burden of parsing

4. **Offline Analysis Capability**
   - Save snapshots for later analysis
   - No kernel module required for analysis

## File Organization

```
tools/dmremap-status/
├── README.md                 # Usage guide
├── dmremap_status.h         # Data structures
├── dmremap-status.c         # Main program
├── parser.c                 # Status parser
├── formatter.c              # Human-readable output
├── json.c                   # JSON/CSV output
├── Makefile                 # Build configuration
├── test_status.txt          # Test data
└── man/
    └── dmremap-status.1     # Manual page

tools/dmremap-status.mk      # Makefile integration rules
```

## Implementation Details

### Data Flow

1. **Input**: Raw kernel status string (31 space-separated fields)
2. **Parse**: `parse_dmremap_status()` converts to structured data
3. **Enhance**: `compute_derived_fields()` calculates conversions
4. **Interpret**: Helper functions provide ratings and colors
5. **Output**: Format-specific functions generate output

### Key Functions

**Parser** (parser.c):
- `parse_dmremap_status()` - Parse raw kernel output
- `parse_dmremap_status_file()` - Parse from file
- `compute_derived_fields()` - Calculate derived values
- Rating functions - Health, performance, cache ratings

**Formatter** (formatter.c):
- `print_human_readable()` - Terminal output with colors
- `print_compact()` - Single-line summary

**JSON** (json.c):
- `print_json()` - Full JSON structure
- `print_csv()` - CSV format
- JSON helper functions for escaping/formatting

### Output Formats

#### Human-Readable Format

```
┌───────────────────────────────┐
│      dm-remap Status          │
├───────────────────────────────┤
│ Device Version    : v4.0-...  │
│                               │
├─ PERFORMANCE                 │
│ Avg Latency   7.3 μs [GOOD]  │
...
```

Features:
- Box drawing characters for structure
- Color codes for ratings
- Unit conversions
- Percentage calculations

#### JSON Format

```json
{
  "timestamp": "2025-11-04T10:23:45Z",
  "device": { ... },
  "performance": { ... },
  "health": { ... }
}
```

Features:
- ISO 8601 timestamps
- Nested structure for logical grouping
- Both raw and computed values
- Proper JSON escaping

#### CSV Format

```
timestamp,device,health_score,...
2025-11-04T10:23:45Z,...
```

Features:
- Header row with field names
- ISO 8601 timestamps
- Values suitable for spreadsheets
- One row per invocation

## Building and Installation

### Quick Start

```bash
# Build
cd tools/dmremap-status
make

# Install (requires sudo for /usr/local)
sudo make install

# Test
dmremap-status --input test_status.txt --format human
```

### Integration with Main Build

Add to main Makefile:
```makefile
include tools/dmremap-status.mk

# Then users can do:
# make build-dmremap-status
# make install-dmremap-status
# make test-dmremap-status
```

## Testing Strategy

### Unit Testing

Test each format individually:
```bash
cd tools/dmremap-status
./dmremap-status --input test_status.txt --format human
./dmremap-status --input test_status.txt --format json | jq .
./dmremap-status --input test_status.txt --format csv
```

### Integration Testing

With kernel module:
```bash
# Create device
sudo ./scripts/setup_test.sh

# Query status
sudo dmremap-status dm-test-remap

# All formats
for fmt in human json csv compact; do
  sudo dmremap-status dm-test-remap --format $fmt
done
```

### Regression Testing

```bash
# Parse test data with all versions
for version in v1 v2; do
  ./dmremap-status --input test_status_$version.txt --format json | jq .
done
```

## Extensibility

### Adding New Output Formats

1. **Add format enum** in `dmremap_status.h`:
   ```c
   typedef enum {
       OUTPUT_HUMAN,
       OUTPUT_JSON,
       OUTPUT_MYFORMAT,  // New format
   } output_format_t;
   ```

2. **Implement formatter** in appropriate .c file:
   ```c
   int print_myformat(const dm_remap_status_t *status, FILE *out)
   {
       // Implementation
   }
   ```

3. **Add command-line handling** in `dmremap-status.c`:
   ```c
   case OUTPUT_MYFORMAT:
       ret = print_myformat(status, stdout);
       break;
   ```

4. **Update help text** and man page

### Adding New Statistics

1. **Extend data structure** in `dmremap_status.h`:
   ```c
   typedef struct {
       // ... existing fields ...
       unsigned long long my_new_stat;
   } dm_remap_status_t;
   ```

2. **Add parse case** in `parser.c`:
   ```c
   case 32:  // Assuming new field is position 32
       parsed->my_new_stat = strtoull(token, NULL, 10);
       break;
   ```

3. **Display in formatters** as needed

## Performance Characteristics

### Latency (per invocation)
- Parse: ~0.5ms
- Format: ~0.5ms
- Total: ~1ms

### Memory Usage
- Runtime: ~5KB
- Per status: ~2KB
- Watch mode: Fixed ~100KB

### I/O
- dmsetup query: ~5-10ms
- File read: ~1-2ms
- Output generation: <1ms

**Overhead**: Negligible compared to kernel module operation

## Security Considerations

### No Privilege Escalation

The tool:
- Does NOT require setuid
- Does NOT modify kernel data
- Does NOT access sensitive files
- Inherits permissions from dmsetup

### Safe Error Handling

```c
if (parse_dmremap_status(buffer, status) != 0) {
    fprintf(stderr, "Error: Failed to parse...\n");
    return EXIT_FAILURE;
}
```

All functions validate inputs and handle errors gracefully.

### Resource Limits

- Buffer sizes are bounded
- No dynamic allocation except one status struct
- Safe from buffer overflows
- DoS resistant (no unbounded loops)

## Future Enhancement Possibilities

### Short Term (v1.1)
- [ ] Real-time graphing (ASCII or SVG)
- [ ] Comparative snapshots (delta mode)
- [ ] Custom output templates
- [ ] Prometheus metrics export

### Medium Term (v2.0)
- [ ] Web dashboard integration
- [ ] Historical database backend
- [ ] Alerting system
- [ ] Performance profiling

### Long Term (v3.0)
- [ ] Machine learning anomaly detection
- [ ] Predictive failure analysis
- [ ] Automated remediation
- [ ] Multi-device aggregation

## Documentation

### User Documentation
- `README.md` - Usage guide and examples
- `dmremap-status.1` - Man page
- Command-line help: `dmremap-status --help`

### Developer Documentation
- This file - Architecture and design
- `dmremap_status.h` - API and data structures
- Source code comments - Implementation details

### Integration Points
- `docs/development/STATUS_OUTPUT.md` - Kernel output reference
- `docs/development/KERNEL_VS_USERSPACE_OUTPUT.md` - Design rationale
- `tools/dmremap-status.mk` - Build integration

## Troubleshooting

### Build Issues

**Missing headers**:
```bash
# Install dependencies (Fedora/RHEL)
sudo dnf install glibc-devel

# Install dependencies (Debian/Ubuntu)
sudo apt-get install libc6-dev
```

**Permission errors**:
```bash
# Need sudo for most dmsetup operations
sudo dmremap-status dm-test-remap
```

### Runtime Issues

**Parse errors**:
```bash
# Verify kernel module version
sudo dmsetup table dm-test-remap | grep dm-remap-v4

# Check raw status
sudo dmsetup status dm-test-remap
```

**Timeout in watch mode**:
```bash
# Increase interval (device too busy)
sudo dmremap-status dm-test-remap --watch 5
```

## Version Compatibility

### Kernel Module Compatibility
- Designed for v4.0-phase1.4
- Backward compatible with earlier v4.0 versions
- Auto-detects version field

### OS Compatibility
- Linux kernel 4.10+
- GCC 5.0+
- Standard C99 library
- glibc 2.17+

### Installation Compatibility
- Works with any Linux distribution
- Standalone binary (no shared library dependencies)
- Can be statically compiled if needed

## Related Resources

- Main project: https://github.com/amigatomte/dm-remap
- Linux Device Mapper: https://www.kernel.org/doc/html/latest/admin-guide/device-mapper/
- dmsetup documentation: man dmsetup(8)

## Contributing

When extending the tool:

1. Follow existing code style
2. Add error handling for all new code
3. Update man page for new features
4. Test with multiple formats
5. Document new functions
6. Add test cases

## License

Same as dm-remap project (see LICENSE file)
