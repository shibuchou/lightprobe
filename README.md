# lightprobe

轻量级用户态动态探针原型，面向 2026 年全国大学生计算机系统能力大赛 OS 功能挑战赛道赛题：

```text
轻量级用户态动态探针 / Lightweight Dynamic probe for User Space
```

队伍名：原神启动

## 1. 项目目标

`lightprobe` 目标是在 Linux x86_64 用户态进程中实现轻量级动态探针能力：

- 不依赖 Linux kernel uprobe trap。
- 通过 `ptrace` 控制目标进程。
- 在目标进程内写入 probe runtime。
- 修改目标函数入口指令，使其跳转到用户态 probe stub。
- 在用户态完成参数采集、事件写入、动态开关、多探针管理等功能。

当前主线聚焦：

```text
x86_64 + 动态库函数 + entry probe + return probe + event buffer + CLI 管理
```

`remote_mmap()` 已由成员 B 接入，并已按本项目接口完成构建集成。当前主线已经完成单线程/多线程的 `attach -> events -> detach` 基础闭环验证，后续重点转向 benchmark、展示材料和文档收口。

## 2. 当前功能状态

| 赛题要求 | 状态 | 说明 |
| --- | --- | --- |
| F1 动态库函数探针插入 | 进行中 | 已有符号解析、远程 mmap、入口 patch、trampoline、entry stub 代码生成；`getpid/malloc` 的单线程和多线程验证已跑通 |
| F2 函数参数获取 | 进行中 | entry stub 已采集 x86_64 System V ABI 前 6 个参数、tid、CLOCK_MONOTONIC 时间戳 |
| F3 函数返回值捕获 | 进行中 | entry 侧已替换返回地址并压入 shadow stack；ret_stub 已写 return event、retval 和 duration；`getpid/malloc` 返回值已完成实际验证 |
| F4 探针清理 | 进行中 | 已保存原始指令并支持入口恢复；`lp_remote_munmap()` 已接入远程 syscall；单线程/多线程 detach 已完成实测 |
| F5 探针动态开关 | 进行中 | CLI 会更新本地状态和远程 `config->enabled` |
| F6 多探针共存 | 进行中 | 本地 probe table 支持 16 个 probe |
| F7 多线程安全 | 进行中 | controller 已有 stop/resume all threads；event buffer 使用 `lock xadd` 分配 slot；shadow stack 空线程槽使用 `lock cmpxchg` 原子抢占；`getpid/malloc --ret` 多线程验证已通过 |

## 3. 目录结构

```text
lightprobe/
├── include/                    # 公共接口和数据结构
│   ├── lightprobe.h
│   ├── probe_types.h
│   ├── event.h
│   ├── runtime.h
│   ├── injector.h
│   └── controller.h
├── controller/                 # 进程控制层，成员 B 负责
│   ├── process_attach.c
│   ├── thread_control.c
│   ├── maps_parser.c
│   ├── elf_resolver.c
│   ├── remote_mem_ptrace.c
│   └── controller_stub.c
├── injector/                   # 核心插桩链路，成员 A 负责
│   ├── instruction_x86_64.c    # x86_64 指令长度解析
│   ├── patch_x86_64.c          # 入口 patch / 恢复
│   ├── trampoline_x86_64.c     # trampoline 和 stub 代码生成
│   └── probe_manager.c         # probe 生命周期管理
├── runtime/                    # 目标进程内 runtime 数据布局
│   ├── remote_layout.c
│   ├── event_buffer.c
│   ├── runtime_config.c
│   └── shadow_stack.c
├── cli/                        # 命令行工具
├── docs/                       # 设计和分工文档
└── tests/                      # 单元测试
```

## 4. 构建和测试

### 4.1 构建

```bash
make
```

生成：

```text
build/lightprobe
```

### 4.2 清理

```bash
make clean
```

### 4.3 单元测试

```bash
make test
```

当前测试内容：

- x86_64 单条指令长度解析。
- 入口 patch 覆盖长度计算。
- entry stub 代码生成。
- retprobe entry stub 代码生成。
- ret stub 代码生成。
- ret stub 缺少 shadow stack 时返回 `EINVAL`。

成功输出：

```text
instruction length tests passed
stub builder tests passed
```

### 4.4 端到端验证目标

```bash
make targets
```

当前会生成四个最小验证目标：

- `build/tests/target_getpid_loop`
- `build/tests/target_malloc_loop`
- `build/tests/target_multithread_getpid`
- `build/tests/target_multithread_malloc`

这些目标会持续调用对应函数，便于观察 entry/return 事件；其中多线程目标用于验证 shadow stack、return probe 和 event buffer 在多线程下的基本可用性。

## 5. CLI 使用说明

查看帮助：

```bash
./build/lightprobe
```

当前命令：

```text
attach
detach
enable
disable
events
list
```

