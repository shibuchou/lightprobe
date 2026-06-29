# lightprobe 系统演示视频录制流程与解说词

> 面向：2026 年全国大学生计算机系统能力大赛 OS 功能挑战赛道 `proj40`。  
> 赛题：轻量级用户态动态探针 / Lightweight Dynamic probe for User Space。  
> 推荐视频长度：8 到 12 分钟。  
> 目标：用一条清晰证据链展示 F1-F7 基础功能、正确性、性能指标、文档与 demo 完整度。

## 1. 录制前准备

### 1.1 环境

建议在 Linux/x86_64 环境中录制，推荐使用当前项目实际验证环境：

- Ubuntu 22.04 / 24.04。
- x86_64。
- 具备 `sudo` 权限。
- 能访问 `ptrace`、`/proc/<pid>/maps`、tracefs 或 perf。

如果从 Windows 打开仓库，进入 WSL/Linux shell 后建议路径为：

```bash
cd /mnt/d/code/Ubuntu/lightprobe
```

如果在虚拟机中录制，进入对应仓库路径即可。

### 1.2 录制注意事项

- 不要在视频中明文展示 sudo 密码、GitLab 密码、GitHub token 或个人证件信息。
- 录制前清理终端，放大字体到 16 到 20 号。
- 建议打开两个终端窗口：
  - 左侧：运行命令和演示脚本。
  - 右侧：展示 README、架构图或 benchmark 报告。
- 不建议展示 `git status`，因为当前工作区可能包含未提交开发文件；演示重点是系统能力，不是仓库状态。
- 所有端到端脚本应串行执行，因为当前 CLI 使用 `/tmp/lightprobe_state.bin` 作为本地状态文件。

### 1.3 一次性准备命令

```bash
cd /mnt/d/code/Ubuntu/lightprobe
clear
make clean
make all targets
```

如果不希望在命令行显示密码，直接运行脚本后按提示输入 sudo 密码：

```bash
./tests/scripts/demo.sh
```

如果是私下验证、不会录制命令历史，也可以使用环境变量：

```bash
LIGHTPROBE_SUDO_PASSWORD='你的sudo密码' ./tests/scripts/demo.sh
```

## 2. 演示主线

整段视频建议围绕一句话展开：

```text
lightprobe 通过 ptrace 修改目标进程内存，在目标进程内写入用户态 runtime，并 patch 动态库函数入口，让函数调用直接跳转到用户态 entry/return stub，从而避免 kernel uprobe 的 trap 进入内核开销。
```

推荐演示顺序：

1. 赛题背景与评分点。
2. 系统架构和模块分工。
3. 单探针 attach/events/detach 生命周期。
4. return probe、参数、返回值和 tid 事件证据。
5. 16 并发探针验证 F6。
6. benchmark 对比 baseline / kernel uprobe / lightprobe。
7. 正确性验证：浮点寄存器和信号压力。
8. 实现边界与总结。

## 3. 官方评分点到演示证据映射

| 官方要求 | 演示证据 | 推荐展示位置 |
| --- | --- | --- |
| F1 动态库函数探针插入 | `attach --lib libc.so.6 --func getpid` 成功，目标进程继续运行 | Step 1 |
| F2 函数参数获取 | `events` 输出 `args[0..5]` | Step 1 |
| F3 函数返回值捕获 | `type=return`、`retval=0x<pid>`、`duration` | Step 1 |
| F4 探针清理 | `detach` 成功，`list` 无残留 | Step 1 / Step 6 |
| F5 动态开关 | README/CLI 展示 `enable/disable`，必要时口头说明 | 架构介绍 |
| F6 多探针共存 | 16/16 attach、16/16 events、16/16 detach | Step 2 |
| F7 多线程安全 | 多线程、信号压力下 entry==return，无崩溃 | Step 5 |
| 性能指标 | baseline / uprobe / lightprobe 对比，WSL2 entry overhead 293ns | Step 3 |
| 正确性指标 | XMM0-XMM15 delta < 1e-12，信号压力 entry==return | Step 4 / Step 5 |
| 文档和 demo | README、架构图、benchmark report、demo.sh | 开头与结尾 |

