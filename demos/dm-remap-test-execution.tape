# vhs tape file for dm-remap Full Test Execution
# This demonstrates running the complete integration test with pause points
#
# Usage: vhs < demos/dm-remap-test-execution.tape > dm-remap-test.mp4

Set WindowWidth 1280
Set WindowHeight 800
Set TypingSpeed 40ms
Set Theme Dracula
Set FontSize 18
Set Framerate 60

Clear

# Title
Type "╔═══════════════════════════════════════════════════════════════╗"
Enter
Type "║     dm-remap + ZFS Integration Test Execution                ║"
Enter
Type "║                                                               ║"
Enter
Type "║     This test demonstrates dm-remap working with ZFS to:     ║"
Enter
Type "║     • Create transparent block remapping                      ║"
Enter
Type "║     • Handle bad sectors automatically                        ║"
Enter
Type "║     • Maintain data integrity                                 ║"
Enter
Type "║     • Achieve excellent compression ratios                    ║"
Enter
Type "╚═══════════════════════════════════════════════════════════════╝"
Enter
Sleep 3s

Clear

# Start test
Type "# Starting dm-remap + ZFS integration test..."
Enter
Sleep 1s

Type ""
Enter
Type "bash tests/dm_remap_zfs_integration_test.sh"
Enter
Sleep 1s

Type ""
Enter
Type "[INFO] ════════════════════════════════════════════════════════════"
Enter
Type "[INFO] dm-remap + ZFS Integration Test"
Enter
Type "[INFO] ════════════════════════════════════════════════════════════"
Enter
Sleep 1s

# Step 1
Type "[INFO] "
Enter
Type "[INFO] Step 1: Check Prerequisites"
Enter
Type "[INFO] Checking prerequisites..."
Enter
Type "[✓] dm-remap module loaded"
Enter
Type "[✓] zfs tools available"
Enter
Type "[✓] dmsetup available"
Enter
Type "[✓] Sufficient disk space available"
Enter
Sleep 2s

# Step 2
Type "[INFO] Step 2: Create Test Devices"
Enter
Type "[INFO] Creating test devices..."
Enter
Type "[INFO] Creating sparse file: /tmp/dm-remap-test-sparse (512M)"
Enter
Type "[INFO] Command: Create main device sparse file"
Enter
Type "[INFO]   $ dd if=/dev/zero of=\"/tmp/dm-remap-test-sparse\" bs=1M count=512"
Enter
Type "[✓] Sparse file created"
Enter
Sleep 1s

Type "[INFO] Creating spare file: /tmp/dm-remap-test-spare (512M)"
Enter
Type "[✓] Spare file created"
Enter
Sleep 1s

Type "[INFO] Setting up loopback devices..."
Enter
Type "[✓] Main device: /dev/loop17"
Enter
Type "[✓] Spare device: /dev/loop18"
Enter
Type "[✓] Main device size: 536870912 bytes"
Enter
Type "[✓] Spare device size: 536870912 bytes"
Enter
Sleep 2s

# Step 3
Type "[INFO] Step 3: Setup dm-remap Device"
Enter
Type "[INFO] Setting up dm-remap device..."
Enter
Type "[INFO] Device sectors: 1048576"
Enter
Type "[INFO] Creating dm-remap mapping..."
Enter
Type "[✓] dm-remap device created: /dev/mapper/dm-test-main"
Enter
Type "[✓] dm-remap device verified"
Enter
Sleep 2s

# Step 4
Type "[INFO] Step 4: Create ZFS Pool"
Enter
Type "[INFO] Creating ZFS pool..."
Enter
Type "[✓] ZFS pool created: dm-test-pool"
Enter
Type "[✓] ZFS pool configured"
Enter
Sleep 1s

Type ""
Enter
Type "NAME           SIZE  ALLOC   FREE  CKPOINT  EXPANDSZ   FRAG    CAP  DEDUP    HEALTH  ALTROOT"
Enter
Type "dm-test-pool   480M   236K   480M        -         -    11%     0%  1.00x    ONLINE  -"
Enter
Type ""
Enter
Type "NAME                USED  AVAIL  REFER  MOUNTPOINT"
Enter
Type "dm-test-pool/data    24K   352M    24K  /mnt/dm-remap-test"
Enter
Sleep 2s

# Step 5
Type "[INFO] Step 5: Write Test Files"
Enter
Type "[INFO] Writing test files to ZFS pool..."
Enter
Type "[✓] Created /mnt/dm-remap-test/test-data/test-file-1MB.bin"
Enter
Type "[✓] Created /mnt/dm-remap-test/test-data/test-file-5MB.bin"
Enter
Type "[✓] Created /mnt/dm-remap-test/test-data/test-file-10MB.bin"
Enter
Type "[✓] Pattern test file created"
Enter
Sleep 1s

Type ""
Enter
Type "total 15M"
Enter
Type "-rw-r--r-- 1 root root  10M Nov 10 14:27 pattern-test.bin"
Enter
Type "-rw-r--r-- 1 root root  10M Nov 10 14:27 test-file-10MB.bin"
Enter
Type "-rw-r--r-- 1 root root 1.0M Nov 10 14:27 test-file-1MB.bin"
Enter
Type "-rw-r--r-- 1 root root 5.0M Nov 10 14:27 test-file-5MB.bin"
Enter
Sleep 2s

# Step 6 - Pause point
Type "[INFO] Step 6: Inject Bad Sectors"
Enter
Type "[INFO] Injecting bad sectors via dm-remap..."
Enter
Type "[✓] Bad sectors injected"
Enter
Sleep 1s

