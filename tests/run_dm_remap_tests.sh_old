#!/bin/bash
set -e

# Configuration
SPARSE_FILE="test_disk.img"
LOOP_DEV=""
DM_NAME="remap_test"
ERRDEV_NAME="errdev0"
SECTOR_SIZE=512
TOTAL_SECTORS=1000000
SPARE_SECTORS=1000
BAD_SECTORS=(123456 234567 345678)
EXPECTED_STRING="Hello from remapped sector!"

echo "🧱 Creating sparse file..."
dd if=/dev/zero of=$SPARSE_FILE bs=$SECTOR_SIZE count=0 seek=$TOTAL_SECTORS

echo "🔗 Setting up loopback device..."
LOOP_DEV=$(losetup --find --show $SPARSE_FILE)
echo "Loop device: $LOOP_DEV"

START_SECTOR=0
SPARE_START=$((TOTAL_SECTORS - SPARE_SECTORS))

echo "💣 Creating error-mapped device to simulate multiple unreadable sectors..."
TABLE=""
PREV_END=0
for BAD in "${BAD_SECTORS[@]}"; do
    # Add normal region before the bad sector
    if [ "$BAD" -gt "$PREV_END" ]; then
        LENGTH=$((BAD - PREV_END))
        TABLE+="$PREV_END $LENGTH linear $LOOP_DEV $PREV_END\n"
    fi
    # Add error sector
    TABLE+="$BAD 1 error\n"
    PREV_END=$((BAD + 1))
done

# Add remaining normal region
if [ "$PREV_END" -lt "$TOTAL_SECTORS" ]; then
    LENGTH=$((TOTAL_SECTORS - PREV_END))
    TABLE+="$PREV_END $LENGTH linear $LOOP_DEV $PREV_END\n"
fi

echo -e "$TABLE" | dmsetup create $ERRDEV_NAME

echo "🧩 Creating dm-remap target on top of error device..."
dmsetup create $DM_NAME --table "0 $SPARE_START remap /dev/mapper/$ERRDEV_NAME $START_SECTOR $SPARE_START"

# Test each bad sector
for BAD in "${BAD_SECTORS[@]}"; do
    echo "🔍 Attempting to read from bad sector $BAD (should fail)..."
    set +e
    dd if=/dev/mapper/$DM_NAME bs=$SECTOR_SIZE skip=$BAD count=1 status=none
    if [ $? -ne 0 ]; then
        echo "✅ Read failure confirmed for sector $BAD."
    else
        echo "❌ Unexpected success reading bad sector $BAD."
        exit 1
    fi
    set -e

    echo "🛠 Remapping bad sector $BAD..."
    dmsetup message $DM_NAME 0 remap $BAD

    echo "💾 Writing test data to remapped sector $BAD..."
    echo "$EXPECTED_STRING $BAD" | dd of=/dev/mapper/$DM_NAME bs=$SECTOR_SIZE seek=$BAD count=1 conv=notrunc status=none

    echo "🔁 Reading back data from remapped sector $BAD..."
    READ_DATA=$(dd if=/dev/mapper/$DM_NAME bs=$SECTOR_SIZE skip=$BAD count=1 status=none | strings)

    echo "🔎 Verifying data match for sector $BAD..."
    if [[ "$READ_DATA" == *"$EXPECTED_STRING $BAD"* ]]; then
        echo "✅ Data match confirmed for sector $BAD."
    else
        echo "❌ Data mismatch at sector $BAD!"
        echo "Expected: $EXPECTED_STRING $BAD"
        echo "Got: $READ_DATA"
        exit 1
    fi
done

echo "🧹 Cleaning up..."
dmsetup remove $DM_NAME
dmsetup remove $ERRDEV_NAME
losetup -d $LOOP_DEV
rm -f $SPARSE_FILE

echo "🎉 All multi-sector tests completed successfully."
