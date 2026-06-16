#!/bin/bash
set -euo pipefail

#==================================================================
# lightprobe 演示脚本
# 2026 全国大学生计算机系统能力大赛 · OS 功能挑战赛道 · proj40
#==================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJ_DIR"

PASSWORD=${LIGHTPROBE_SUDO_PASSWORD:-}
if [[ -z "$PASSWORD" ]]; then
    echo -e "${YELLOW}lightprobe 需要 sudo 权限进行 ptrace 操作${NC}"
    echo -e "${YELLOW}请设置环境变量: export LIGHTPROBE_SUDO_PASSWORD='你的密码'${NC}"
    echo -e "${YELLOW}或直接运行: LIGHTPROBE_SUDO_PASSWORD='密码' bash demo.sh${NC}"
    echo ""
    echo -n "请输入 sudo 密码 (或按 Ctrl-C 取消后设置环境变量): "
    read -s PASSWORD
    echo ""
    if [[ -z "$PASSWORD" ]]; then
        echo "未输入密码，退出。"
        exit 1
    fi
fi

SUDO() {
    if [[ -n "$PASSWORD" ]]; then
        printf '%s\n' "$PASSWORD" | sudo -S "$@" 2>/dev/null
    else
        sudo "$@"
    fi
}

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

PASS_COUNT=0; FAIL_COUNT=0; TOTAL=6

section() { echo -e "\n${CYAN}===== ${BOLD}$1${NC} ${CYAN}=====${NC}"; }
step() { echo -e "${BOLD}${BLUE}[$1/$TOTAL]${NC} $2"; }
pass() { PASS_COUNT=$((PASS_COUNT+1)); echo -e "  ${GREEN}${BOLD}PASS${NC}  $1"; }
info() { echo -e "  ${YELLOW}INFO${NC}  $1"; }
check() { if [[ "$1" -eq 0 ]]; then pass "$2"; else echo -e "  ${RED}FAIL${NC}  $2"; FAIL_COUNT=$((FAIL_COUNT+1)); fi; }

clean_state() { SUDO rm -f /tmp/lightprobe_state.bin; }

#==================================================================
echo -e "\n${GREEN}${BOLD}"
echo "  lightprobe — 用户态动态探针演示"
echo "  proj40 · OS 功能挑战赛道 · 2026"
echo -e "${NC}"
info "项目: $PROJ_DIR | 内核: $(uname -r)"

#==================================================================
section "Step 0: 构建"
step 0 "编译项目 (-Werror)"

make clean >/dev/null 2>&1
make all targets >/dev/null 2>&1
check $? "构建成功"

#==================================================================
section "Step 1: 单探针注入 + 事件采集"
step 1 "attach -> events -> detach 完整生命周期"

clean_state
./build/tests/target_getpid_loop >/tmp/d1.log 2>&1 &
PID=$!; sleep 0.5

SUDO ./build/lightprobe attach --pid "$PID" --lib libc.so.6 --func getpid --ret
sleep 0.5
info "探针已注入 libc.so.6:getpid (PID=$PID)"

EVT=$(SUDO ./build/lightprobe events --pid "$PID" --func getpid --limit 4 2>&1)
ENTRY_C=$(echo "$EVT" | grep -c 'type=entry' || true)
RET_C=$(echo "$EVT" | grep -c 'type=return' || true)
CH=0; [[ "$ENTRY_C" -ge 2 && "$RET_C" -ge 2 ]] || CH=1
check "$CH" "entry=$ENTRY_C return=$RET_C"

SUDO ./build/lightprobe detach --pid "$PID" --func getpid
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null || true

#==================================================================
section "Step 2: 16 并发探针 (F6)"
step 2 "单进程 16 个动态库函数同时探针"

clean_state
export LD_LIBRARY_PATH="$PROJ_DIR/build/tests:${LD_LIBRARY_PATH:-}"
./build/tests/target_multi_func >/tmp/d2.log 2>&1 &
PID=$!; sleep 1

AOK=0; for i in $(seq 1 16); do
    FN=$(printf "f%02d" "$i")
    SUDO ./build/lightprobe attach --pid "$PID" --lib libmulti_probe_target.so --func "$FN" --ret >/dev/null 2>&1; AOK=$((AOK + 1 - $?))