### 5.1 attach：插入探针

```bash
sudo ./build/lightprobe attach --pid <pid> --lib <lib_name> --func <func_name> [--ret]
```

示例：

```bash
sudo ./build/lightprobe attach --pid 1234 --lib libc.so.6 --func getpid
sudo ./build/lightprobe attach --pid 1234 --lib libc.so.6 --func malloc --ret
```

参数说明：

- `--pid <pid>`：目标进程 PID。
- `--lib <lib_name>`：动态库名，例如 `libc.so.6`。
- `--func <func_name>`：函数名，例如 `getpid`、`malloc`。
- `--ret`：请求安装 return probe。当前 A 侧已经生成 entry/ret stub，并维护 shadow stack；attach 链路已能完成远程 runtime 分配和入口 patch。

当前 attach 内部流程：

```text
1. 解析动态库函数运行时地址。
2. 暂停目标进程所有线程。
3. 读取目标函数入口指令。
4. 按 x86_64 指令边界计算入口 patch 长度。
5. 分配远程 runtime。
6. 写入 config / event buffer / shadow stack / trampoline / entry stub / ret stub。
7. 修改目标函数入口为 jmp entry_stub。
8. 恢复目标进程线程。
9. 保存 probe 元数据到本地状态文件。
```

当前状态：`remote_mmap()` 已接入，attach 不再停在远程 runtime 分配阶段。已经完成一次如下冒烟验证：

```bash
./build/lightprobe attach --pid <child-pid> --lib libc.so.6 --func getpid --ret
```

输出示例：

```text
attached probe_id=0 pid=<pid> libc.so.6:getpid target=0x... ret=1
```

注意：在默认 `kernel.yama.ptrace_scope=1` 的系统上，普通用户只能 ptrace 自己的子进程；调试非子进程通常需要 `sudo`。

### 5.1.1 return probe 当前实现

带 `--ret` 时，entry stub 会额外执行：

```text
1. 获取当前 tid。
2. 在 shadow stack 中查找该 tid 对应线程槽。
3. 如果遇到空槽，用 lock cmpxchg 原子抢占，避免多线程首次进入时抢同一槽。
4. 保存原始返回地址和 entry timestamp。
5. 将目标函数栈顶返回地址替换为 ret_stub_addr。
```

目标函数执行 `ret` 后会进入 ret stub：

```text
1. 保存 rax 作为返回值。
2. 按 tid 从 shadow stack 弹出原始返回地址。
3. 写入 PROBE_EVENT_RETURN。
4. 计算 duration_ns = return_timestamp - entry_timestamp。
5. 恢复 rax 并跳回原始返回地址。
```

这部分目前已经通过 stub builder 单元测试验证“代码生成路径”可用，并通过单线程/多线程验证确认远程 runtime 分配、stub 写入、event 读取和 detach 链路可达。

### 5.1.2 已验证的最小闭环

目前已经通过 `sudo` 版最小闭环验证：

```text
target_getpid_loop -> attach(getpid --ret) -> events -> detach
target_malloc_loop -> attach(malloc --ret) -> events -> detach
```

当前已经确认：

- 单线程 `getpid --ret`：attach / events / detach 全部成功，`retval == target_pid`
- 单线程 `malloc --ret`：attach / events / detach 全部成功，`retval` 为有效堆地址
- 多线程 `getpid --ret`：多个不同 `tid` 的 entry/return 事件成对出现
- 多线程 `malloc --ret`：多个不同 `tid` 的 entry/return 事件成对出现
- 所有验证完成后 `list` 为空，说明本地 probe 状态能正确清理

### 5.1.3 最小 demo 命令链

推荐先构建主程序和验证目标：

```bash
make clean
make
make targets
```

手动演示 `getpid --ret`：

```bash
./build/tests/target_getpid_loop &
target_pid=$!

sudo ./build/lightprobe attach --pid "$target_pid" --lib libc.so.6 --func getpid --ret
sudo ./build/lightprobe events --pid "$target_pid" --func getpid --limit 8
sudo ./build/lightprobe detach --pid "$target_pid" --func getpid
./build/lightprobe list

kill "$target_pid"
```

实际输出会包含三类关键证据：

```text
attached probe_id=0 pid=<pid> libc.so.6:getpid target=0x... ret=1
type=entry  ... tid=<tid> args=[...]
type=return ... tid=<tid> retval=0x<pid> duration=<ns>
detached pid=<pid> func=getpid
ID  PID      ENABLED  RET  TARGET              LIB:FUNC
```

其中 `retval=0x<pid>` 对应目标进程 PID，说明 return probe 捕获到 `getpid()` 的真实返回值；最后 `list` 只剩表头，说明本地 probe 状态已经清理。

当前已经补充四个端到端验证脚本：

