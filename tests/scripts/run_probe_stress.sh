#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../.." && pwd)"

func=${1:-strlen}
duration=${2:-8}
threads=${3:-8}
sleep_ns=${4:-100000}
password=${LIGHTPROBE_SUDO_PASSWORD:-}

run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

case "$func" in
    strlen|malloc)
        ;;
    *)
        echo "supported stress functions: strlen, malloc" >&2
        exit 2
        ;;
esac

run_sudo rm -f /tmp/lightprobe_state.bin
./build/tests/target_probe_stress "$threads" "$sleep_ns" > /tmp/lightprobe_stress.log 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func "$func" >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
attach_args=(--pid "$target_pid" --lib libc.so.6 --func "$func" --ret)
if [[ "$func" == "strlen" ]]; then
    header=$(head -n 1 /tmp/lightprobe_stress.log)
    target_addr=$(printf '%s\n' "$header" | sed -n 's/.*strlen_addr=\(0x[0-9a-fA-F]*\).*/\1/p')
    if [[ -z "$target_addr" ]]; then
        echo "failed to resolve strlen target address from: $header" >&2
        exit 1
    fi
    attach_args=(--pid "$target_pid" --lib libc.so.6 --func "$func" --addr "$target_addr" --ret)
fi
run_sudo ./build/lightprobe attach "${attach_args[@]}"
sleep "$duration"
echo "stress target pid=$target_pid func=$func duration=${duration}s threads=$threads sleep_ns=$sleep_ns"
run_sudo ./build/lightprobe events --pid "$target_pid" --func "$func" --limit 32
run_sudo ./build/lightprobe events --pid "$target_pid" --func "$func" --limit 4096 --csv > "/tmp/lightprobe_${func}_stress.csv"
echo "csv=/tmp/lightprobe_${func}_stress.csv"
run_sudo ./build/lightprobe detach --pid "$target_pid" --func "$func"
./build/lightprobe list
