# 成员 A 设计说明：远程 runtime 与 return probe

本文档从成员 A 的视角整理当前 `lightprobe` 的 runtime 布局、entry/return probe 设计、已验证能力和后续工作重点。

## 1. 成员 A 当前负责范围

成员 A 当前主责：

- `injector/`：入口 patch、trampoline、entry/ret stub 机器码生成、probe 生命周期管理。
- `runtime/`：远程 runtime 内存布局、事件缓冲区、shadow stack、enable/disable 配置区。
- `tests/targets/` 与 `tests/scripts/`：最小验证目标和闭环验证脚本。
- `README.md` 与本说明：整理当前可运行能力、验证矩阵和后续计划。

成员 B 当前主责：

- `controller/`：进程 attach/detach、线程暂停恢复、符号解析、远程读写、远程 `mmap/munmap`、远程 syscall。

## 2. 远程 runtime 布局

单个 probe 在目标进程内占用一整块远程内存：

```text
base + config_offset        struct lp_runtime_config
base + event_buffer_offset  struct lp_event_buffer
base + shadow_stack_offset  struct lp_shadow_stack
base + trampoline_offset    trampoline code
base + entry_stub_offset    entry probe stub
base + ret_stub_offset      return probe stub
```

布局由 `runtime/remote_layout.c` 统一计算，并回填到 `struct probe_desc`：

```c
desc->runtime_base_addr
desc->runtime_size
desc->config_addr
desc->event_buffer_addr
desc->shadow_stack_addr
desc->trampoline_addr
desc->entry_stub_addr
desc->ret_stub_addr
```

当前第一版仍采用 `RWX` 远程映射，目标是先跑通功能闭环；后续可以拆分成数据区 `RW`、代码区 `RX`。

## 3. attach 安装顺序

`lp_probe_attach()` 当前顺序：

```text
1. 检查同一 pid+func 是否已存在。
2. 解析目标函数运行时地址。
3. 暂停目标进程所有线程。
4. 读取目标函数入口原始指令。
5. 分配远程 runtime。
6. 初始化 config/event_buffer/shadow_stack。
7. 构造并写入 trampoline。
8. 构造并写入 entry_stub / ret_stub。
9. patch 目标函数入口，跳转到 entry_stub。
10. 恢复目标线程并保存本地 probe 状态。
```

当前 `remote_mmap()`、`remote_munmap()`、`remote_read()`、`remote_write()` 都已接入，因此 attach/detach 的主链路已经可以真实运行。

## 4. entry probe 当前能力

当前 `entry_stub` 已完成：

- 保存/恢复通用寄存器和 flags。
- 读取 x86_64 System V ABI 前 6 个参数。
- 读取 `tid`。
- 读取 `CLOCK_MONOTONIC` 时间戳。
- 按 `config->enabled` 判断是否记录事件。
- 用 `lock xadd` 获取 event buffer slot。
- 写入 `PROBE_EVENT_ENTRY`。
- 跳转到 `trampoline_addr` 执行原始指令并回到目标函数。

事件字段中当前已稳定写入：

```text
pid
tid
probe_id
event_type = PROBE_EVENT_ENTRY
args[0..5]
timestamp_ns
```

## 5. return probe 当前设计与实现

### 5.1 设计流程

```text
1. 函数入口进入 entry_stub。
2. entry_stub 从目标函数栈顶读取原始返回地址 original_ret。
3. 按 tid 找到 shadow stack 线程槽。
4. 把 original_ret 和 entry timestamp 压入该线程槽。
5. 将栈顶返回地址改写为 ret_stub_addr。
6. 函数执行完成后，ret 跳到 ret_stub。
7. ret_stub 保存 rax 作为 retval。
8. ret_stub 按 tid 找到对应线程槽，弹出 original_ret。
9. ret_stub 写入 PROBE_EVENT_RETURN。
10. ret_stub 跳回 original_ret，恢复目标程序控制流。
```