- `tests/scripts/run_getpid_probe_smoke.sh`
- `tests/scripts/run_malloc_probe_smoke.sh`
- `tests/scripts/run_multithread_getpid_probe_smoke.sh`
- `tests/scripts/run_multithread_malloc_probe_smoke.sh`

示例：

```bash
./tests/scripts/run_getpid_probe_smoke.sh
./tests/scripts/run_malloc_probe_smoke.sh
./tests/scripts/run_multithread_getpid_probe_smoke.sh
./tests/scripts/run_multithread_malloc_probe_smoke.sh
```

如果需要非交互传入 sudo 密码，可以使用环境变量或第一个参数：

```bash
LIGHTPROBE_SUDO_PASSWORD='<password>' ./tests/scripts/run_getpid_probe_smoke.sh
./tests/scripts/run_getpid_probe_smoke.sh '<password>'
```

脚本会自动完成：

```text
启动目标进程
sudo attach --ret
读取最近事件
sudo detach
检查本地 probe 列表
```

推荐验证顺序：

```text
1. run_getpid_probe_smoke.sh
2. run_malloc_probe_smoke.sh
3. run_multithread_getpid_probe_smoke.sh
4. run_multithread_malloc_probe_smoke.sh
```

### 5.2 detach：清理探针

```bash
sudo ./build/lightprobe detach --pid <pid> --func <func_name>
```

示例：

```bash
sudo ./build/lightprobe detach --pid 1234 --func getpid
```

功能：

- 从本地 probe 表中找到对应 probe。
- 暂停目标进程所有线程。
- 恢复目标函数入口原始指令。
- 在线程暂停期间尝试释放远程 runtime。
- 从本地 probe 表中删除该 probe。

当前限制：

- `lp_remote_munmap()` 已接入远程 `munmap` syscall，作为 detach 阶段的 best-effort cleanup。

### 5.3 enable：启用探针

```bash
sudo ./build/lightprobe enable --pid <pid> --func <func_name>
```

示例：

```bash
sudo ./build/lightprobe enable --pid 1234 --func getpid
```

功能：

- 更新本地状态文件中的 `desc->enabled = 1`。
- 写目标进程远程 runtime 中的 `config->enabled = 1`。

entry stub 每次执行时会读取 `config->enabled`：

```text
enabled = 1：记录事件
enabled = 0：跳过事件记录，直接执行 trampoline
```

### 5.4 disable：禁用探针

```bash
sudo ./build/lightprobe disable --pid <pid> --func <func_name>
```

示例：

```bash
sudo ./build/lightprobe disable --pid 1234 --func getpid
```

功能：

- 更新本地状态文件中的 `desc->enabled = 0`。
- 写目标进程远程 runtime 中的 `config->enabled = 0`。

注意：`disable` 不恢复入口 patch。函数入口仍然跳转到 entry stub，但 entry stub 会跳过事件记录。

### 5.5 events：读取事件

```bash
sudo ./build/lightprobe events --pid <pid> [--func <func_name>] [--limit <n>] [--csv]
```

示例：

```bash
sudo ./build/lightprobe events --pid 1234 --func getpid --limit 32
sudo ./build/lightprobe events --pid 1234 --func getpid --limit 32 --csv
```

参数说明：

- `--pid <pid>`：目标进程 PID。
- `--func <func_name>`：指定函数名。如果不指定，读取该 PID 下第一个 probe。
- `--limit <n>`：最多输出最近 N 条事件，默认 32，最大 4096。
- `--csv`：输出 CSV 格式，便于 benchmark 脚本处理。

普通输出示例：

```text
probe_id=0 pid=1234 libc.so.6:getpid event_buffer=0x7f... total=10 showing=10
ts=0 pid=1234 tid=0 probe=0 type=entry args=[0x0,0x0,0x0,0x0,0x0,0x0] retval=0x0 duration=0
```

CSV 输出字段：

```text
timestamp_ns,pid,tid,probe_id,event_type,arg1,arg2,arg3,arg4,arg5,arg6,retval,duration_ns
```

当前限制：

- entry event 和 return event 都已经可以从 event buffer 读取。
- `--csv` 输出适合后续 benchmark 脚本做批量统计；当前仓库只提供 smoke 级闭环验证，尚未固化完整压测报表。

### 5.6 list：查看本地 probe 表

```bash
./build/lightprobe list
```

示例输出：

```text
ID  PID      ENABLED  RET  TARGET              LIB:FUNC
0   1234     1        0    0x00007f...         libc.so.6:getpid
```

功能：

- 读取 `/tmp/lightprobe_state.bin`。
- 打印本地记录的 probe 元数据。

## 6. 本地状态文件

CLI 是多进程命令模型，例如 `attach` 和 `detach` 是两次独立执行。为了让多个命令共享 probe 元数据，lightprobe 使用本地状态文件：

```text
/tmp/lightprobe_state.bin
```

