# dmremap-status - Userspace Status Formatter

A userspace utility for interpreting and displaying dm-remap kernel module status output in multiple human-friendly and machine-parseable formats.

## Overview

The dm-remap kernel module outputs 31 fields of diagnostic and performance data via `dmsetup status`. The `dmremap-status` tool parses this raw output and presents it in formats suitable for:

- **Human Operators**: Colored, formatted terminal output with interpretation and ratings
- **Automation**: JSON, CSV, and structured data formats  
- **Monitoring**: Watch mode for real-time status tracking
- **Analysis**: Offline status analysis from saved snapshots

## Features

✅ **Multiple Output Formats**
- Human-readable with color indicators
- JSON for scripting and automation
- CSV for logging and graphing
- Compact single-line summary
- Raw kernel output passthrough

✅ **Smart Interpretation**
- Health status ratings (0-100 scale)
- Performance ratings based on latency
- Cache efficiency analysis
- Automatic field calculations and conversions

✅ **Monitoring Capabilities**
- Watch mode with configurable refresh rate
- Real-time performance tracking
- Hotspot detection
- Health degradation alerts

✅ **Offline Analysis**
- Read from saved status snapshots
- No kernel module required for analysis
- Perfect for CI/CD integration

✅ **User-Friendly Presentation**
- Color-coded indicators (green/yellow/red)
- Intuitive section organization
- Automatic unit conversions (ns→μs, B→MB, sectors→GB)
- Percentage calculations where relevant

## Building

### Prerequisites
- GCC compiler
- Standard C library development files
- For live queries: Linux kernel with dm-remap module, dmsetup utility

### Build Steps

```bash
cd tools/dmremap-status
make                    # Build the tool
make install            # Install to /usr/local/bin
make install PREFIX=/usr  # Install to /usr/bin
```

### Build Output
```
cc -Wall -Wextra -O2 -g -o dmremap-status \
    dmremap-status.o parser.o formatter.o json.o
```

## Installation

```bash
# Build
cd tools/dmremap-status
make

# Install (requires sudo)
sudo make install

# Verify
dmremap-status --version
```

To install to a different location:
```bash
sudo make install PREFIX=/opt/dm-remap
```

## Usage

### Basic Status (Human-Readable)

```bash
# Show status for device dm-test-remap
sudo dmremap-status dm-test-remap
```

Output:
```
┌───────────────────────────────────────────────────────────────────┐
│                    dm-remap Status                               │
├───────────────────────────────────────────────────────────────────┤
│  Device Version        : v4.0-phase1.4
│  Device Size           : 2.0 GB (4194304 sectors)
│
│  Main Device               : /dev/mapper/dm-test-linear
│  Spare Device              : /dev/loop1
│  Spare Capacity            : Available
│    └─ 2047.0 GB (4292870144 sectors)
├───────────────────────────────────────────────────────────────────┤
├─ PERFORMANCE
│  Avg Latency                  7.3 μs  [EXCELLENT]
│  Throughput                   723 MB/s
│  I/O Operations               6 completed
├───────────────────────────────────────────────────────────────────┤
├─ HEALTH
│  Health Score                 [EXCELLENT 100/100]
│  Operational State            operational
│  Errors                       0
│  Hotspots                     0 ✓
...
```

### JSON Output

```bash
# Get JSON for scripting
sudo dmremap-status dm-test-remap --format json

# Parse with jq
sudo dmremap-status dm-test-remap --format json | jq '.health'
{
  "score": 100,
  "status": "EXCELLENT",
  "hotspots": 0
}
```

### CSV Logging

```bash
# Log to file for analysis
sudo dmremap-status dm-test-remap --format csv >> history.csv

# View history
cat history.csv
```

### Watch Mode

```bash
# Refresh every 2 seconds
sudo dmremap-status dm-test-remap --watch 2

# Watch with compact output
sudo dmremap-status dm-test-remap --watch 1 --format compact
```

### Offline Analysis

```bash
# Save a status snapshot
sudo dmsetup status dm-test-remap > status_snapshot.txt

# Analyze offline (no sudo needed)
dmremap-status --input status_snapshot.txt --format human
dmremap-status --input status_snapshot.txt --format json | jq .
```

## Output Formats

### Human-Readable

Pretty-printed output with colors, indicators, and interpretation:

```
├─ PERFORMANCE
│  Avg Latency                  7.3 μs  [EXCELLENT]
│  Throughput                   723 MB/s
│  I/O Operations               6 completed
```

### JSON

Complete structured format suitable for automation:

```json
{
  "timestamp": "2025-11-04T10:23:45Z",
  "health": {
    "score": 100,
    "status": "EXCELLENT",
    "hotspots": 0
  },
  "performance": {
    "avg_latency_us": 7.30,
    "throughput_mbps": 723.00
  }
}
```

### CSV

Comma-separated values with header for spreadsheets:

```
timestamp,device,health_score,avg_latency_us,throughput_mbps,errors
2025-11-04T10:23:45Z,/dev/mapper/dm-test-linear,100,7.30,723,0
```

### Compact

Single-line summary:

