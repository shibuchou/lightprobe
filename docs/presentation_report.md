# lightprobe 项目汇报报告

> 2026 年全国大学生计算机系统能力大赛 · OS 功能挑战赛道 · proj40  
> 赛题：轻量级用户态动态探针 / Lightweight Dynamic Probe for User Space

---

## 一、我们要解决什么问题

### 背景

在 Linux 系统上对运行中的进程进行函数级观测，主流方案有：

| 方案 | 依赖 | 痛点 |
|------|------|------|
| **uprobe + BPF** | 内核 CONFIG_UPROBES、BPF 虚拟机、BTF 调试信息 | 内核配置要求高，需要编译 BPF 程序，调试困难 |
| **SystemTap** | 内核符号表、完整编译工具链 | 重型依赖，生产环境常不满足 |
| **gdb 手动调试** | 人工交互 | 无法自动化、不能批量采集 |

**核心矛盾**：想快速观测某个进程的某个函数调用（参数、返回值、耗时），要么需要复杂的内核基础设施，要么只能手动断点调试。

### 我们的目标

实现一个**不依赖内核 uprobe、不写 BPF 代码**的纯用户态动态探针——只需一个可执行文件，就能对任意运行中的进程注入探针、采集函数调用数据、事后恢复原状。

---

## 二、我们做到了什么

### 核心功能（已全部验证通过）

| 功能 | 说明 | 状态 |
|------|------|------|
| **Entry Probe** | 捕获函数入口的前 6 个参数、PID/TID、时间戳 | 通过 |
| **Return Probe** | 捕获函数返回值和执行耗时（纳秒级） | 通过 |
| **动态库 Hook** | 对 `libc.so.6` 等动态库函数注入探针（getpid/malloc/strlen/write） | 通过 |
| **动态开关** | `enable`/`disable` 实时开关事件记录，不中断目标进程 | 通过 |
| **事件导出** | CSV 格式导出，支持 `--limit` 和过滤 | 通过 |
| **多线程安全** | Shadow stack 保证多线程 return probe 正确配对 | 通过 |
| **IFUNC 支持** | 处理 glibc IFUNC（如 strlen），直指真实实现地址 | 通过 |
| **干净卸载** | Detach 恢复原始指令，释放远程内存 | 通过 |

### CLI 命令一览

```text
attach   --pid <pid> --lib <lib> --func <func> [--addr <addr>] [--ret]
detach   --pid <pid> --func <func>
enable   --pid <pid> --func <func>
disable  --pid <pid> --func <func>
events   --pid <pid> [--func <func>] [--limit <n>] [--csv]
list
```

### 验证矩阵

| 验证项 | 结果 |
|--------|------|
| 单元测试（指令解析 + stub builder） | 通过 |
| 单线程 getpid + ret | 通过 |
| 单线程 malloc + ret | 通过 |
| 多线程 getpid + ret | 通过 |
| 多线程 malloc + ret | 通过 |
| strlen + ret（IFUNC 场景） | 通过 |
| write + ret | 通过 |
| 压力测试 benchmark | 通过 |

---

## 三、怎么实现的

### 整体架构

```
┌──────────────────────────────────────────────┐
│  CLI 层 (cli/)                                │
│  接收用户命令 → attach/detach/events/list    │
├──────────────────────────────────────────────┤
│  Controller 层 (controller/)                  │
│  ptrace 进程控制 · 线程管理 · /proc/maps 解析 │
│  ELF 符号解析 · 远程内存读写 · 远程 syscall   │
├──────────────────────────────────────────────┤
│  Injector 层 (injector/)                      │
│  指令长度解析 · 入口 patch · Trampoline 生成  │
│  Entry/Ret Stub · Probe 生命周期管理          │
├──────────────────────────────────────────────┤
│  Runtime 层 (runtime/)                        │
│  远程配置 · Ring Buffer · Shadow Stack        │
│  Trampoline 代码 · Entry/Ret Stub 代码         │
└──────────────────────────────────────────────┘
```

### 核心技术路径

