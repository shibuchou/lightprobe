#!/usr/bin/env bash
set -euo pipefail

cd /home/shibuchou/Desktop/lightprobe

password=${LIGHTPROBE_SUDO_PASSWORD:-${1:-}}
run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

run_sudo rm -f /tmp/lightprobe_state.bin
./build/tests/target_getpid_loop > /tmp/lightprobe_getpid_loop.log 2>&1 &
target_pid=$!

cleanup() {
    run_sudo ./build/lightprobe detach --pid "$target_pid" --func getpid >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    run_sudo rm -f /tmp/lightprobe_state.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 1
run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func getpid --ret
sleep 1
run_sudo ./build/lightprobe events --pid "$target_pid" --func getpid --limit 8
run_sudo ./build/lightprobe detach --pid "$target_pid" --func getpid
./build/lightprobe list
