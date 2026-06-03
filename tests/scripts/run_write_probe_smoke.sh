#!/usr/bin/env bash
set -euo pipefail

cd /home/shibuchou/Desktop/lightprobe
exec ./tests/scripts/run_probe_smoke.sh ./build/tests/target_write_loop write 8
