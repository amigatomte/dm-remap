#!/bin/bash
#
# explain_dm_linear_error.sh - Detailed explanation of dm-linear + dm-error precision
#

echo "=========================================================================="
echo "HOW DM-LINEAR + DM-ERROR PROVIDES SURGICAL PRECISION SECTOR CONTROL"
echo "=========================================================================="
echo

echo "🎯 CONCEPT: Create a 'composite' device where different sector ranges"
echo "           map to different targets (linear vs error)"
echo

echo "=========================================================================="
echo "STEP-BY-STEP BREAKDOWN"
echo "=========================================================================="
echo

echo "1️⃣ DEFINE BAD SECTORS"
echo "──────────────────────"
echo "Let's say we want these specific sectors to be 'bad':"
echo "   Bad sectors: 1000, 1001, 5000, 12000"
echo "   Total device size: 20,000 sectors"
echo

echo "2️⃣ CREATE DEVICE MAPPING TABLE"
echo "───────────────────────────────"
echo "We create a table that maps sector ranges to different targets:"
echo

cat << 'EOF'
┌─────────────┬───────┬─────────────┬──────────────────────┐
│ Start Sector│ Count │ Target Type │ Destination          │
├─────────────┼───────┼─────────────┼──────────────────────┤
│ 0           │ 1000  │ linear      │ /dev/loop0 offset 0  │  ← Good sectors
│ 1000        │ 1     │ error       │ (always fails)       │  ← Bad sector!
│ 1001        │ 1     │ error       │ (always fails)       │  ← Bad sector!
│ 1002        │ 3998  │ linear      │ /dev/loop0 offset 1002│ ← Good sectors
│ 5000        │ 1     │ error       │ (always fails)       │  ← Bad sector!
│ 5001        │ 6999  │ linear      │ /dev/loop0 offset 5001│ ← Good sectors
│ 12000       │ 1     │ error       │ (always fails)       │  ← Bad sector!
│ 12001       │ 7999  │ linear      │ /dev/loop0 offset 12001│← Good sectors
└─────────────┴───────┴─────────────┴──────────────────────┘
EOF

echo
echo "3️⃣ VISUAL REPRESENTATION"
echo "─────────────────────────"
echo

cat << 'EOF'
Original Device (20,000 sectors):
[0════════════════════════════════════════════20000]
 └──────── All I/O goes to /dev/loop0 ──────────┘

Composite Device (same 20,000 sectors):
[0═══1000║1001║1002═══════5000║5001═══12000║12001═══20000]
 │    │    │    │           │    │       │      │
 │    │    │    │           │    │       │      └─ linear → /dev/loop0
 │    │    │    │           │    │       └─ ERROR (fails!)
 │    │    │    │           │    └─ linear → /dev/loop0  
 │    │    │    │           └─ ERROR (fails!)
 │    │    │    └─ linear → /dev/loop0
 │    │    └─ ERROR (fails!)
 │    └─ ERROR (fails!)
 └─ linear → /dev/loop0
EOF

echo
echo "4️⃣ HOW I/O OPERATIONS WORK"
echo "───────────────────────────"
echo

echo "When an application reads/writes to the composite device:"
echo

echo "✅ READ/WRITE to sector 500:"
echo "   → Lookup table: sector 500 is in range [0-999]"
echo "   → Target: linear → /dev/loop0 offset 500"
echo "   → Result: SUCCESS (normal I/O to real device)"
echo

echo "❌ READ/WRITE to sector 1000:"
echo "   → Lookup table: sector 1000 is in range [1000-1000]"
echo "   → Target: error"
echo "   → Result: I/O ERROR (permanent failure)"
echo

echo "✅ READ/WRITE to sector 2000:"
echo "   → Lookup table: sector 2000 is in range [1002-4999]"
echo "   → Target: linear → /dev/loop0 offset 2000"
echo "   → Result: SUCCESS (normal I/O to real device)"
echo

echo "❌ READ/WRITE to sector 5000:"
echo "   → Lookup table: sector 5000 is in range [5000-5000]"
echo "   → Target: error"
echo "   → Result: I/O ERROR (permanent failure)"
echo

echo "=========================================================================="
echo "ACTUAL IMPLEMENTATION"
echo "=========================================================================="
echo

echo "🔧 The dmsetup table creation code:"
echo