## 4. 视频分镜与解说词

### 片段 0：标题与赛题说明

**画面**

打开 `README.md` 顶部，展示项目名、赛题名和架构图。

**建议操作**

```bash
sed -n '1,32p' README.md
```

也可以直接在 VS Code 中展示：

```text
README.md
docs/figures/lightprobe_architecture_cn.svg
```

**解说词**

```text
大家好，我们展示的是 2026 年全国大学生计算机系统能力大赛 OS 功能挑战赛道 proj40，赛题是“轻量级用户态动态探针”。

这个项目叫 lightprobe，核心目标是在 Linux x86_64 用户态实现一个动态探针框架。它不依赖 kernel uprobe 的 trap 机制，而是通过 ptrace 控制目标进程，在目标进程内写入 probe runtime，并修改动态库函数入口，让函数调用直接进入用户态的 entry stub 和 return stub。

这样可以在不重启、不修改目标程序源码的前提下，采集函数参数、返回值、线程号和时间戳，并且避免每次探针触发都陷入内核态。
```

### 片段 1：架构介绍

**画面**

展示架构图：

```text
docs/figures/lightprobe_architecture_cn.svg
```

或展示目录结构：

```bash
find cli controller injector runtime include tests docs -maxdepth 1 -type d
```

**解说词**

```text
系统分成五个核心部分。

第一是 CLI 层，提供 attach、detach、enable、disable、events 和 list 命令。

第二是 controller 层，负责 ptrace attach、暂停线程、解析 maps 和 ELF 符号，并通过远程 mmap、read、write 和 syscall 操作目标进程内存。

第三是 injector 层，负责 x86_64 函数入口 patch、trampoline 构造、entry stub 和 ret stub 生成。

第四是 runtime 层，它位于目标进程地址空间中，包含 config、event buffer、shadow stack、trampoline 和 stub 代码。

最后是 tests 和 docs，用来提供目标程序、压力测试、benchmark 和比赛展示材料。
```

### 片段 2：构建项目

**画面**

终端执行构建。

**命令**

```bash
make clean
make all targets
```

**预期输出**

```text
build/lightprobe
build/tests/target_getpid_loop
build/tests/target_multi_func
build/tests/target_getpid_bench
...
```

**解说词**

```text
先构建主程序和所有测试目标。主程序是 build/lightprobe，测试目标包括 getpid 循环程序、malloc 和 strlen 测试、多线程测试、16 函数并发测试，以及 benchmark 目标程序。

这些目标程序用于证明探针可以插入真实动态库函数，并且可以在单线程、多线程和压力场景中稳定运行。
```

### 片段 3：单探针完整生命周期

**画面**

推荐先执行一键脚本的 Step 1，或者手动演示命令。录屏更直观时建议手动演示。

**手动命令**

```bash
./build/tests/target_getpid_loop &
target_pid=$!

sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func getpid --ret
sudo ./build/lightprobe events --pid "$target_pid" --func getpid --limit 8
sudo ./build/lightprobe detach --pid "$target_pid" --func getpid
./build/lightprobe list

kill "$target_pid"
```

**预期输出重点**

```text
attached probe_id=0 pid=<pid> libc.so.6:getpid target=0x... ret=1
type=entry  ... tid=<tid> args=[...]
type=return ... tid=<tid> retval=0x<pid> duration=<ns>
detached pid=<pid> func=getpid
ID  PID      ENABLED  RET  TARGET              LIB:FUNC
```

**解说词**

```text
这里展示的是一个完整的探针生命周期。

我们先启动一个循环调用 getpid 的目标进程，然后通过 lightprobe attach 命令把探针插入 libc.so.6 的 getpid 函数。这里加上 --ret，表示同时启用 entry probe 和 return probe。

attach 成功后，可以看到 probe_id、目标进程 pid、库名、函数名和目标函数地址。接着执行 events 命令读取目标进程内的 event buffer。

entry 事件证明我们捕获到了函数入口，里面包含 pid、tid、probe_id、前 6 个参数和时间戳。return 事件证明 return probe 正常工作，retval 对应 getpid 的返回值，也就是目标进程 pid，同时 duration 表示从 entry 到 return 的耗时。

最后执行 detach，系统恢复目标函数入口的原始指令，清理本地状态。list 只剩表头，说明没有残留探针。
```

