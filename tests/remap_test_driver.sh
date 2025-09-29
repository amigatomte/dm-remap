#!/bin/bash

# remap_test_driver.sh
#     The unified single/batch harness that:
#         Calls the wrapper for every mapping creation.
#         Runs your fio workload and remap table monitor.
#         Supports single run or batch mode.
#         In batch mode, can pause between runs, preview the next test, and let you quit early.
#         Produces a summary at the end — even for partial batches — listing parameters and log file names for each completed run.

set -euo pipefail

MODULE=dm-remap
PRIMARY_IMG=/tmp/primary_sparse.img
SPARE_IMG=/tmp/spare_sparse.img
WRAPPER=/usr/local/bin/remap_create_safe.sh

# === DEFAULTS for single run ===
SIZE_MB=10
RUNTIME=10
BLOCK_SIZE=4k
DM_NAME="test_remap"

# === BATCH TESTS ===
TESTS=(
  "10 10 4k"
  "50 20 8k"
  "100 30 16k"
)

PAUSE_BETWEEN=false
COMPLETED_RUNS=()

usage() {
    echo "Usage:"
    echo "  $0 single [size_mb] [runtime_sec] [block_size] [--dm-name=name]"
    echo "  $0 batch [--pause] [--dm-name-prefix=prefix]"
    exit 1
}

run_test() {
    local SIZE_MB=$1
    local RUNTIME=$2
    local BLOCK_SIZE=$3
    local DM_NAME_LOCAL=$4
    local SECTORS=$((SIZE_MB * 2048))
    local LOGFILE="remap_changes_${SIZE_MB}MB_${RUNTIME}s_${BLOCK_SIZE}_$(date +%Y%m%d_%H%M%S).log"

    echo
    echo "=== Running test: ${SIZE_MB}MB, ${RUNTIME}s, ${BLOCK_SIZE}, dm_name=${DM_NAME_LOCAL} ==="
    echo "[+] Logging to $LOGFILE"

    {
    echo "=== DM-REMAPP TEST RUN ==="
    echo "Date: $(date)"
    echo "Module: $MODULE"
    echo "Primary image: $PRIMARY_IMG"
    echo "Spare image: $SPARE_IMG"
    echo "Size: ${SIZE_MB}MB (${SECTORS} sectors)"
    echo "fio runtime: ${RUNTIME}s"
    echo "fio block size: ${BLOCK_SIZE}"
    echo "dm_name: ${DM_NAME_LOCAL}"
    echo
    } >> "$LOGFILE"

    if lsmod | grep -q "^${MODULE//-/_}"; then
        sudo rmmod $MODULE || true
    fi
    sudo insmod ./${MODULE}.ko

    truncate -s ${SIZE_MB}M "$PRIMARY_IMG"
    truncate -s ${SIZE_MB}M "$SPARE_IMG"

    LOOP_PRIMARY=$(sudo losetup -f --show "$PRIMARY_IMG")
    LOOP_SPARE=$(sudo losetup -f --show "$SPARE_IMG")

    ADJUSTED_RUN=false
    if sudo "$WRAPPER" main_dev="$LOOP_PRIMARY" main_start=0 spare_dev="$LOOP_SPARE" spare_start=0 spare_total="$SECTORS" logfile="$LOGFILE" dm_name="$DM_NAME_LOCAL"; then
        ADJUSTED_RUN=false
    else
        rc=$?
        if [[ $rc -eq 1 ]]; then
            ADJUSTED_RUN=true
        elif [[ $rc -eq 2 ]]; then
            echo "[!] Sector size mismatch — aborting run" | tee -a "$LOGFILE"
            sudo losetup -d "$LOOP_PRIMARY"
            sudo losetup -d "$LOOP_SPARE"
            sudo rmmod $MODULE
            return
        else
            echo "[!] Wrapper failed with unexpected code $rc" | tee -a "$LOGFILE"
            return
        fi
    fi

    sudo cat /sys/kernel/debug/dm_remap/remap_table || true >> "$LOGFILE"

    if ! command -v fio >/dev/null 2>&1; then
        sudo apt-get update && sudo apt-get install -y fio
    fi

    (
        prev=""
        while true; do
            current=$(sudo cat /sys/kernel/debug/dm_remap/remap_table 2>/dev/null || true)
            echo "=== remap_table @ $(date +%T) ===" >> "$LOGFILE"
            if [[ -z "$prev" ]]; then
                echo "$current" >> "$LOGFILE"
            else
                while IFS= read -r line; do
                    if ! grep -Fxq "$line" <<< "$prev"; then
                        echo "[ADDED] $line" >> "$LOGFILE"
                    fi
                done <<< "$current"
                while IFS= read -r line; do
                    if ! grep -Fxq "$line" <<< "$current"; then
                        echo "[REMOVED] $line" >> "$LOGFILE"
                    fi
                done <<< "$prev"
            fi
            prev="$current"
            sleep 1
        done
    ) &
    MON_PID=$!

    {
        echo
        echo "=== FIO OUTPUT ==="
        sudo fio --name=remap_test \
                 --filename=/dev/mapper/$DM_NAME_LOCAL \
                 --direct=1 \
                 --rw=randrw \
                 --bs=${BLOCK_SIZE} \
                 --size=${SIZE_MB}M \
                 --numjobs=1 \
                 --time_based \
                 --runtime=${RUNTIME} \
                 --group_reporting
        echo "=== END FIO OUTPUT ==="
    } | tee -a "$LOGFILE"

    kill $MON_PID || true
    wait $MON_PID 2>/dev/null || true

    sudo cat /sys/kernel/debug/dm_remap/remap_table || true >> "$LOGFILE"

    {
        echo
        echo "=== DMESG OUTPUT ==="
        sudo dmesg | tail -n 30
        echo "=== END DMESG OUTPUT ==="
    } >> "$LOGFILE"

    sudo dmsetup remove "$DM_NAME_LOCAL"
    sudo losetup -d "$LOOP_PRIMARY"
    sudo losetup -d "$LOOP_SPARE"
    sudo rmmod $MODULE

    {
        echo
        echo "=== FINAL DMESG OUTPUT ==="
        sudo dmesg | tail -n 20
        echo "=== END FINAL DMESG OUTPUT ==="
    } >> "$LOGFILE"

    echo
    echo "=== SUMMARY for ${SIZE_MB}MB, ${RUNTIME}s, ${BLOCK_SIZE}, dm_name=${DM_NAME_LOCAL} ==="
    FIRST_TS=$(grep -m1 "^=== remap_table @" "$LOGFILE" | sed 's/^=== remap_table @ //')
    LAST_TS=$(grep "^=== remap_table @" "$LOGFILE" | tail -n1 | sed 's/^=== remap_table @ //')
    echo "First snapshot: $FIRST_TS"
    echo "Last snapshot:  $LAST_TS"
    echo "Total added lines:   $(grep -c '^\[ADDED\]' "$LOGFILE" || true)"
    echo "Total removed lines: $(grep -c '^\[REMOVED\]' "$LOGFILE" || true)"
    if $ADJUSTED_RUN; then
        echo "NOTE: Mapping parameters were auto‑adjusted for alignment"
    fi
    echo "Log file: $LOGFILE"
    echo "=============================="

    COMPLETED_RUNS+=("${SIZE_MB}MB ${RUNTIME}s ${BLOCK_SIZE} dm_name=${DM_NAME_LOCAL} | $LOGFILE")
}

