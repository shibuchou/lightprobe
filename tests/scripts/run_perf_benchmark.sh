#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJ_DIR"

ITERS=${LIGHTPROBE_BENCH_ITERS:-1000000}
ROUNDS=${LIGHTPROBE_BENCH_ROUNDS:-10}
WARMUP=${LIGHTPROBE_BENCH_WARMUP:-2}
BENCH_BIN="./build/tests/target_getpid_bench"
LP_BIN="./build/lightprobe"
RESULT_DIR="/tmp/lightprobe_benchmark"
mkdir -p "$RESULT_DIR"

password=${LIGHTPROBE_SUDO_PASSWORD:-}

run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@" 2>/dev/null
    else
        sudo "$@" 2>/dev/null
    fi
}

require_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "SKIP: requires root (use LIGHTPROBE_SUDO_PASSWORD or run as root)" >&2
        return 1
    fi
    return 0
}

find_libc() {
    local libc
    libc=$(ldd "$BENCH_BIN" 2>/dev/null | grep -oP '/\S+libc\S*\.so\S*' | head -1) || true
    if [[ -z "$libc" ]]; then
        libc=$(ldconfig -p 2>/dev/null | grep -oP '/\S+libc\.so\S*' | head -1) || true
    fi
    if [[ -z "$libc" ]]; then
        for p in /lib/x86_64-linux-gnu/libc.so.6 /lib64/libc.so.6 /usr/lib/libc.so.6; do
            if [[ -f "$p" ]]; then libc="$p"; break; fi
        done
    fi
    echo "$libc"
}

find_getpid_offset() {
    local libc=$1
    local offset
    offset=$(readelf -s "$libc" 2>/dev/null | awk '/ getpid@/ && /FUNC/ {print $2; exit}') || true
    if [[ -z "$offset" ]]; then
        offset=$(nm -D "$libc" 2>/dev/null | awk '/ getpid$/ {print "0x"$1; exit}') || true
    fi
    echo "$offset"
}

setup_uprobe_tracefs() {
    local libc=$1
    local offset=$2
    local uprobe_event="/sys/kernel/tracing/uprobe_events"
    local uprobe_enable="/sys/kernel/tracing/events/uprobes/lpb_getpid/enable"

    if ! run_sudo test -f "$uprobe_event"; then
        echo "TRACEFS: $uprobe_event not found" >&2
        return 1
    fi

    run_sudo bash -c "echo 'p:lpb_getpid $libc:$offset' > $uprobe_event" || {
        echo "TRACEFS: failed to add uprobe" >&2
        return 1
    }

    if [[ -f "$uprobe_enable" ]]; then
        run_sudo bash -c "echo 1 > $uprobe_enable" || true
    fi

    echo "TRACEFS: uprobe set on getpid at $libc+$offset"
    return 0
}

cleanup_uprobe_tracefs() {
    local uprobe_event="/sys/kernel/tracing/uprobe_events"
    if [[ -f "$uprobe_event" ]]; then
        run_sudo bash -c "echo > $uprobe_event" 2>/dev/null || true
    fi
}

setup_uprobe_perf() {
    local libc=$1
    if ! command -v perf &>/dev/null; then
        echo "PERF: perf not found" >&2
        return 1
    fi
    run_sudo perf probe -x "$libc" getpid 2>/dev/null || {
        echo "PERF: failed to add probe" >&2
        return 1
    }
    echo "PERF: probe set on getpid"
    return 0
}

cleanup_uprobe_perf() {
    if command -v perf &>/dev/null; then
        run_sudo perf probe -d probe_getpid 2>/dev/null || true
    fi
}

run_bench() {
    local label=$1
    echo ""
    echo "=== Group: $label ===" >&2
    "$BENCH_BIN" --label "$label" --iters "$ITERS" --rounds "$ROUNDS" --warmup "$WARMUP"
}

run_lightprobe_bench() {
    local label=$1
    local use_ret=$2
    local outfile="/tmp/lightprobe_bench_${label}.json"

    echo ""
    echo "=== Group: $label ===" >&2

    run_sudo rm -f /tmp/lightprobe_state*.bin
    rm -f "$outfile"

    "$BENCH_BIN" --label "$label" --iters "$ITERS" --rounds "$ROUNDS" --warmup "$WARMUP" --wait >"$outfile" 2>&1 &
    local target_pid=$!

    sleep 1

    local ret_flag=""
    if [[ "$use_ret" == "1" ]]; then
        ret_flag="--ret"
    fi

    run_sudo "$LP_BIN" attach --pid "$target_pid" --lib libc.so.6 --func getpid $ret_flag || {
        echo "ERROR: attach failed" >&2
        kill "$target_pid" 2>/dev/null || true
        wait "$target_pid" 2>/dev/null || true
        cat "$outfile" 2>/dev/null || true
        return 1
    }

    kill -SIGUSR1 "$target_pid" || true

    wait "$target_pid" || true

    run_sudo "$LP_BIN" detach --pid "$target_pid" --func getpid 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state*.bin

    cat "$outfile" 2>/dev/null
    rm -f "$outfile"
}

extract_avg() {
    grep -oP '"avg_ns_per_call":\K[0-9.]+' | head -1
}