### 片段 4：解释 return probe 设计

**画面**

展示 `docs/member_a_runtime_layout.md` 中 return probe 流程，或者口头配合架构图。

**建议展示**

```bash
sed -n '89,126p' docs/member_a_runtime_layout.md
```

**解说词**

```text
return probe 的关键点是返回地址替换。

目标函数进入 entry stub 后，stub 会读取栈顶原始返回地址，把它和 entry timestamp 一起压入当前 tid 对应的 shadow stack。然后把栈顶返回地址改成 ret_stub。

目标函数正常执行结束时，ret 指令不直接回到调用者，而是先跳到 ret_stub。ret_stub 保存 rax 作为返回值，读取当前时间戳计算 duration，再从 shadow stack 弹出原始返回地址，最后跳回原始调用点。

这个过程要求上下文保存恢复正确，并且多线程下每个 tid 的返回地址不能串扰，所以我们使用 per-thread shadow stack 来维护返回路径。
```

### 片段 5：16 并发探针验证 F6

**画面**

执行 16 probe 脚本。

**命令**

```bash
./tests/scripts/run_16probe_smoke.sh
```

如果需要 sudo 密码：

```bash
LIGHTPROBE_SUDO_PASSWORD='密码' ./tests/scripts/run_16probe_smoke.sh
```

**预期输出重点**

```text
attached f01 OK
...
attached f16 OK
Functions with events: 16 / 16
Total entry events: ...
return events: ...
[PASS] all 16 functions have entry+return events
[PASS] all 16 probes detached
[PASS] list clean, no residual probes
```

**解说词**

```text
这一段对应官方基础功能 F6：多探针共存，同一进程至少 16 个并发探针。

测试目标链接了一个自定义动态库 libmulti_probe_target.so，里面导出 f01 到 f16 共 16 个函数。脚本会在同一个目标进程中依次 attach 16 个探针，并且全部启用 return probe。

随后脚本分别读取每个函数的事件，确认 16 个函数都有 entry 和 return 事件。最后逐个 detach，确认 16 个探针全部清理成功，并且 list 无残留。

这说明我们的 probe table、runtime 分配、event buffer 读取和 detach 清理都能支持多探针共存。
```

### 片段 6：性能 benchmark 与 kernel uprobe 对比

**画面**

展示 benchmark 报告，然后可运行脚本。

**快速展示报告**

```bash
sed -n '42,82p' docs/benchmark_report.md
```

**可选现场运行**

```bash
LIGHTPROBE_BENCH_ITERS=100000 ./tests/scripts/run_perf_benchmark.sh
```

如果 tracefs 需要 sudo：

```bash
LIGHTPROBE_SUDO_PASSWORD='密码' LIGHTPROBE_BENCH_ITERS=100000 ./tests/scripts/run_perf_benchmark.sh
```

**报告中的关键数据**

```text
WSL2:
baseline = 98.77 ns
lightprobe-entry = 392.01 ns
entry-only overhead = 293.25 ns

VM:
baseline = 550.98 ns
kernel uprobe = 3449.12 ns
lightprobe-entry = 1810.04 ns
lightprobe entry 比 kernel uprobe 快 57%
```

**解说词**

```text
官方测试基准要求目标进程循环调用 getpid 一百万次，并分别测量无探针、内核 uprobe 和用户态探针的执行时间。

我们的 benchmark 使用 clock_gettime 统计整个循环耗时，再计算单次调用平均耗时。这里展示两组结果。

在 WSL2 的现代 CPU 环境下，baseline 是 98.77 纳秒，lightprobe entry-only 是 392.01 纳秒，所以单次探针开销大约是 293 纳秒，满足小于 1000 纳秒的指标。

在 Ubuntu 22.04 虚拟机环境下，kernel uprobe 平均 3449 纳秒，lightprobe entry-only 平均 1810 纳秒。虽然虚拟机本身 baseline 更高，但 lightprobe 仍然比 kernel uprobe 快 57%，这直接验证了用户态跳转避免内核 trap 的设计优势。

entry+return 模式会额外执行 shadow stack push/pop 和 return event 写入，因此开销更高，但它提供了返回值和 duration 采集能力。
```

