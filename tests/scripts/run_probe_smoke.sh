#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ $# -lt 3 ]]; then
    echo "usage: $0 <target-binary> <func> <limit> [target-arg...]" >&2
    exit 2
fi

target_bin=$1
func=$2
limit=$3
shift 3

password=${LIGHTPROBE_SUDO_PASSWORD:-}
run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

log_name=${target_bin##*/}
run_sudo rm -f /tmp/lightprobe_state.bin
"$target_bin" "$@" > "/tmp/lightprobe_${log_name}.log" 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func "$func" >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func "$func" --ret
sleep 1
run_sudo ./build/lightprobe events --pid "$target_pid" --func "$func" --limit "$limit"
run_sudo ./build/lightprobe detach --pid "$target_pid" --func "$func"
./build/lightprobe list