extract_field() {
    local key=$1
    grep -oP "\"${key}\":\K[0-9.]+" | head -1
}

lp_check_sudo() {
    if [[ $EUID -ne 0 ]] && [[ -z "$password" ]]; then
        echo "SKIP: lightprobe requires sudo (set LIGHTPROBE_SUDO_PASSWORD or run as root)" >&2
        return 1
    fi
    return 0
}

echo "=== lightprobe Performance Benchmark ===" >&2
echo "iters=$ITERS rounds=$ROUNDS warmup=$WARMUP" >&2
echo "" >&2

make targets 2>/dev/null || {
    echo "ERROR: build failed" >&2
    exit 1
}

if [[ ! -x "$BENCH_BIN" ]]; then
    echo "ERROR: $BENCH_BIN not found" >&2
    exit 1
fi

if [[ ! -x "$LP_BIN" ]]; then
    echo "ERROR: $LP_BIN not found, run 'make' first" >&2
    exit 1
fi

LIBC=$(find_libc)
echo "libc: $LIBC" >&2

declare -A AVG_RESULTS
UPRORE_OK=0

# Group A: baseline
RESULT_A=$(run_bench "baseline")
echo "$RESULT_A" | tee "$RESULT_DIR/baseline.json"
AVG_A=$(echo "$RESULT_A" | extract_avg)
AVG_RESULTS["baseline"]="$AVG_A"

# Group B: uprobe
GETPID_OFFSET=$(find_getpid_offset "$LIBC")
if [[ -n "$GETPID_OFFSET" ]]; then
    UPROBE_SET=0
    if setup_uprobe_tracefs "$LIBC" "$GETPID_OFFSET"; then
        UPROBE_SET=1
    elif setup_uprobe_perf "$LIBC"; then
        UPROBE_SET=1
    fi

    if [[ "$UPROBE_SET" == "1" ]]; then
        RESULT_B=$(run_bench "uprobe")
        echo "$RESULT_B" | tee "$RESULT_DIR/uprobe.json"
        AVG_B=$(echo "$RESULT_B" | extract_avg)
        AVG_RESULTS["uprobe"]="$AVG_B"
        UPRORE_OK=1
        cleanup_uprobe_tracefs
        cleanup_uprobe_perf
    else
        echo "SKIP: uprobe setup failed (no tracefs/perf/bpftrace available)" >&2
        AVG_RESULTS["uprobe"]="N/A"
    fi
else
    echo "SKIP: cannot resolve getpid offset in libc" >&2
    AVG_RESULTS["uprobe"]="N/A"
fi

# Group C: lightprobe entry-only
if lp_check_sudo; then
    RESULT_C=$(run_lightprobe_bench "lightprobe-entry" 0)
    echo "$RESULT_C" | tee "$RESULT_DIR/lightprobe_entry.json"
    AVG_C=$(echo "$RESULT_C" | extract_avg)
    AVG_RESULTS["lightprobe-entry"]="$AVG_C"
else
    AVG_RESULTS["lightprobe-entry"]="N/A"
fi

# Group D: lightprobe entry+return
if lp_check_sudo; then
    RESULT_D=$(run_lightprobe_bench "lightprobe-entry-return" 1)
    echo "$RESULT_D" | tee "$RESULT_DIR/lightprobe_entry_return.json"
    AVG_D=$(echo "$RESULT_D" | extract_avg)
    AVG_RESULTS["lightprobe-entry-return"]="$AVG_D"
else
    AVG_RESULTS["lightprobe-entry-return"]="N/A"
fi

echo ""
echo "=============================================="
echo " Benchmark Comparison (avg ns per getpid call)"
echo "=============================================="
printf "%-28s %s\n" "Mode" "avg_ns_per_call"
printf "%-28s %s\n" "----------------------------" "---------------"
printf "%-28s %s\n" "baseline" "${AVG_RESULTS[baseline]:-N/A}"
printf "%-28s %s\n" "uprobe" "${AVG_RESULTS[uprobe]:-N/A}"
printf "%-28s %s\n" "lightprobe-entry" "${AVG_RESULTS[lightprobe-entry]:-N/A}"
printf "%-28s %s\n" "lightprobe-entry-return" "${AVG_RESULTS[lightprobe-entry-return]:-N/A}"

if [[ -n "${AVG_RESULTS[baseline]:-}" ]] && [[ -n "${AVG_RESULTS[lightprobe-entry]:-}" ]]; then
    OVERHEAD=$(awk "BEGIN { printf \"%.2f\", ${AVG_RESULTS[lightprobe-entry]} - ${AVG_RESULTS[baseline]} }")
    printf "\nlightprobe overhead: %s ns/call\n" "$OVERHEAD"

    if [[ "$UPRORE_OK" == "1" ]] && [[ -n "${AVG_RESULTS[uprobe]:-}" ]]; then
        UPROBE_OVERHEAD=$(awk "BEGIN { printf \"%.2f\", ${AVG_RESULTS[uprobe]} - ${AVG_RESULTS[baseline]} }")
        printf "uprobe overhead:     %s ns/call\n" "$UPROBE_OVERHEAD"
    fi
fi

echo ""
echo "Results saved to $RESULT_DIR/"
