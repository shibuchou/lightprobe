#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJ_DIR"

NFUNCS=16
LIB_PATH="$PROJ_DIR/build/tests/libmulti_probe_target.so"
TARGET_BIN="./build/tests/target_multi_func"
LP_BIN="./build/lightprobe"
password=${LIGHTPROBE_SUDO_PASSWORD:-}

echo "=== lightprobe 16-probe concurrency smoke test ==="

run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@" 2>/dev/null
    else
        sudo "$@"
    fi
}

make targets 2>&1 || { echo "BUILD FAILED"; exit 1; }

if [[ ! -x "$TARGET_BIN" ]] || [[ ! -f "$LIB_PATH" ]]; then
    echo "ERROR: target or library not found"
    exit 1
fi

run_sudo rm -f /tmp/lightprobe_state*.bin

export LD_LIBRARY_PATH="$PROJ_DIR/build/tests:${LD_LIBRARY_PATH:-}"
"$TARGET_BIN" >/tmp/lightprobe_16probe_target.log 2>&1 &
TARGET_PID=$!
echo "target pid=$TARGET_PID"

cleanup() {
    echo ""
    echo "[cleanup] detaching all probes..."
    for ((i=0; i<NFUNCS; i++)); do
        fname=$(printf "f%02d" $((i+1)))
        run_sudo "$LP_BIN" detach --pid "$TARGET_PID" --func "$fname" >/dev/null 2>&1 || true
    done
    kill "$TARGET_PID" >/dev/null 2>&1 || true
    wait "$TARGET_PID" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state*.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1

FAILURES=0

echo ""
echo "--- Attaching $NFUNCS probes ---"
for ((i=0; i<NFUNCS; i++)); do
    fname=$(printf "f%02d" $((i+1)))
    if run_sudo "$LP_BIN" attach --pid "$TARGET_PID" --lib libmulti_probe_target.so --func "$fname" --ret 2>/dev/null; then
        echo "  [$((i+1))/$NFUNCS] attached $fname OK"
    else
        echo "  [$((i+1))/$NFUNCS] attached $fname FAILED"
        ((FAILURES++))
    fi
done

sleep 1

echo ""
echo "--- Verifying events per function ---"
TOTAL_PROBE_IDS=0
TOTAL_ENTRY=0
TOTAL_RET=0

for ((i=0; i<NFUNCS; i++)); do
    fname=$(printf "f%02d" $((i+1)))
    EVENTS_OUTPUT=$(run_sudo "$LP_BIN" events --pid "$TARGET_PID" --func "$fname" --limit 4 2>&1 || true)
    
    probe_id=$(echo "$EVENTS_OUTPUT" | grep -oP 'probe_id=\K\d+' | head -1 || true)
    entry_count=$(echo "$EVENTS_OUTPUT" | grep -c 'type=entry' || true)
    ret_count=$(echo "$EVENTS_OUTPUT" | grep -c 'type=return' || true)
    
    if [[ -n "$probe_id" ]] && [[ "$entry_count" -gt 0 ]] && [[ "$ret_count" -gt 0 ]]; then
        TOTAL_PROBE_IDS=$((TOTAL_PROBE_IDS + 1))
        TOTAL_ENTRY=$((TOTAL_ENTRY + entry_count))
        TOTAL_RET=$((TOTAL_RET + ret_count))
    else
        echo "  [WARN] $fname: probe_id=$probe_id entry=$entry_count ret=$ret_count"
    fi
done

echo "Functions with events: $TOTAL_PROBE_IDS / $NFUNCS"
echo "Total entry events: $TOTAL_ENTRY, return events: $TOTAL_RET"

if [[ "$TOTAL_PROBE_IDS" -ge "$NFUNCS" ]]; then
    echo "  [PASS] all 16 functions have entry+return events"
else
    echo "  [FAIL] expected $NFUNCS functions with events, got $TOTAL_PROBE_IDS"
    ((FAILURES++))
fi

echo ""
echo "--- Detaching all probes ---"
DETACH_FAILURES=0
for ((i=0; i<NFUNCS; i++)); do
    fname=$(printf "f%02d" $((i+1)))
    if run_sudo "$LP_BIN" detach --pid "$TARGET_PID" --func "$fname" 2>/dev/null; then
        :
    else
        echo "  detach $fname FAILED"
        ((DETACH_FAILURES++))
    fi
done

if [[ "$DETACH_FAILURES" -eq 0 ]]; then
    echo "  [PASS] all 16 probes detached"
else
    echo "  [FAIL] $DETACH_FAILURES detach failures"
    ((FAILURES += DETACH_FAILURES))
fi

echo ""
echo "--- Final list check ---"
LIST_OUTPUT=$("./build/lightprobe" list 2>&1 || true)
if echo "$LIST_OUTPUT" | grep -q 'ID'; then
    DATA_LINES=$(echo "$LIST_OUTPUT" | grep -cv 'ID' || true)
    if [[ "$DATA_LINES" -eq 0 ]]; then
        echo "  [PASS] list clean, no residual probes"
    else
        echo "  [FAIL] $DATA_LINES residual probe(s) in list"
        ((FAILURES++))
    fi
else
    echo "  [PASS] list empty"
fi

echo ""
if [[ "$FAILURES" -eq 0 ]]; then
    echo "=== RESULT: ALL CHECKS PASSED ==="
else
    echo "=== RESULT: $FAILURES FAILURE(S) ==="
    exit 1
fi