### 片段 7：浮点寄存器正确性

**画面**

执行浮点寄存器测试。

**命令**

```bash
./tests/scripts/run_fp_probe_smoke.sh
```

或：

```bash
LIGHTPROBE_SUDO_PASSWORD='密码' ./tests/scripts/run_fp_probe_smoke.sh
```

**预期输出重点**

```text
baseline result=...
after-detach result=...
PASS: XMM0-15 registers preserved before/after probe (delta < 1e-12)
```

**解说词**

```text
官方正确性指标要求上下文保存和恢复完整，包括通用寄存器、标志寄存器和浮点寄存器。

这个测试目标调用 fp_mix(double, double)，它会走浮点寄存器路径。我们在函数上安装 entry+return 探针，然后比较探针前后目标函数返回值。

结果显示 delta 小于 1e-12，说明探针没有破坏 XMM0 到 XMM15 的浮点寄存器状态。当前 stub 热路径只使用通用寄存器，不使用 SSE 或 AVX 指令，因此不会污染 XMM 寄存器。
```

### 片段 8：信号压力和多线程安全

**画面**

执行信号压力脚本。

**命令**

```bash
DURATION=12 ./tests/scripts/run_signal_stress.sh
```

或：

```bash
LIGHTPROBE_SUDO_PASSWORD='密码' DURATION=12 ./tests/scripts/run_signal_stress.sh
```

**预期输出重点**

```text
entry_count=2048 return_count=2048
signal_stress done ... sent=25556 received=25556
```

**解说词**

```text
这一段展示多线程安全和信号安全。

目标进程启动 4 个工作线程循环调用 getpid，同时主线程高频发送 SIGUSR1。我们在 getpid 上安装 entry+return 探针，然后读取事件。

结果中 entry_count 和 return_count 完全相等，目标进程正常退出，没有崩溃或死锁，信号发送和接收数量一致。

这说明在多线程和高频信号干扰下，return probe 的 shadow stack 仍然能够保持 entry 和 return 的配对关系。
```

### 片段 9：一键演示脚本

**画面**

在已经讲清楚核心机制后，执行总控脚本作为收束。

**命令**

```bash
./tests/scripts/demo.sh
```

或：

```bash
LIGHTPROBE_SUDO_PASSWORD='密码' ./tests/scripts/demo.sh
```

**预期输出重点**

```text
Step 0: 构建
Step 1: 单探针注入 + 事件采集
Step 2: 16 并发探针
Step 3: 性能 Benchmark
Step 4: XMM 浮点寄存器
Step 5: 信号压力测试
Step 6: 最终验证
演示完成: 通过 10 / 失败 0
```

**解说词**

```text
最后运行一键演示脚本，把前面的关键能力串起来。

脚本会自动完成构建、单探针生命周期、16 并发探针、benchmark 和 uprobe 对比、浮点寄存器验证、信号压力测试以及最终清理检查。

最后显示通过 10、失败 0，说明本次演示覆盖的功能项全部通过。
```

### 片段 10：总结与边界

**画面**

展示 README 的验证矩阵和实现边界。

**建议展示**

```bash
sed -n '171,239p' README.md
```

**解说词**

```text
总结一下，lightprobe 当前已经完成官方基础功能 F1 到 F7。

它支持动态库函数 attach 和 detach，能够采集前 6 个参数、返回值、tid 和时间戳；支持 enable 和 disable 管理；支持同一进程 16 个并发探针；并通过多线程、信号压力和浮点寄存器测试验证上下文安全。

性能上，lightprobe 在现代 CPU 上 entry-only 单次开销约 293 纳秒，满足小于 1000 纳秒的指标；在虚拟机环境中也比 kernel uprobe 快 57%，体现了用户态探针避免内核 trap 的优势。

当前边界是：项目主要支持 Linux x86_64 用户态动态库函数；ptrace 操作通常需要 sudo 权限；return probe 已支持基础多线程场景，但还不是工业级完整 unwinder；极深递归、极高并发、线程异步退出和完整多架构支持属于后续增强。

以上就是 lightprobe 的系统演示。
```

