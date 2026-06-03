#!/usr/bin/env bash
set -euo pipefail

cd /home/shibuchou/Desktop/lightprobe
password=${1:-sbc}
printf '%s\n' "$password" | sudo -S rm -f /tmp/lightprobe_state.bin
./build/tests/target_malloc_loop > /tmp/lightprobe_malloc_loop.log 2>&1 &
target_pid=$!
cleanup() {
    printf '%s\n' "$password" | sudo -S ./build/lightprobe detach --pid "$target_pid" --func malloc >/dev/null 2>&1 || true
    kill "$target_pid" >/dev/null 2>&1 || true
    wait "$target_pid" 2>/dev/null || true
    printf '%s\n' "$password" | sudo -S rm -f /tmp/lightprobe_state.bin >/dev/null 2>&1 || true
}
trap cleanup EXIT
sleep 1
printf '%s\n' "$password" | sudo -S ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func malloc --ret
sleep 1
printf '%s\n' "$password" | sudo -S ./build/lightprobe events --pid "$target_pid" --func malloc --limit 8
printf '%s\n' "$password" | sudo -S ./build/lightprobe detach --pid "$target_pid" --func malloc
./build/lightprobe list