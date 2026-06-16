# lightprobe 验证记录

> 本文档记录所有端到端测试的实际执行结果，作为功能完成度的可追溯证据。

## 测试环境

| 环境 | 系统 | 内核 | 架构 |
|------|------|------|------|
| WSL2 | Ubuntu 24.04 | 6.8.0 | x86_64 |
| VM | Ubuntu 22.04.5 | 6.8.0-124-generic | x86_64 |

---

## 1. 基础探针注入 (F1-F5)

### getpid smoke

```
$ LIGHTPROBE_SUDO_PASSWORD=*** ./tests/scripts/run_getpid_probe_smoke.sh
attached probe_id=0 pid=XXX libc.so.6:getpid target=0x... ret=1
probe_id=0 pid=XXX libc.so.6:getpid event_buffer=0x... total=1868 showing=8
ts=... probe=0 type=entry args=[...] retval=0x0 duration=0
ts=... probe=0 type=return args=[...] retval=0x... duration=...
detached pid=XXX func=getpid
ID  PID      ENABLED  RET  TARGET              LIB:FUNC
```

**结果**: attach → 采集 entry+return 事件 → detach → list 清空，完整生命周期通过。

### malloc / strlen / write smoke

同上模式，均通过。参见 `tests/scripts/run_malloc_probe_smoke.sh`、`run_strlen_probe_smoke.sh`、`run_write_probe_smoke.sh`。

---

## 2. 性能 Benchmark (P0-1)

### WSL2 环境 (1,000,000 iters, 5 rounds)

| Mode | avg_ns_per_call | stddev |
|------|----------------|--------|
| baseline | 98.77 | 1.10 |
| lightprobe-entry | 392.01 | 5.75 |
| lightprobe-entry-return | 662.21 | 10.26 |

**Entry-only overhead: 293.25 ns/call (<1000ns)**

### VM 原生 Linux (1,000,000 iters, 5 rounds)

| Mode | avg_ns_per_call | stddev |
|------|----------------|--------|
| baseline | 550.98 | 11.34 |
| **uprobe (kernel)** | **3,449.12** | 24.40 |
| lightprobe-entry | 1,810.04 | 18.26 |
| lightprobe-entry-return | 2,883.08 | 14.41 |

**lightprobe entry-only 比内核 uprobe 快 57%**

完整数据及分析见 `docs/benchmark_report.md`。

---

## 3. 16 并发探针 (F6)

```
$ LIGHTPROBE_SUDO_PASSWORD=*** ./tests/scripts/run_16probe_smoke.sh
=== lightprobe 16-probe concurrency smoke test ===
target pid=XXX
attached probe_id=0 ... f01 OK
... (16 probes)
attached probe_id=15 ... f16 OK
Functions with events: 16 / 16
Total entry events: 32, return events: 32
[PASS] all 16 functions have entry+return events
[PASS] all 16 probes detached
[PASS] list clean, no residual probes
=== RESULT: ALL CHECKS PASSED ===
```

**结果**: 16/16 attach → 16/16 events → 16/16 detach → list 清空。

---

## 4. 浮点寄存器保存恢复 (P2 / XMM0-XMM15)

```
$ LIGHTPROBE_SUDO_PASSWORD=*** ./tests/scripts/run_fp_probe_smoke.sh
baseline result=2.425324148261
attached probe_id=0 libfp_probe_target.so:fp_mix target=0x... ret=1
probe_id=0 ... type=entry ... type=return ...
detached pid=XXX func=fp_mix
after-detach result=2.425324148261
PASS: XMM0-15 registers preserved before/after probe (delta < 1e-12)
```

**结果**: 探针前后浮点返回值完全相同，delta < 1×10⁻¹²。

**实现方式**: XMM0-XMM15 保存/恢复代码位于 `injector/xmm_save_restore_x86_64.S`，在 `probe_stub_x86_64.S` 和 `ret_stub_x86_64.S` 的 stub 入口/出口调用，不侵入 `trampoline_x86_64.c` 的热路径。详细设计见 `docs/improvement_plan.md` P2 节。

---

## 5. 多线程信号压力测试 (P1)

```
$ LIGHTPROBE_SUDO_PASSWORD=*** DURATION=12 ./tests/scripts/run_signal_stress.sh
=== attaching to pid=XXX ===
attached probe_id=0 pid=XXX libc.so.6:getpid target=0x... ret=1
=== collecting events ===
entry_count=2048 return_count=2048
lightprobe_signal_stress pid=XXX duration=12s workers=4
signal_stress done pid=XXX sent=25556 received=25556 duration=12
```

**结果**: entry==return (2,048/2,048)，信号收发一致 (25,556/25,556)，目标进程无崩溃。

---

## 6. attach/detach 延迟 (P0-2)

| 函数 | 轮次 | min (ms) | avg (ms) | max (ms) |
|------|------|----------|----------|----------|
| getpid | 10 | 35.4 | 37.6 | 39.5 |
| malloc | 10 | 35.8 | 37.5 | 39.4 |
| strlen | 10 | 34.7 | 38.0 | 42.9 |

注: 此数据为 CLI 端到端延迟（含进程启动、ptrace、状态文件 I/O）。核心 patch/unpatch 阶段延迟未单独测量。

---

## 7. 一键演示脚本

```
$ LIGHTPROBE_SUDO_PASSWORD=*** ./tests/scripts/demo.sh
Step 0: 构建              PASS
Step 1: 单探针注入         PASS
Step 2: 16 并发探针        PASS (4 sub-checks)
Step 3: Benchmark+uprobe  PASS
Step 4: XMM 浮点寄存器     PASS
Step 5: 信号压力测试       PASS
Step 6: 最终验证           PASS
===== 演示完成: 通过 10 / 失败 0 =====
```

---

## 验证总结

| 功能 | 脚本 | 状态 | 关键指标 |
|------|------|------|----------|
| F1-F5 基础探针 | `run_getpid_probe_smoke.sh` | PASS | attach → events → detach → list |
| F6 16 并发探针 | `run_16probe_smoke.sh` | PASS | 16/16 全部通过 |
| P0-1 Benchmark | `run_perf_benchmark.sh` | PASS | 293ns (WSL2), uprobe 对比 |
| P0-2 Latency | `run_latency_bench.sh` | PASS | ~38ms CLI 端到端 |
| P1 信号压力 | `run_signal_stress.sh` | PASS | entry=return=2048 |
| P2 XMM 浮点 | `run_fp_probe_smoke.sh` | PASS | delta < 1e-12 |
| Demo 全集 | `demo.sh` | PASS | 10/10 checks |
