#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../.." && pwd)"

password=${LIGHTPROBE_SUDO_PASSWORD:-}
run_sudo() {
    if [[ -n "$password" ]]; then
        printf '%s\n' "$password" | sudo -S "$@" 2>/dev/null
    else
        sudo "$@"
    fi
}

echo "building targets..."
make targets

declare -A TARGET_MAP
TARGET_MAP[getpid]="./build/tests/target_getpid_loop"
TARGET_MAP[malloc]="./build/tests/target_malloc_loop"
TARGET_MAP[strlen]="./build/tests/target_strlen_loop"

FUNCS=(getpid malloc strlen)
RUNS=10

echo "=== lightprobe attach/detach latency ==="

for func in "${FUNCS[@]}"; do
    target="${TARGET_MAP[$func]}"
    latencies=()

    for ((i = 1; i <= RUNS; i++)); do
        run_sudo rm -f /tmp/lightprobe_state*.bin

        "$target" >/dev/null 2>&1 &
        target_pid=$!
        sleep 0.5

        start_ns=$(date +%s%N)
        run_sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func "$func" --ret
        run_sudo ./build/lightprobe detach --pid "$target_pid" --func "$func"
        end_ns=$(date +%s%N)

        latency_ns=$((end_ns - start_ns))
        latencies+=("$latency_ns")

        kill "$target_pid" >/dev/null 2>&1 || true
        wait "$target_pid" 2>/dev/null || true
    done

    min_ns=${latencies[0]}
    max_ns=${latencies[0]}
    sum_ns=0
    for lat_ns in "${latencies[@]}"; do
        sum_ns=$((sum_ns + lat_ns))
        if ((lat_ns < min_ns)); then min_ns=$lat_ns; fi
        if ((lat_ns > max_ns)); then max_ns=$lat_ns; fi
    done
    avg_ns=$((sum_ns / RUNS))

    min_ms=$(awk "BEGIN {printf \"%.3f\", $min_ns / 1000000}")
    avg_ms=$(awk "BEGIN {printf \"%.3f\", $avg_ns / 1000000}")
    max_ms=$(awk "BEGIN {printf \"%.3f\", $max_ns / 1000000}")

    echo "func=$func runs=$RUNS min_ms=$min_ms avg_ms=$avg_ms max_ms=$max_ms"
    run_sudo rm -f /tmp/lightprobe_state*.bin
done