### 5.2 shadow stack 结构

```c
struct lp_shadow_stack_entry {
    uint32_t tid;
    uint32_t depth;
    uint64_t return_addrs[LP_SHADOW_STACK_DEPTH];
    uint64_t entry_timestamps[LP_SHADOW_STACK_DEPTH];
};

struct lp_shadow_stack {
    struct lp_shadow_stack_entry entries[LP_SHADOW_STACK_THREADS];
};
```

第一版策略：

- 固定最多 `LP_SHADOW_STACK_THREADS = 256` 个线程槽。
- 每线程最多 `LP_SHADOW_STACK_DEPTH = 64` 层嵌套。
- 通过 `tid` 线性查找线程槽。
- 槽位为空时用 `lock cmpxchg` 原子抢占。

### 5.3 多线程 return probe 修复结果

多线程 `getpid --ret` 初期会在 attach 后很快崩溃，最终定位到 `ret_stub` 线程槽扫描的控制流问题：

- 单线程通常命中第一个槽，因此旧逻辑不容易暴露问题。
- 多线程需要跨槽扫描，旧版短跳控制流容易把返回路径跳坏。

当前已经修复 `ret_stub` 的线程槽扫描控制流，多线程 `getpid --ret` 和多线程 `malloc --ret` 都已经验证通过。

## 6. 当前验证矩阵

### 6.1 单元测试

当前 `make test` 包含：

```text
tests/unit/test_instruction_len.c
tests/unit/test_stub_builder.c
```

覆盖内容：

- x86_64 指令长度解析。
- patch 覆盖长度计算。
- 非 retprobe entry stub 代码生成。
- retprobe entry stub 代码生成。
- ret stub 代码生成。
- `shadow_stack_addr == 0` 时 ret stub 返回 `EINVAL`。

### 6.2 端到端测试目标

当前 `make targets` 会生成：

```text
build/tests/target_getpid_loop
build/tests/target_malloc_loop
build/tests/target_multithread_getpid
build/tests/target_multithread_malloc
```

### 6.3 闭环验证脚本

当前脚本：

```text
tests/scripts/run_getpid_probe_smoke.sh
tests/scripts/run_malloc_probe_smoke.sh
tests/scripts/run_multithread_getpid_probe_smoke.sh
tests/scripts/run_multithread_malloc_probe_smoke.sh
```

推荐验证顺序：

```text
1. 单线程 getpid --ret
2. 单线程 malloc --ret
3. 多线程 getpid --ret
4. 多线程 malloc --ret
```

### 6.4 已确认通过的验证

当前已经确认通过：

- 单线程 `getpid --ret`
  - attach 成功
  - entry/return 成对出现
  - `retval == target_pid`
  - detach 成功
- 单线程 `malloc --ret`
  - attach 成功
  - entry/return 成对出现
  - `retval` 为有效堆地址
  - detach 成功
- 多线程 `getpid --ret`
  - 多个不同 `tid` 的 entry/return 事件成对出现
  - detach 成功
- 多线程 `malloc --ret`
  - 多个不同 `tid` 的 entry/return 事件成对出现
  - detach 成功

所有闭环验证完成后，`./build/lightprobe list` 为空，说明本地 probe 状态能正确清理。

## 7. 当前实现边界

当前实现已经能支撑比赛演示和基础功能验证，但仍有边界：

- 时间戳仍使用 syscall 获取，正确性优先，性能还可优化。
- shadow stack 仍采用固定大小数组和线性扫描，后续可优化线程槽管理。
- fallback 仍然比较激进，后续可以设计更温和的错误路径。
- 目前尚未补 benchmark、批量压力测试和展示材料。

## 8. 下一步优先级

当前建议优先级：

1. 整理 README、验证脚本和提交范围。
2. 补 benchmark / 压测脚本。
3. 生成比赛展示所需的命令链和结果截图。
4. 最后再考虑性能优化和更温和的异常路径。