保存内容包括：

- probe id
- pid
- lib / func
- target addr
- trampoline addr
- entry stub addr
- ret stub addr
- config addr
- event buffer addr
- shadow stack addr
- original code
- enabled 状态

注意：

- 该文件只用于本机调试。
- 不应提交到仓库。
- 如果目标进程已经退出，可以手动删除该文件清理状态。

## 7. 核心实现说明

### 7.1 x86_64 入口 patch

目标函数入口会被改写为：

```asm
jmp entry_stub
```

patch 长度至少 5 字节。为了避免切断指令，项目实现了第一版 x86_64 指令长度解析：

```c
lp_x86_64_insn_len()
lp_x86_64_calc_patch_len()
```

它会按完整指令边界累计覆盖长度。

### 7.2 trampoline

trampoline 内容：

```text
被覆盖的原始指令
跳回 target_addr + original_len
```

为了避免污染 `rax` 等常用寄存器，当前绝对跳转使用保存/恢复 `r11` 的序列。

### 7.3 entry stub

entry stub 当前会：

```text
保存 flags 和通用寄存器
读取 config->enabled
enabled 为 0 时跳过记录
使用 lock xadd 获取 event buffer slot
写入 entry event
通过 gettid syscall 写入 tid
通过 clock_gettime(CLOCK_MONOTONIC) syscall 写入 timestamp_ns
记录前 6 个参数
恢复寄存器
跳转 trampoline
```

x86_64 System V ABI 前 6 个参数：

```text
arg1 = rdi
arg2 = rsi
arg3 = rdx
arg4 = rcx
arg5 = r8
arg6 = r9
```

### 7.4 event buffer

目标进程内每个 probe 有独立 event buffer：

```c
struct lp_event_buffer {
    volatile uint64_t write_index;
    uint64_t capacity;
    struct probe_event events[4096];
};
```

entry stub 使用 `lock xadd` 更新 `write_index`，从而支持多线程同时写事件。

## 8. benchmark 与展示材料

当前项目已经具备可展示的最小 benchmark 输入和事件输出能力：

- 验证目标：`getpid`、`malloc`。
- 线程模型：单线程、多线程。
- 事件类型：entry event、return event。
- 输出格式：普通文本和 CSV。
- 关键字段：`timestamp_ns`、`pid`、`tid`、`probe_id`、参数、`retval`、`duration_ns`。

推荐展示流程：

```bash
make clean
make
make targets
make test

./tests/scripts/run_getpid_probe_smoke.sh
./tests/scripts/run_malloc_probe_smoke.sh
./tests/scripts/run_multithread_getpid_probe_smoke.sh
./tests/scripts/run_multithread_malloc_probe_smoke.sh
```

简单性能说明：

- 事件写入使用目标进程内 ring buffer，entry/return stub 通过 `lock xadd` 获取写入 slot。
- ring buffer 固定容量为 4096 条事件，超过容量后按 `write_index % capacity` 覆盖旧事件。
- `events --csv` 可以作为后续 benchmark 输入，用于统计事件数量、线程分布和 return probe duration。
- 当前实现优先保证功能正确性。`gettid` 与 `clock_gettime` 仍通过 syscall 获取，后续可以改为 vDSO 或缓存策略降低开销。

## 9. 当前已知限制与边界

- 当前支持范围是 Linux/x86_64 用户态动态库函数。
- hook 安装、远程内存读写和远程 syscall 依赖 `ptrace`，调试非子进程通常需要 `sudo` 或合适的 ptrace 权限。
- return probe 已支持多线程基础场景，但它不是完整工业级 unwinder。
- shadow stack 使用固定线程槽和固定嵌套深度，极高并发、极深递归需要继续扩展。
- 当前对信号中断、异步取消、异常控制流等复杂场景只做基础 fallback，不作为第一版能力承诺。
- x86_64 指令长度解析是第一版，覆盖常见函数入口，复杂 VEX/EVEX 指令暂未支持。
- 当前远程 runtime 采用 RWX 映射，后续可以拆分为 RW 数据区和 RX 代码区。

## 10. 后续功能扩展方向

优先级从高到低：

```text
1. 固化 benchmark：用 events --csv 输出批量统计事件数量、线程分布和 duration。
2. 扩展验证函数：补 strlen/write/free 等更贴近业务场景的动态库函数。
3. 加强压力测试：递归、更多线程、更高调用频率、长时间运行。
4. 优化性能：减少 syscall 次数，优化 shadow stack 线程槽查找，降低 stub 热路径开销。
5. 加强健壮性：处理信号中断、线程退出、异常返回路径和更复杂指令入口。
6. 改进安全边界：远程 runtime 拆分 RW/RX 映射，减少 RWX 页面。
```

## 11. 相关文档

- `docs/member_a_runtime_layout.md`
- `docs/member_b_task.md`