Type ""
Enter
Type "╔════════════════════════════════════════════════════════════╗"
Enter
Type "║ Bad sectors injected and ready for filesystem testing     ║"
Enter
Type "║ Press ENTER to continue, or Ctrl+C to abort...            ║"
Enter
Type "╚════════════════════════════════════════════════════════════╝"
Enter
Sleep 2s

Type ""
Enter

# Step 7
Type "[INFO] Step 7: Read Test Files (Trigger Remapping)"
Enter
Type "[INFO] Reading test files (triggering dm-remap)..."
Enter
Type "[✓] Test files read"
Enter
Sleep 1s

# Step 8
Type "[INFO] Step 8: ZFS Scrub"
Enter
Type "[INFO] Running ZFS scrub to verify pool integrity..."
Enter
Type "[INFO] Starting ZFS scrub (this may take a moment)..."
Enter
Type ""
Enter
Type "  pool: dm-test-pool"
Enter
Type " state: ONLINE"
Enter
Type "  scan: scrub repaired 0B in 00:00:00 with 0 errors on Mon Nov 10 14:27:11 2025"
Enter
Type ""
Enter
Type "config:"
Enter
Type "        NAME            STATE     READ WRITE CKSUM"
Enter
Type "        dm-test-pool    ONLINE       0     0     0"
Enter
Type "          dm-test-main  ONLINE       0     0     0"
Enter
Type ""
Enter
Type "errors: No known data errors"
Enter
Sleep 2s

# Step 9
Type "[INFO] Step 9: Comprehensive Filesystem Tests"
Enter
Type "[INFO] Running comprehensive filesystem tests..."
Enter
Type "[✓] Copy test completed"
Enter
Type "[✓] Delete/recreate test completed"
Enter
Type "[✓] Compression test completed (1.46x ratio)"
Enter
Type "[✓] Directory test completed"
Enter
Type "[✓] Large file test completed"
Enter
Sleep 2s

# Step 10 - Report
Type "[INFO] Step 10: Generate Report"
Enter
Type ""
Enter
Type "╔════════════════════════════════════════════════════════════╗"
Enter
Type "║     dm-remap + ZFS Integration Test Report               ║"
Enter
Type "║     Generated: Mon Nov 10 14:27:22 CET 2025              ║"
Enter
Type "╚════════════════════════════════════════════════════════════╝"
Enter
Sleep 1s

Type ""
Enter
Type "TEST CONFIGURATION"
Enter
Type "═════════════════════════════════════════════════════════════"
Enter
Type "Device Size:          512 MB"
Enter
Type "Main Loop Device:     /dev/loop17"
Enter
Type "Spare Loop Device:    /dev/loop18"
Enter
Type "ZFS Pool:             dm-test-pool"
Enter
Type "Mount Point:          /mnt/dm-remap-test"
Enter
Sleep 1s

Type ""
Enter
Type "TEST RESULTS"
Enter
Type "═════════════════════════════════════════════════════════════"
Enter
Type "✓ All 9 test steps passed"
Enter
Type "✓ ZFS pool status: ONLINE"
Enter
Type "✓ Data integrity errors: 0"
Enter
Type "✓ Compression ratio: 1.46x"
Enter
Type "✓ Read performance: Normal"
Enter
Type "✓ Write performance: Normal"
Enter
Sleep 2s

# Pause before cleanup
Type ""
Enter
Type "╔════════════════════════════════════════════════════════════╗"
Enter
Type "║ All tests completed. Ready to clean up test environment   ║"
Enter
Type "║ Press ENTER to continue, or Ctrl+C to abort...            ║"
Enter
Type "╚════════════════════════════════════════════════════════════╝"
Enter
Sleep 2s

Type ""
Enter

# Cleanup
Type "[INFO] Cleaning up test environment..."
Enter
Type "[✓] Cleanup complete"
Enter
Sleep 1s

Type ""
Enter
Type "[✓] ════════════════════════════════════════════════════════════"
Enter
Type "[✓] All tests completed successfully!"
Enter
Type "[✓] ════════════════════════════════════════════════════════════"
Enter
Sleep 1s

Type ""
Enter
Type "[✓] Commands script saved to: /tmp/dm-remap-test-commands-20251110-142707.sh"
Enter
Sleep 2s

Clear

# Final summary
Type "╔═══════════════════════════════════════════════════════════════╗"
Enter
Type "║                    Test Summary                               ║"
Enter
Type "║                                                               ║"
Enter
Type "║  dm-remap successfully integrated with ZFS:                  ║"
Enter
Type "║                                                               ║"
Enter
Type "║  ✓ Created and managed 512MB test devices                    ║"
Enter
Type "║  ✓ Set up transparent block remapping                        ║"
Enter
Type "║  ✓ Built ZFS pool with lz4 compression                      ║"
Enter
Type "║  ✓ Verified data integrity (0 errors)                       ║"
Enter
Type "║  ✓ Achieved 1.46x compression ratio                          ║"
Enter
Type "║  ✓ Maintained normal performance                             ║"
Enter
Type "║                                                               ║"
Enter
Type "║  All operations transparent to applications!                 ║"
Enter
Type "║                                                               ║"
Enter
Type "╚═══════════════════════════════════════════════════════════════╝"
Enter
Sleep 3s

Type ""
Enter
Type "Learn more: https://github.com/amigatomte/dm-remap"
Enter
Sleep 1s

Type "For more details, see:"
Enter
Type "  • README.md - Quick start guide"
Enter
Type "  • TOOLS.md - Available utilities"
Enter
Type "  • docs/user/QUICK_START.md - Detailed walkthrough"
Enter
Sleep 2s
