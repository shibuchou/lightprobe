#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../.." && pwd)"

duration=${1:-15}
password=${LIGHTPROBE_SUDO_PASSWORD:-}

if [[ "$duration" -lt 6 ]]; then
    echo "duration must be >= 6 seconds" >&2
    exit 2
fi

run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

echo "=== building targets ==="
make targets

run_sudo rm -f /tmp/lightprobe_state*.bin
./build/tests/target_signal_stress "$duration" > /tmp/lightprobe_signal_stress.log 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func getpid >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state*.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
echo "=== attaching to pid=$target_pid ==="
run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func getpid --ret

collect_after=$((duration - 5))
echo "=== sleeping ${collect_after}s (collecting events before target exits) ==="
sleep "$collect_after"

echo "=== collecting events ==="
run_sudo ./build/lightprobe events --pid "$target_pid" --func getpid --limit 4096 --csv > /tmp/lightprobe_signal_stress.csv

echo "=== waiting for stress test to finish ==="
wait "$target_pid"
target_exit=$?

echo "=== verifying results ==="
entry_count=$(awk -F, 'NR>1 && $5==1 {c++} END{print c+0}' /tmp/lightprobe_signal_stress.csv)
return_count=$(awk -F, 'NR>1 && $5==2 {c++} END{print c+0}' /tmp/lightprobe_signal_stress.csv)
echo "entry_count=$entry_count return_count=$return_count"

if [[ "$entry_count" -eq 0 ]]; then
    echo "FAIL: no events collected"
    exit 1
fi

if [[ "$entry_count" -ne "$return_count" ]]; then
    echo "FAIL: entry_count ($entry_count) != return_count ($return_count)"
    exit 1
fi

if [[ "$target_exit" -ne 0 ]]; then
    echo "FAIL: target exited with code $target_exit (expected 0)"
    exit 1
fi

echo "=== stress test log ==="
cat /tmp/lightprobe_signal_stress.log

echo "=== detaching ==="
run_sudo ./build/lightprobe detach --pid "$target_pid" --func getpid
./build/lightprobe list

echo "PASS: signal stress test completed"