cat << 'EOF'
create_precision_bad_sectors() {
    local device=$1
    local sectors=$2
    local bad_sectors_array=("${!3}")
    
    # Sort bad sectors for proper table creation
    IFS=$'\n' sorted_bad_sectors=($(sort -n <<< "${bad_sectors_array[*]}"))
    
    local table=""
    local current_sector=0
    
    for bad_sector in "${sorted_bad_sectors[@]}"; do
        # Add good sectors before the bad sector
        if [ $current_sector -lt $bad_sector ]; then
            local good_count=$((bad_sector - current_sector))
            table+="$current_sector $good_count linear $device $current_sector\n"
        fi
        
        # Add the bad sector (using error target)
        table+="$bad_sector 1 error\n"
        current_sector=$((bad_sector + 1))
    done
    
    # Add remaining good sectors
    if [ $current_sector -lt $sectors ]; then
        local remaining=$((sectors - current_sector))
        table+="$current_sector $remaining linear $device $current_sector\n"
    fi
    
    # Create the device
    echo -e "$table" | sudo dmsetup create precision-bad-sectors
}
EOF

echo
echo "🔧 Example dmsetup table output for bad sectors [1000, 1001, 5000]:"
echo

cat << 'EOF'
0 1000 linear /dev/loop0 0          # Sectors 0-999: normal I/O
1000 1 error                        # Sector 1000: always fails
1001 1 error                        # Sector 1001: always fails  
1002 3998 linear /dev/loop0 1002     # Sectors 1002-4999: normal I/O
5000 1 error                        # Sector 5000: always fails
5001 14999 linear /dev/loop0 5001    # Sectors 5001-19999: normal I/O
EOF

echo
echo "=========================================================================="
echo "WHY THIS IS EQUIVALENT TO DM-DUST"
echo "=========================================================================="
echo

echo "dm-dust functionality:"
echo "  • Dynamic add/remove bad sectors: addbadblock/removebadblock"
echo "  • Precise sector-level control: Individual sector targeting"
echo "  • Permanent sector failures: Bad sectors always fail"
echo

echo "dm-linear + dm-error equivalent:"
echo "  • Static bad sector definition: Defined at device creation"
echo "  • Precise sector-level control: Individual sector targeting ✅"
echo "  • Permanent sector failures: Bad sectors always fail ✅"
echo

echo "🎯 ADVANTAGES of dm-linear + dm-error:"
echo "  ✅ More realistic: Real hardware bad sectors don't disappear"
echo "  ✅ Always available: No special kernel modules needed"
echo "  ✅ Same precision: Exact sector-level control"
echo "  ✅ Better performance: No overhead of dynamic management"
echo

echo "=========================================================================="
echo "TESTING RESULTS FROM YOUR SYSTEM"
echo "=========================================================================="
echo

echo "From your actual test run with bad sectors [1000,1001,5000,12000,12001,12002,25000]:"
echo

cat << 'EOF'
Final precision bad sector table:
0 1000 linear 7:17 0           # Sectors 0-999: SUCCESS
1000 1 error                   # Sector 1000: FAILS ✅
1001 1 error                   # Sector 1001: FAILS ✅
1002 3998 linear 7:17 1002     # Sectors 1002-4999: SUCCESS
5000 1 error                   # Sector 5000: FAILS ✅
5001 6999 linear 7:17 5001     # Sectors 5001-11999: SUCCESS
12000 1 error                  # Sector 12000: FAILS ✅
12001 1 error                  # Sector 12001: FAILS ✅
12002 1 error                  # Sector 12002: FAILS ✅
12003 12997 linear 7:17 12003  # Sectors 12003-24999: SUCCESS
25000 1 error                  # Sector 25000: FAILS ✅
25001 179799 linear 7:17 25001 # Sectors 25001-204799: SUCCESS

Test Results:
Testing precise bad sector 1000: Failed as expected ✅
Testing precise bad sector 1001: Failed as expected ✅
Testing precise bad sector 5000: Failed as expected ✅
Testing precise bad sector 12000: Failed as expected ✅
Testing precise bad sector 12001: Failed as expected ✅
Testing precise bad sector 12002: Failed as expected ✅
Testing precise bad sector 25000: Failed as expected ✅

Testing good sector 2000: Success ✅
Testing good sector 6000: Success ✅
Testing good sector 15000: Success ✅  
Testing good sector 30000: Success ✅
EOF

echo
echo "=========================================================================="
echo "CONCLUSION"
echo "=========================================================================="
echo

echo "🏆 dm-linear + dm-error provides SURGICAL PRECISION sector control:"
echo
echo "  🎯 Exact sector targeting: Any individual sector can be marked bad"
echo "  🎯 Multiple bad sectors: Can create complex bad sector patterns"
echo "  🎯 Permanent failures: Bad sectors always fail (realistic)"
echo "  🎯 Perfect integration: Works seamlessly with dm-remap"
echo "  🎯 No special requirements: Uses standard device mapper targets"
echo
echo "This technique gives you the same precision as dm-dust while being"
echo "more widely available and often more realistic for testing scenarios!"
echo
echo "Happy testing! 🚀"