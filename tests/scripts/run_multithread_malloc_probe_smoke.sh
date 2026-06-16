#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../.." && pwd)"

password=${LIGHTPROBE_SUDO_PASSWORD:-${1:-}}
run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

run_sudo rm -f /tmp/lightprobe_state*.bin
./build/tests/target_multithread_malloc > /tmp/lightprobe_multithread_malloc.log 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func malloc >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state*.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func malloc --ret
sleep 2
run_sudo ./build/lightprobe events --pid "$target_pid" --func malloc --limit 16
run_sudo ./build/lightprobe detach --pid "$target_pid" --func malloc
./build/lightprobe list
