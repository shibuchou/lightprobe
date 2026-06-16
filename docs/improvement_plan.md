# lightprobe 下一步完善计划（最终执行版）

> 赛题：轻量级用户态动态探针 · proj40 · OS 功能挑战赛道  
> 当前：F1-F7 基本完成，文档完善，分数预估 83-85  
> 目标：按评分点逐项补齐证据，冲 93-95

---

## 总览

```
P0  性能基准 + F6 并发实测       ← 立刻执行，最大提分来源
P1  多线程 + 信号压力测试
P2  浮点/XMM 寄存器保存恢复
P3  工程优化
P4  进阶加分（二选一）
```

---

## P0-1：标准 getpid Benchmark（~2h）

### 目标程序 `tests/targets/target_getpid_bench.c`

```text
--iters    1000000     循环次数
--rounds   10          重复轮数
--warmup   2           预热轮数
--wait                 启动后等待用户信号再执行
--label    baseline | uprobe | lightprobe-entry | lightprobe-entry-return
```

`--label` 设计说明：程序本身不感知外部探针模式，只将 label 写入 JSON 作为输出标记；实际模式由外部脚本控制（先 attach 探针，再启动目标程序执行循环）。

`--wait` 逻辑：

```text
1. 启动 → 打印 PID → 等待 Enter / SIGUSR1
2. 收到信号 → 执行 warmup 轮
3. 执行正式轮 → 输出 JSON
```

输出格式：

```json
{
  "label": "lightprobe-entry",
  "iters": 1000000,
  "rounds": 10,
  "warmup": 2,
  "avg_ns_total": 123456789,
  "avg_ns_per_call": 123.45,
  "stddev_ns_per_call": 3.21,
  "min_ns_per_call": 120.10,
  "max_ns_per_call": 128.90
}
```

### 四组测试（区分 entry-only / entry+return）

| 组 | label | 说明 |
|------|------|------|
| A | baseline | 无探针，纯 getpid 循环 |
| B | uprobe | tracefs uprobe 挂到 `libc.so.6:getpid` |
| C | lightprobe-entry | `attach --ret` 不加，只 entry probe |
| D | lightprobe-entry-return | `attach --ret`，完整 entry+return |

报告中主推 C 作为标准性能指标，D 作为扩展测试。

### uprobe tracefs 三层 fallback

```text
第一层：tracefs uprobe_events
  通过 readelf/nm 解析 getpid 在 libc 中的 offset
  echo 'p:lpb_getpid /path/to/libc.so.6:OFFSET' > /sys/kernel/tracing/uprobe_events
  echo 1 > /sys/kernel/tracing/events/lpb_getpid/enable

第二层：perf probe（如果第一层失败）
  perf probe -x /path/to/libc.so.6 getpid

第三层：bpftrace（如果前两层都失败）
  bpftrace -e 'uprobe:/path/to/libc.so.6:getpid { @++; }
               interval:s:1 { exit(); }'
```

### benchmark 脚本 `tests/scripts/run_perf_benchmark.sh`

```bash
./tests/scripts/run_perf_benchmark.sh
# 自动执行 A/B/C/D 四组，输出对比表和 JSON
```

### 产出

`docs/benchmark_report.md`：

```text
- 四组测试原始数据（JSON）
- 对比表：label | avg_ns_per_call | stddev | min | max
- 单次探针开销 = C_avg - A_avg
- vs uprobe 开销 = (C_avg - A_avg) vs (B_avg - A_avg)
- 结论：是否 < 1000ns，是否低于 uprobe
```

---

## P0-2：attach/detach 延迟量化（~30min）

修改 `run_perf_benchmark.sh` 或单独脚本，对 getpid/malloc/strlen 各取 10 次 attach+detach 耗时，输出 min/avg/max。结果并入 `docs/benchmark_report.md`。

---

## P0-3：16 并发探针端到端测试（~1h）

### 使用动态库（贴合 F1"动态库函数探针"题意）

```text
tests/targets/libmulti_probe_target.so
  __attribute__((visibility("default")))
  int f01(int x) { return x + 1; }
  ...
  int f16(int x) { return x + 16; }

tests/targets/target_multi_func.c
  链接 libmulti_probe_target.so
  循环调用 f01(0) ... f16(0)
```

### 测试脚本 `tests/scripts/run_16probe_smoke.sh`

验证点：

```text
1. attach 成功数 = 16（全部返回 attached probe_id=0..15）
2. events --limit 32，每个 probe_id 都有 entry event
3. --ret 下每个 probe 都有 return event，retval 正确
4. detach 16 次全部成功
5. list 只剩表头
6. 目标进程无崩溃
```

---

## P1：多线程 + 信号压力测试（~3h）

### 信号安全方案

