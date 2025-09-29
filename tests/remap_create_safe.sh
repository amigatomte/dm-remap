#!/bin/bash

# remap_create_safe.sh
#     The alignment‑aware wrapper that:
#         Detects the underlying devices’ physical sector sizes.
#         Refuses mismatched sizes.
#         Auto‑aligns offsets/lengths if needed, logging any adjustments.
#         Returns an exit code so the caller knows if it adjusted, aborted, or succeeded cleanly.
#     Can be used standalone in production or called from any harness.

set -euo pipefail

# === Defaults ===
main_dev=""
main_start=0
spare_dev=""
spare_start=0
spare_total=""
logfile=""
dm_name="test_remap"   # default mapping name

# === Usage / Help ===
show_usage() {
    cat <<EOF
Usage:
  $0 main_dev=/dev/sda spare_dev=/dev/sdb [main_start=N] [spare_start=N] [spare_total=N] [logfile=/path/to/log] [dm_name=name]

Arguments (order-independent, key=value form):
  main_dev     : Path to main device (required)
  main_start   : Start sector on main device (default: 0)
  spare_dev    : Path to spare device (required)
  spare_start  : Start sector on spare device (default: 0)
  spare_total  : Number of sectors to map from spare device.
                 If omitted, uses full spare device size minus spare_start.
  logfile      : Path to log file (default: auto-generated in /tmp/)
  dm_name      : Name for the device-mapper mapping (default: test_remap)

Notes:
  - All sector values are in 512-byte sectors.
  - Physical sector sizes of both devices must match.
  - Offsets and lengths will be auto-aligned to the physical sector size if needed.
  - Exit codes:
      0 = success, no adjustments
      1 = success, adjustments made
      2 = aborted due to sector size mismatch
EOF
}

# === Parse key=value args ===
for arg in "$@"; do
    case "$arg" in
        main_dev=*)    main_dev="${arg#*=}" ;;
        main_start=*)  main_start="${arg#*=}" ;;
        spare_dev=*)   spare_dev="${arg#*=}" ;;
        spare_start=*) spare_start="${arg#*=}" ;;
        spare_total=*) spare_total="${arg#*=}" ;;
        logfile=*)     logfile="${arg#*=}" ;;
        dm_name=*)     dm_name="${arg#*=}" ;;
        -h|--help)     show_usage; exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; show_usage; exit 1 ;;
    esac
done

# === Validate required args ===
if [[ -z "$main_dev" || -z "$spare_dev" ]]; then
    echo "ERROR: main_dev and spare_dev are required." >&2
    show_usage
    exit 1
fi

# === Default logfile if not provided ===
if [[ -z "$logfile" ]]; then
    ts=$(date +%Y%m%d_%H%M%S)
    logfile="/tmp/remap_wrapper_${ts}.log"
    echo "[i] No logfile specified — using $logfile"
fi

# === Default spare_total if not provided ===
if [[ -z "$spare_total" ]]; then
    total_sectors=$(blockdev --getsz "$spare_dev")
    spare_total=$(( total_sectors - spare_start ))
fi

# === Check physical sector sizes early for live output ===
phys_size_main=$(cat /sys/block/$(basename "$(readlink -f "$main_dev")")/queue/hw_sector_size)
phys_size_spare=$(cat /sys/block/$(basename "$(readlink -f "$spare_dev")")/queue/hw_sector_size)

# === Live echo of chosen parameters ===
echo "=== remap_create_safe.sh parameters ==="
echo "Main device:   $main_dev"
echo "Main start:    $main_start sectors"
echo "Spare device:  $spare_dev"
echo "Spare start:   $spare_start sectors"
echo "Spare total:   $spare_total sectors"
echo "Main phys size:  $phys_size_main B"
echo "Spare phys size: $phys_size_spare B"
echo "DM name:       $dm_name"
echo "Log file:      $logfile"
echo "======================================="

# === Log initial parameters ===
{
echo "=== WRAPPER: remap_create_safe.sh ==="
echo "Main device:   $main_dev"
echo "Main start:    $main_start sectors"
echo "Spare device:  $spare_dev"
echo "Spare start:   $spare_start sectors"
echo "Spare total:   $spare_total sectors"
echo "Main phys sector size:  $phys_size_main B"
echo "Spare phys sector size: $phys_size_spare B"
echo "DM name:       $dm_name"
} >> "$logfile"

# === Check physical sector size match ===
if [[ "$phys_size_main" != "$phys_size_spare" ]]; then
    echo "ERROR: Physical sector sizes differ: $main_dev=${phys_size_main}B, $spare_dev=${phys_size_spare}B" | tee -a "$logfile"
    exit 2
fi

# === Alignment check ===
phys_sectors=$((phys_size_main / 512))

adjusted_main_start=$(( (main_start / phys_sectors) * phys_sectors ))
adjusted_spare_start=$(( (spare_start / phys_sectors) * phys_sectors ))
adjusted_spare_total=$(( (spare_total / phys_sectors) * phys_sectors ))

ADJUSTED_FLAG=0
if [[ "$main_start" != "$adjusted_main_start" || "$spare_start" != "$adjusted_spare_start" || "$spare_total" != "$adjusted_spare_total" ]]; then
    ADJUSTED_FLAG=1
    echo "WARNING: Values not aligned to ${phys_size_main}B physical sectors."
    echo " Adjusting: main_start=$adjusted_main_start, spare_start=$adjusted_spare_start, spare_total=$adjusted_spare_total"
    echo "WARNING: Values not aligned to ${phys_size_main}B physical sectors." >> "$logfile"
    echo " Adjusting: main_start=$adjusted_main_start, spare_start=$adjusted_spare_start, spare_total=$adjusted_spare_total" >> "$logfile"
fi

# === Create mapping ===
SECTORS=$(blockdev --getsz "$main_dev")
echo "Creating device-mapper target..."
echo "dmsetup create $dm_name with: 0 $SECTORS remap $main_dev $adjusted_main_start $spare_dev $adjusted_spare_start $adjusted_spare_total"
echo "0 $SECTORS remap $main_dev $adjusted_main_start $spare_dev $adjusted_spare_start $adjusted_spare_total" \
    | sudo dmsetup create "$dm_name"

echo "Mapping created successfully via wrapper."
echo "=== END WRAPPER ===" >> "$logfile"

exit $ADJUSTED_FLAG