```
v4.0-phase1.4: health=100% latency=7.3μs throughput=723MB/s cache=66% errors=0
```

### Raw

Raw kernel output (unchanged):

```
0 4194304 dm-remap-v4 v4.0-phase1.4 /dev/mapper/dm-test-linear /dev/loop1 6 0 0 0 1 6 ...
```

## Command-Line Options

```
-f, --format FORMAT      Output format: human|json|csv|raw|compact (default: human)
-i, --input FILE         Read from file instead of dmsetup
-w, --watch INTERVAL     Watch mode: refresh every N seconds
-n, --no-color          Disable colored output
-h, --help              Show help message
-v, --version           Show version
```

## Output Interpretation

### Health Score (0-100)
- **95-100 (Green)**: EXCELLENT - Perfect operation
- **80-94 (Green)**: GOOD - Normal operation  
- **60-79 (Yellow)**: FAIR - Minor issues
- **40-59 (Red)**: POOR - Significant issues
- **<40 (Red)**: CRITICAL - Immediate attention needed

### Latency Performance
- **<10 μs**: EXCELLENT (optimal O(1) performance)
- **<50 μs**: GOOD
- **<100 μs**: FAIR
- **<1 ms**: POOR
- **≥1 ms**: CRITICAL

### Cache Hit Rate
- **≥80%**: EXCELLENT
- **≥60%**: GOOD
- **≥40%**: FAIR
- **≥20%**: POOR
- **<20%**: CRITICAL

## Source Code Organization

```
tools/dmremap-status/
├── dmremap_status.h      # Data structures and function declarations
├── dmremap-status.c      # Main program and CLI argument parsing
├── parser.c              # Parse 31-field kernel status output
├── formatter.c           # Human-readable formatted output
├── json.c                # JSON and CSV output formatters
├── Makefile              # Build configuration
├── man/
│   └── dmremap-status.1  # Manual page
├── test_status.txt       # Test data (example status output)
└── README.md             # This file
```

## Data Structure

The tool parses the 31-field kernel status into this structure:

```c
typedef struct {
    /* Header (1-3) */
    char target_type[64];
    unsigned long long device_size_sectors;
    
    /* Version & Devices (4-6) */
    char version[64];
    char main_device[256];
    char spare_device[256];
    
    /* Statistics (7-29) */
    unsigned long long total_reads, total_writes, total_remaps, total_errors;
    unsigned int active_remaps;
    unsigned long long avg_latency_ns, throughput_bps;
    /* ... more fields ... */
    
    /* Health (27-31) */
    unsigned int health_score;
    unsigned int hotspot_count;
    char operational_state[32];
} dm_remap_status_t;
```

See `dmremap_status.h` for complete structure definition.

## Development

### Extending the Tool

To add a new output format:

1. Add format enum value to `output_format_t`
2. Create new function `print_myformat()` in appropriate .c file
3. Add case handler in `dmremap-status.c` main loop
4. Update help text and man page

### Testing

```bash
# Test with provided test data
./dmremap-status --input test_status.txt --format human

# Test all formats
for fmt in human json csv compact raw; do
  echo "=== Format: $fmt ==="
  ./dmremap-status --input test_status.txt --format $fmt
done

# Validate JSON output
./dmremap-status --input test_status.txt --format json | jq .
```

## Known Limitations

- Watch mode requires continuous sudo access to dmsetup
- JSON output is generated in-process (no external library required)
- CSV format outputs one line per invocation (no aggregation)
- Terminal detection uses `isatty()` for color detection

## Performance

- Parsing: <1ms for typical status string
- Formatting: <1ms for any output format
- Total overhead: Negligible compared to kernel module operation

## Future Enhancements

Potential future improvements:

- [ ] Real-time graphing with gnuplot/SVG output
- [ ] Historical trend analysis
- [ ] Comparative snapshots (delta mode)
- [ ] Automated alerting on health degradation
- [ ] Integration with monitoring systems (Prometheus, etc.)
- [ ] Web dashboard support
- [ ] Performance profiling mode

## License

See LICENSE file in dm-remap repository

## Contributing

Contributions welcome! Please:

1. Follow existing code style
2. Add tests for new features
3. Update documentation
4. Test with multiple output formats

## Related Documentation

- `docs/development/STATUS_OUTPUT.md` - Complete field-by-field reference
- `docs/development/STATUS_QUICK_REFERENCE.md` - Quick lookup guide
- `docs/development/KERNEL_VS_USERSPACE_OUTPUT.md` - Design rationale
- `dmremap-status.1` - Man page (run `man dmremap-status`)

## Troubleshooting

### "Permission denied" errors
- Most dmsetup operations require sudo: `sudo dmremap-status dm-test-remap`

### "Device not found" errors
- Verify device exists: `sudo dmsetup ls`
- Device must be a dm-remap target

### Parse errors
- Ensure kernel module is running version 4.0-phase1.4 or compatible
- Save raw output and test with `--input` for offline debugging

### Color not showing in output
- Use `--no-color` flag
- Colors only appear on TTY outputs
- Set `NO_COLOR=1` environment variable to disable globally