| 场景 | 做法 |
|------|------|
| attach/detach 阶段 | 暂停目标线程组，统一 patch/unpatch |
| stub 执行中被信号打断 | per-thread `in_probe` 标志 + shadow stack 一致性校验 |
| shadow stack 原子性 | 先写完整 frame → 递增 depth；先读 frame → 递减 depth |
| 异常兜底 | depth 异常 / slot overflow / return 地址异常 → fallback 原路径 |

**不使用 cli/sti（特权指令用户态不可用），不使用 sigprocmask（破坏性能目标）。**

### 测试目标 `tests/targets/target_signal_stress.c`

```text
- 4 线程 malloc/getpid 循环
- 主线程高频发送 SIGUSR1 / SIGALRM 给工作线程
- 运行 60 秒
```

### 验证脚本 `tests/scripts/run_signal_stress.sh`

```text
统计条件：
  entry 数 == return 数
  无 segfault / assert 触发
  shadow stack 溢出/异常 fallback 次数 < 1%
```

---

## P2：浮点/XMM 寄存器保存恢复（~1.5h）

### 保存范围

**XMM0-XMM15**，覆盖本项目 trampoline/stub 可能影响的主流 SSE 浮点寄存器路径；不声称覆盖完整 XSAVE 扩展状态。

### 实现位置

`injector/trampoline_x86_64.c` — entry stub 生成时增加 XMM 压栈，ret stub 增加 XMM 出栈。

### 测试 `tests/targets/target_fp_probe.c`

```c
double fp_mix(double a, double b) {
    return sin(a) + cos(b) + a * b;
}
```

probe 前后对比返回值（允许浮点误差 < 1e-12）。如果有条件，补一个 SIMD 测试（如 `memcpy`）：

```c
// probe memcpy with large buffer, verify no XMM corruption
```

---

## P3：工程优化

| 优先级 | 事项 | 预估 |
|--------|------|------|
| **高** | 状态文件 `/tmp/lightprobe_state.bin` → `/tmp/lightprobe_state_<pid>.bin` | 1h |
| **中** | `context_x86_64.S` 补充注释：实际上下文保存在 `trampoline_x86_64.c` 内联汇编中实现 | 15min |
| **低** | 远程 runtime RWX → RW(数据区) + RX(代码区) 拆分 | 2h |

---

## P4：进阶加分（二选一）

| 选项 | 内容 | 预估 | 推荐度 |
|------|------|------|--------|
| **A3** | `events --perf` 输出 perf-script 兼容格式 | 1 天 | 稳：只改输出层 |
| **A2** | 最小条件探针 `--filter-arg0-eq/gte/lte` | 2 天 | 亮眼：功能更明显 |

---

## 时间线

```text
6/16 │ P0-1 benchmark 目标程序 (--wait)       │ uprobe fallback 脚本
     │ 四组测试 (baseline/uprobe/entry/entry+ret) │ P0-2 attach/detach 延迟
     │ 产出 docs/benchmark_report.md

6/17 │ P0-3 libmulti_probe_target.so + f01-f16  │ target_multi_func.c
     │ run_16probe_smoke.sh                     │ P3.1 状态文件 PID 隔离
     │ P3.2 context_x86_64.S 注释

6/18 │ P1 signal_stress 目标程序 + 测试脚本     │ shadow stack 原子性 + fallback

6/19 │ P2 XMM0-15 保存恢复                      │ fp_probe 验证

6/20-21 │ P3.3 RWX 拆分（可选）                 │ P4 A3 perf-script 或 A2 条件探针

6/22-29 │ 全量回归 + 文档补全 + 答辩 PPT/演示脚本

6/30 │ 最终提交
```

---

## 分数预估

| 阶段 | 分数 | 关键证据 |
|------|------|----------|
| 当前 | 83-85 | F1-F7 基本完成，文档完善 |
| P0 完成 | 88-90 | 标准 benchmark 四组对比 + F6 16 并发实测 + attach/detach 延迟 |
| P1+P2 完成 | 91-93 | 信号安全 + 浮点寄存器 + 正确性提升 |
| P4 完成 | 93-95 | 一个进阶加分项 |

---

## 执行状态 (截至 2024-06-16)

| 任务 | 状态 |
|------|------|
| P0-1 benchmark | **done** — WSL2 293ns, VM 1259ns, uprobe 2898ns |
| P0-2 latency | **done** — ~38ms CLI end-to-end |
| P0-3 16-probe | **done** — 16/16 attach/events/detach/clean |
| P1 signal stress | **done** — entry=return=2048, no crash |
| P2 XMM0-15 | **done** — delta < 1e-12 |
| P3.1 state file PID | **reverted** — 全局路径更稳定 |
| P3.2 comment | **done** |
| P3.3 RWX 拆分 | 未开始 (低优先) |
| P4 A2/A3 | 未开始 |
| LICENSE | **done** (Apache 2.0) |
| demo.sh | **done** — 10/10 全部通过 |
| benchmark_report.md | **done** — 双环境 + uprobe 对比 |