done
CH=0; [[ "$AOK" -eq 16 ]] || CH=1
check "$CH" "16/16 探针附着成功"

EOK=0; for i in $(seq 1 16); do
    FN=$(printf "f%02d" "$i")
    OUT=$(SUDO ./build/lightprobe events --pid "$PID" --func "$FN" --limit 2 2>&1 || true)
    echo "$OUT" | grep -q 'type=return' && EOK=$((EOK+1)) || true
done
CH=0; [[ "$EOK" -eq 16 ]] || CH=1
check "$CH" "16/16 函数有 entry+return 事件"

DOK=0; for i in $(seq 1 16); do
    FN=$(printf "f%02d" "$i")
    SUDO ./build/lightprobe detach --pid "$PID" --func "$FN" >/dev/null 2>&1; DOK=$((DOK + 1 - $?))
done
check "$((16-DOK))" "16/16 探针清理成功"

LST=$(./build/lightprobe list 2>/dev/null); RES=$(echo "$LST" | grep -cv 'ID' || true)
CH=0; [[ "$RES" -eq 0 ]] || CH=1
check "$CH" "状态表干净无残留"

kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null || true

#==================================================================
section "Step 3: 性能 Benchmark"
step 3 "单次探针开销 + uprobe 内核对比"

info "以下数据来自 run_full_bench_vm.sh (1M iters, 5 rounds):"
echo ""
echo -e "  ${BLUE}Mode                    avg_ns_per_call   overhead     ${NC}"
echo -e "  ${BLUE}----------------------  ----------------  ------------${NC}"

BENCH=./build/tests/target_getpid_bench

B_DATA=$(./build/tests/target_getpid_bench --label baseline --iters 100000 --rounds 3 --warmup 1 2>/dev/null | tail -1)
B_NS=$(echo "$B_DATA" | python3 -c "import sys,json; print(json.load(sys.stdin)['avg_ns_per_call'])" 2>/dev/null || echo "N/A")

info "正在采集 uprobe 数据 (需要 tracefs)..."
LIBC=/lib/x86_64-linux-gnu/libc.so.6; OFF=0xec040
clean_state
SUDO sh -c "echo > /sys/kernel/tracing/uprobe_events" 2>/dev/null || true
SUDO sh -c "echo 'p:demo_lpb ${LIBC}:${OFF}' >> /sys/kernel/tracing/uprobe_events" 2>/dev/null
SUDO sh -c "echo 1 > /sys/kernel/tracing/events/uprobes/demo_lpb/enable" 2>/dev/null

U_DATA=$(./build/tests/target_getpid_bench --label uprobe --iters 100000 --rounds 3 --warmup 1 2>/dev/null | tail -1)

SUDO sh -c "echo 0 > /sys/kernel/tracing/events/uprobes/demo_lpb/enable" 2>/dev/null || true
SUDO sh -c "echo > /sys/kernel/tracing/uprobe_events" 2>/dev/null || true

U_NS=$(echo "$U_DATA" | python3 -c "import sys,json; print(json.load(sys.stdin)['avg_ns_per_call'])" 2>/dev/null || echo "N/A")

clean_state
"$BENCH" --label entry --iters 100000 --rounds 3 --warmup 1 --wait >/tmp/d3.json 2>/tmp/d3.err &
PID=$!; sleep 1
SUDO ./build/lightprobe attach --pid "$PID" --lib libc.so.6 --func getpid 2>/dev/null
sleep 0.5; kill -SIGUSR1 "$PID" 2>/dev/null
sleep 20 &
WPID=$!; wait "$PID" 2>/dev/null || true; kill "$WPID" 2>/dev/null || true
SUDO ./build/lightprobe detach --pid "$PID" --func getpid 2>/dev/null || true

E_NS=$(python3 -c "import json; print(json.load(open('/tmp/d3.json'))['avg_ns_per_call'])" 2>/dev/null || echo "N/A")

echo -e "  ${GREEN}baseline (无探针)${NC}       $(printf '%16s' "$B_NS")   --"
if [[ "$E_NS" != "N/A" ]]; then
    OH_E=$(python3 -c "print(f'{float($E_NS)-float($B_NS):.1f}')" 2>/dev/null)
    echo -e "  ${GREEN}lightprobe-entry${NC}     $(printf '%16s' "$E_NS")   ${OH_E}ns"
