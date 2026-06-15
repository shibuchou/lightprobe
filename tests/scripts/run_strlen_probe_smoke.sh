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

run_sudo rm -f /tmp/lightprobe_state.bin
./build/tests/target_strlen_loop > /tmp/lightprobe_target_strlen_loop.log 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func strlen >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
header=$(head -n 1 /tmp/lightprobe_target_strlen_loop.log)
target_addr=$(printf '%s\n' "$header" | sed -n 's/.*strlen_addr=\(0x[0-9a-fA-F]*\).*/\1/p')
if [[ -z "$target_addr" ]]; then
    echo "failed to resolve strlen target address from: $header" >&2
    exit 1
fi

run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func strlen --addr "$target_addr" --ret
sleep 1
run_sudo ./build/lightprobe events --pid "$target_pid" --func strlen --limit 8
run_sudo ./build/lightprobe detach --pid "$target_pid" --func strlen
./build/lightprobe list
