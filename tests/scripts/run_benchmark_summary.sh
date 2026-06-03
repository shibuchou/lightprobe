#!/usr/bin/env bash
set -euo pipefail

cd /home/shibuchou/Desktop/lightprobe

func=${1:-strlen}
duration=${2:-8}
threads=${3:-8}
sleep_ns=${4:-100000}

./tests/scripts/run_probe_stress.sh "$func" "$duration" "$threads" "$sleep_ns"
csv="/tmp/lightprobe_${func}_stress.csv"

awk -F, -v probe_func="$func" -v duration="$duration" '
NR == 1 { next }
{
    total++
    tids[$3] = 1
    if ($5 == 1) {
        entry++
    } else if ($5 == 2) {
        ret++
        sum += $13
        if (min == 0 || $13 < min) min = $13
        if ($13 > max) max = $13
    }
}
END {
    for (tid in tids) tid_count++
    avg = ret ? sum / ret : 0
    printf("benchmark func=%s duration=%ss events=%d entry=%d return=%d tids=%d return_avg_ns=%.0f return_min_ns=%d return_max_ns=%d\n",
           probe_func, duration, total, entry, ret, tid_count, avg, min, max)
}
' "$csv"