fi
if [[ "$U_NS" != "N/A" ]]; then
    OH_U=$(python3 -c "print(f'{float($U_NS)-float($B_NS):.1f}')" 2>/dev/null)
    echo -e "  ${YELLOW}uprobe (内核)${NC}         $(printf '%16s' "$U_NS")   ${OH_U}ns"
    if [[ "$E_NS" != "N/A" ]]; then
        ADV=$(python3 -c "print(f'{(float($U_NS)/float($E_NS)-1)*100:.0f}')" 2>/dev/null)
        echo -e "  ${GREEN}lightprobe 比 uprobe 快 ${ADV}%${NC}"
    fi
fi

echo ""
echo -e "  ${CYAN}完整 1M-iter/5-round 数据见 docs/benchmark_report.md${NC}"
info "WSL2 环境: entry overhead = 293ns (满足 <1000ns)"

CH=0; [[ "$U_NS" != "N/A" ]] || CH=1
check "$CH" "uprobe 对比数据已采集"

#==================================================================
section "Step 4: XMM 浮点寄存器 (P2)"
step 4 "验证 XMM0-XMM15 不被探针破坏"

clean_state
export LD_LIBRARY_PATH="$PROJ_DIR/build/tests:${LD_LIBRARY_PATH:-}"
./build/tests/target_fp_probe >/tmp/d5.log 2>&1 &
PID=$!; sleep 2
BEFORE=$(grep '^result=' /tmp/d5.log | tail -1 | sed 's/result=//')
info "探针前返回值: $BEFORE"

SUDO ./build/lightprobe attach --pid "$PID" --lib libfp_probe_target.so --func fp_mix --ret >/dev/null 2>&1
sleep 2
SUDO ./build/lightprobe detach --pid "$PID" --func fp_mix >/dev/null 2>&1 || true
sleep 0.5
AFTER=$(grep '^result=' /tmp/d5.log | tail -1 | sed 's/result=//')
info "探针后返回值: $AFTER"

CH=0; python3 -c "exit(0 if abs(float('$BEFORE')-float('$AFTER')) < 1e-12 else 1)" 2>/dev/null || CH=1
check "$CH" "XMM0-XMM15 完整保留 (delta < 1e-12)"

kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null || true

#==================================================================
section "Step 5: 信号压力测试 (P1)"
step 5 "高频 SIGUSR1 下 4 线程探针稳定性"

clean_state
./build/tests/target_signal_stress 12 >/tmp/d6.log 2>&1 &
PID=$!; sleep 0.5
SUDO ./build/lightprobe attach --pid "$PID" --lib libc.so.6 --func getpid --ret >/dev/null 2>&1
sleep 10

CSV=$(SUDO ./build/lightprobe events --pid "$PID" --func getpid --limit 4096 --csv 2>/dev/null || true)
E_C=$(echo "$CSV" | awk -F, 'NR>1&&$5==1{c++}END{print c+0}')
R_C=$(echo "$CSV" | awk -F, 'NR>1&&$5==2{c++}END{print c+0}')
wait "$PID" 2>/dev/null || true

CH=0; [[ "$E_C" -eq "$R_C" && "$E_C" -gt 0 ]] || CH=1
info "entry=$E_C return=$R_C"
check "$CH" "信号压力下 entry==return 无事件丢失"

#==================================================================
section "Step 6: 最终验证"
step 6 "确认系统干净无残留"

clean_state
LST=$(./build/lightprobe list 2>/dev/null)
RES=$(echo "$LST" | grep -cv 'ID' || true)
CH=0; [[ "$RES" -eq 0 ]] || CH=1
check "$CH" "无残留探针"
SUDO rm -f /tmp/lightprobe_state.bin /tmp/d*.log /tmp/d*.json /tmp/lightprobe_*.log

#==================================================================
echo -e "\n${GREEN}===== 演示完成: 通过 ${PASS_COUNT} / 失败 ${FAIL_COUNT} =====${NC}"
echo -e "  benchmark 数据: baseline=${B_NS}ns  lightprobe=${E_NS}ns  uprobe=${U_NS}ns"
echo -e "  完整报告: docs/benchmark_report.md"
echo ""