## 5. 推荐最终视频结构

| 时间 | 内容 | 画面 |
| --- | --- | --- |
| 0:00-0:40 | 赛题和项目目标 | README + 架构图 |
| 0:40-1:40 | 系统架构 | 架构图 / 目录结构 |
| 1:40-3:00 | 单探针生命周期 | attach / events / detach |
| 3:00-3:50 | return probe 原理 | `member_a_runtime_layout.md` |
| 3:50-5:00 | 16 并发探针 | `run_16probe_smoke.sh` |
| 5:00-6:30 | benchmark 对比 | `benchmark_report.md` / 脚本 |
| 6:30-7:30 | 浮点寄存器正确性 | `run_fp_probe_smoke.sh` |
| 7:30-8:40 | 信号压力和多线程 | `run_signal_stress.sh` |
| 8:40-10:00 | 一键 demo 和总结 | `demo.sh` + README 边界 |

## 6. 录制时的终端命令清单

### 快速版

```bash
cd /mnt/d/code/Ubuntu/lightprobe
make clean
make all targets
./tests/scripts/demo.sh
```

### 逐段展示版

```bash
cd /mnt/d/code/Ubuntu/lightprobe
make clean
make all targets

./build/tests/target_getpid_loop &
target_pid=$!
sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func getpid --ret
sudo ./build/lightprobe events --pid "$target_pid" --func getpid --limit 8
sudo ./build/lightprobe detach --pid "$target_pid" --func getpid
./build/lightprobe list
kill "$target_pid"

./tests/scripts/run_16probe_smoke.sh
./tests/scripts/run_fp_probe_smoke.sh
DURATION=12 ./tests/scripts/run_signal_stress.sh
LIGHTPROBE_BENCH_ITERS=100000 ./tests/scripts/run_perf_benchmark.sh
```

## 7. 答辩口径

### 为什么不是 kernel uprobe

```text
kernel uprobe 通过 trap 进入内核，再执行探针逻辑。lightprobe 的目标是把探针 runtime 放在目标进程用户态地址空间中，通过函数入口跳转直接进入用户态 stub，从而减少内核态切换开销。
```

### 为什么需要 ptrace

```text
ptrace 用于控制目标进程、暂停线程、读取和写入目标进程内存、执行远程 mmap，以及 patch 目标函数入口。探针触发后不依赖 ptrace；ptrace 主要用于安装和清理阶段。
```

### return probe 的难点

```text
return probe 不能只在函数入口记录参数，还需要拦截函数返回。我们在 entry stub 中替换栈顶返回地址，把原始返回地址保存到 per-thread shadow stack，函数返回时先进入 ret stub，采集 rax 返回值和 duration，再跳回原始返回地址。
```

### 多线程安全怎么保证

```text
安装和清理时暂停目标线程组，运行时 event buffer 用原子操作分配 slot，return probe 使用 tid 区分 shadow stack 线程槽。16 探针和信号压力测试证明当前基础多线程场景下不会崩溃或死锁。
```

### 已知不足怎么回答

```text
当前实现聚焦 x86_64 Linux 用户态动态库函数。return probe 支持基础多线程，但不是完整工业级 unwinder。极深递归、极高并发、线程异步退出、ARM64 和 perf-script 兼容输出属于后续增强方向。
```

## 8. 视频提交前自检

- [ ] 视频中没有出现明文密码、token、私钥或个人证件。
- [ ] 能看到项目名、赛题名和 `proj40`。
- [ ] 能看到 attach 成功。
- [ ] 能看到 entry 和 return 事件。
- [ ] 能看到 detach 成功和 list 清空。
- [ ] 能看到 16/16 并发探针结果。
- [ ] 能看到 benchmark 对比和 `<1000ns` 结论。
- [ ] 能看到 XMM 浮点寄存器验证。
- [ ] 能看到信号压力下 entry==return。
- [ ] 结尾说明实现边界，没有夸大成工业级完整方案。