MODE=${1:-}
case "$MODE" in
    single)
        shift
        while [[ $# -gt 0 ]]; do
            case "$1" in
                --dm-name=*) DM_NAME="${1#*=}" ;;
                *) break ;;
            esac
            shift
        done
        SIZE_MB=${1:-$SIZE_MB}
        RUNTIME=${2:-$RUNTIME}
        BLOCK_SIZE=${3:-$BLOCK_SIZE}
        run_test "$SIZE_MB" "$RUNTIME" "$BLOCK_SIZE" "$DM_NAME"
        ;;
    batch)
        shift
        DM_NAME_PREFIX="remap"
        while [[ $# -gt 0 ]]; do
            case "$1" in
                --pause) PAUSE_BETWEEN=true ;;
                --dm-name-prefix=*) DM_NAME_PREFIX="${1#*=}" ;;
            esac
            shift
        done
        for i in "${!TESTS[@]}"; do
            read_size=$(echo "${TESTS[$i]}" | awk '{print $1}')
            read_runtime=$(echo "${TESTS[$i]}" | awk '{print $2}')
            read_bs=$(echo "${TESTS[$i]}" | awk '{print $3}')
            auto_dm_name="${DM_NAME_PREFIX}_${read_size}MB_${read_runtime}s_${read_bs}"
            run_test "$read_size" "$read_runtime" "$read_bs" "$auto_dm_name"
            if $PAUSE_BETWEEN && [[ $i -lt $((${#TESTS[@]} - 1)) ]]; then
                next_test=${TESTS[$((i+1))]}
                read -rp "Press Enter to continue to next test: $next_test"
            fi
        done
        ;;
    *)
        usage
        ;;
esac