```
1. ptrace ATTACH   → 暂停目标进程所有线程
2. 远程 mmap       → 在目标进程空间分配 RWX 内存
3. 注入 runtime    → 写入 trampoline + stub + event_buffer + shadow_stack
4. 保存原始指令    → 读取函数入口处 N 字节原始指令
5. 写入跳转        → patch 函数入口为 jmp → entry_stub
6. ptrace DETACH   → 恢复目标进程运行
7. 触发            → 目标调用函数 → 自动跳入 entry_stub → 采集参数
                 → 压入 shadow_stack → 跳转 trampoline → 执行原指令
                 → 函数返回 → ret_stub → 采集返回值/耗时 → 回栈
8. 读取事件        → CLI events 从 ring buffer 读取事件
9. detach          → 恢复原始指令，释放远程内存
```

### 关键技术设计

**Shadow Stack（多线程 return probe 核心）**：
- 为每个线程预分配独立槽位
- entry_stub 进入时将返回地址压入当前线程槽
- ret_stub 触发时从当前线程槽弹出，确保 entry/return 配对正确
- 解决了多线程场景下 return probe 的归属问题

**Ring Buffer（事件采集）**：
- 固定容量环形缓冲，生产者（stub 代码）写入事件
- 消费者（CLI events）从外部通过 ptrace 远程读取
- 无锁设计，避免目标态锁竞争

**IFUNC 处理**：
- glibc 中 `strlen` 等函数使用 IFUNC 机制
- ELF 符号解析只能拿到 resolver 地址，不是真实实现
- 通过从目标程序输出中获取 IFUNC 实际解析地址，用 `--addr` 直接指定

---

## 四、演示测试流程

### 环境要求

- Linux x86_64
- 已编译 `make && make targets`
- sudo 权限（ptrace 需要）

### Demo 1：getpid 单线程探针（30 秒演示）

```bash
# 1. 启动目标程序（后台循环调用 getpid）
./build/tests/target_getpid_loop &
TARGET_PID=$!

# 2. 安装探针（带 return probe）
sudo ./build/lightprobe attach --pid $TARGET_PID --lib libc.so.6 --func getpid --ret

# 3. 读取事件
sudo ./build/lightprobe events --pid $TARGET_PID --func getpid --limit 8

# 4. 卸载探针
sudo ./build/lightprobe detach --pid $TARGET_PID --func getpid

# 5. 停止目标
kill $TARGET_PID
```

**预期输出解读**：
```
attached probe_id=0 pid=<pid> libc.so.6:getpid target=0x... ret=1
type=entry  ... args=[...]                           ← 入口事件：记录参数
type=return ... retval=0x<pid> duration=<ns>         ← 返回事件：retval == 目标PID，证明捕获正确
detached pid=<pid> func=getpid
ID  PID  ENABLED  RET  TARGET  LIB:FUNC              ← list 只剩表头，状态已清理
```

### Demo 2：malloc 多线程探针（备用/进阶演示）

```bash
# 启动多线程目标（4 线程循环 malloc/free）
./build/tests/target_multithread_malloc &
TARGET_PID=$!

# 安装探针
sudo ./build/lightprobe attach --pid $TARGET_PID --lib libc.so.6 --func malloc --ret

# 读取事件（多线程场景）
sudo ./build/lightprobe events --pid $TARGET_PID --func malloc --limit 16

# 卸载
sudo ./build/lightprobe detach --pid $TARGET_PID --func malloc
kill $TARGET_PID
```

**多线程验证点**：
- 输出中会出现多个不同的 `tid`
- 每个 tid 的 entry 和 return 成对出现
- `retval` 均为有效堆地址（非零）

### 一键 smoke 测试

```bash
./tests/scripts/run_getpid_probe_smoke.sh
./tests/scripts/run_multithread_malloc_probe_smoke.sh
```

---

## 五、总结

| 维度 | 说明 |
|------|------|
| **创新点** | 纯 ptrace 实现用户态动态探针，不依赖 uprobe/BPF 内核基础设施 |
| **完成度** | 7 类函数验证通过，单线程/多线程/IFUNC 场景全覆盖 |
| **工程性** | 模块化分层设计，CLI 接口清晰，有单元测试 + 端到端验证 + benchmark |
| **赛事定位** | 2026 全国大学生计算机系统能力大赛 OS 功能挑战赛道 proj40 |
| **当前阶段** | 初赛（提交截止 2026-06-30） |
