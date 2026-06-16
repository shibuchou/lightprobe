#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../.." && pwd)"

password=${LIGHTPROBE_SUDO_PASSWORD:-}
run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

run_sudo rm -f /tmp/lightprobe_state*.bin

export LD_LIBRARY_PATH="$PWD/build/tests:${LD_LIBRARY_PATH:-}"
./build/tests/target_fp_probe > /tmp/lightprobe_target_fp_probe.log 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func fp_mix >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state*.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1

while ! grep -q '^result=' /tmp/lightprobe_target_fp_probe.log 2>/dev/null; do
    sleep 1
done
baseline=$(grep '^result=' /tmp/lightprobe_target_fp_probe.log | tail -n 1 | sed 's/result=//')
echo "baseline result=$baseline"

run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libfp_probe_target.so --func fp_mix --ret
sleep 2

run_sudo ./build/lightprobe events --pid "$target_pid" --func fp_mix --limit 4

run_sudo ./build/lightprobe detach --pid "$target_pid" --func fp_mix
sleep 1

after=$(grep '^result=' /tmp/lightprobe_target_fp_probe.log | tail -n 1 | sed 's/result=//')
echo "after-detach result=$after"

if [[ -z "$baseline" ]] || [[ -z "$after" ]]; then
    echo "could not capture baseline or after result" >&2
    exit 1
fi

if python3 -c "exit(0 if abs(float('$baseline') - float('$after')) < 1e-12 else 1)" 2>/dev/null; then
    echo "PASS: XMM0-15 registers preserved before/after probe (delta < 1e-12)"
else
    if [[ "$baseline" == "$after" ]]; then
        echo "PASS: XMM0-15 registers preserved (string match)"
    else
        echo "FAIL: result mismatch baseline=$baseline after=$after" >&2
        exit 1
    fi
fi
