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

`remote_mmap()` 仍由成员 B 继续实现，因此真实 attach 链路目前会在远程 runtime 分配阶段返回 `ENOSYS`。

## 2. 当前功能状态

| 赛题要求 | 状态 | 说明 |
| --- | --- | --- |
| F1 动态库函数探针插入 | 进行中 | 已有符号解析、入口 patch、trampoline、entry stub 代码生成；等待 `remote_mmap()` 完成后联调 |
| F2 函数参数获取 | 进行中 | entry stub 已采集 x86_64 System V ABI 前 6 个参数、tid、CLOCK_MONOTONIC 时间戳 |
| F3 函数返回值捕获 | 进行中 | entry 侧已替换返回地址并压入 shadow stack；ret_stub 已写 return event、retval 和 duration；等待完整 attach 后实测 |
| F4 探针清理 | 进行中 | 已保存原始指令并支持入口恢复；已预留 best-effort 远程 runtime munmap 接口 |
| F5 探针动态开关 | 进行中 | CLI 会更新本地状态和远程 `config->enabled` |
| F6 多探针共存 | 进行中 | 本地 probe table 支持 16 个 probe |
| F7 多线程安全 | 进行中 | controller 已有 stop/resume all threads；event buffer 使用 `lock xadd` 分配 slot；shadow stack 空线程槽使用 `lock cmpxchg` 原子抢占 |

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
- `--ret`：请求安装 return probe。当前 A 侧已经生成 entry/ret stub，并维护 shadow stack；真实运行仍等待成员 B 的 `remote_mmap()` 接入后联调。

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

注意：当前 `remote_mmap()` 尚未完成，因此 attach 会在第 5 步失败并返回：

```text
Function not implemented
```

这是当前阶段的预期行为。

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

这部分目前已经通过 stub builder 单元测试验证“代码生成路径”可用，但还没有经过真实目标进程执行验证。

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

- `lp_remote_munmap()` 当前为 `ENOSYS` 占位，真实释放依赖成员 B 后续实现。

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

- 当前只写 entry event。
- return event 已在 ret_stub 中生成，但等待 `remote_mmap()` 完成后实测。

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

## 8. 当前已知限制

- `remote_mmap()` 尚未完成，真实 attach 链路还不能完整跑通。
- `remote_munmap()` 尚未完成，detach 当前只会 best-effort 调用。
- return probe 已完成控制流和 return event 生成，等待完整 attach 后实测验证。
- 远程 runtime 暂未 unmap。
- x86_64 指令长度解析是第一版，覆盖常见函数入口，复杂 VEX/EVEX 指令暂未支持。
- 当前只支持 x86_64。

## 9. 后续计划

优先级从高到低：

```text
1. 等成员 B 完成 remote_mmap 后做 getpid 最小闭环联调。
2. 完成 return probe。
3. 补 timestamp_ns 和 tid。
4. 完善 detach 清理远程 runtime。
5. 增加测试目标程序和 benchmark。
6. 完善多线程压力测试。
```

## 10. 相关文档

- `docs/member_a_runtime_layout.md`
- `docs/member_b_task.md`
