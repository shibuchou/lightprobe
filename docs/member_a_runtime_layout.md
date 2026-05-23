# 成员 A 设计说明：远程 runtime 布局

本文档记录单个 probe 在目标进程中的远程内存布局。该布局由成员 A 的 `runtime/remote_layout.c` 统一计算，成员 B 只需要提供 `lp_remote_mmap()` 和 `lp_remote_write()`。

## 分配方式

每个 probe 使用一整块远程内存：

```text
lp_remote_mmap(pid, layout.size, PROT_READ | PROT_WRITE | PROT_EXEC, &base)
```

当前第一版采用 RWX，目的是先跑通最小闭环。后续可以优化为：

- 数据区：RW
- stub/trampoline：RX

## 内存布局

从 `base` 开始按 16 字节对齐排列：

```text
base + config_offset        struct lp_runtime_config
base + event_buffer_offset  struct lp_event_buffer
base + shadow_stack_offset  struct lp_shadow_stack
base + trampoline_offset    trampoline code
base + entry_stub_offset    entry probe stub
base + ret_stub_offset      return probe stub
```

相关地址会写回 `struct probe_desc`：

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

## attach 安装顺序

`lp_probe_attach()` 当前顺序：

```text
1. 检查同一 pid+func 是否已存在。
2. 调用 lp_resolve_symbol() 得到 target_addr。
3. 暂停目标进程所有线程。
4. 读取目标函数入口原始指令。
5. 调用 lp_remote_mmap() 分配远程 runtime。
6. 初始化 config/event_buffer/shadow_stack。
7. 构造 trampoline，并写入目标进程。
8. patch 目标函数入口，跳转 entry_stub_addr。
9. 恢复目标线程。
10. 保存本地 probe 状态文件。
```

## 当前限制

当前代码已经完成 remote layout、trampoline 写入和第一版 `entry_stub` 写入。`entry_stub` 当前能力：

```text
target function -> entry_stub -> event buffer -> trampoline -> target+original_len
```

它会：

- 保存/恢复通用寄存器和 flags。
- 读取前 6 个参数。
- 按 `config->enabled` 判断是否记录事件。
- 用 `lock xadd` 获取 event buffer slot。
- 写入 entry event、pid、probe_id、前 6 个参数。
- 跳转到 `trampoline_addr` 执行原始指令并回到目标函数。

当前限制：

- `timestamp_ns` 使用 `clock_gettime(CLOCK_MONOTONIC)` syscall 采集，正确性优先，后续可优化为 vDSO/rdtsc。
- `tid` 使用 `gettid` syscall 采集。
- entry stub 会写 `PROBE_EVENT_ENTRY`；带 `--ret` 时 ret stub 会写 `PROBE_EVENT_RETURN`。
- 已实现第一版 x86_64 指令长度解析，入口 patch 会按完整指令边界覆盖至少 5 字节。
- 指令长度解析覆盖常见 libc 函数入口指令；遇到复杂 EVEX/VEX/罕见指令时会返回错误，避免错误切断指令。
- return probe 已实现第一版栈顶返回地址替换和 shadow stack push/pop，等待真实 attach 链路联调。

## Return probe 设计

Return probe 的目标是捕获目标函数返回值，即 x86_64 System V ABI 下的 `rax`。

### 设计流程

```text
1. 函数入口进入 entry_stub。
2. entry_stub 从目标函数栈顶读取原始返回地址：
   original_ret = *(uint64_t *)rsp
3. entry_stub 按 tid 找到 shadow stack entry。
4. 将 original_ret 和 entry timestamp 压入该线程 shadow stack。
5. 将 *(uint64_t *)rsp 改写为 ret_stub_addr。
6. entry_stub 跳转 trampoline，目标函数继续执行。
7. 目标函数 ret 时跳转到 ret_stub。
8. ret_stub 保存 rax 作为 retval。
9. ret_stub 按 tid 从 shadow stack 弹出 original_ret。
10. ret_stub 写入 return event：
    - event_type = PROBE_EVENT_RETURN
    - retval = rax
    - duration_ns = now - entry_timestamp
11. ret_stub 跳转 original_ret，恢复目标程序控制流。
```

### shadow stack 数据结构

当前 runtime 中已经预留：

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
- 通过 tid 线性查找线程槽。
- 槽位为空时用 `lock cmpxchg` 原子抢占。

### shadow stack 槽位抢占

entry stub 查找线程槽时遵循：

```text
1. 如果 slot.tid == 当前 tid，直接使用该槽。
2. 如果 slot.tid == 0，执行 lock cmpxchg(slot.tid, 0, 当前 tid)。
3. cmpxchg 成功，说明当前线程抢占该空槽。
4. cmpxchg 失败但失败值等于当前 tid，说明并发路径中该槽已属于当前线程，也可使用。
5. cmpxchg 失败且失败值属于其他 tid，继续扫描下一个槽。
```

这样避免多个线程第一次同时进入 retprobe 时都看到同一个空槽并覆盖彼此的返回地址栈。

### 当前实现状态

当前已经实现：

- entry_stub 在 `--ret` 场景下读取栈顶原始返回地址。
- 按 tid 查找 shadow stack 槽。
- 将原始返回地址和 entry timestamp 压入 shadow stack。
- 将目标函数栈顶返回地址替换为 `ret_stub_addr`。
- ret_stub 按 tid 找到 shadow stack，弹出原始返回地址并跳回。

当前 ret_stub 已写入 `PROBE_EVENT_RETURN`：

- `retval` 来自目标函数返回时的 `rax`。
- `timestamp_ns` 来自 return 时刻的 `clock_gettime(CLOCK_MONOTONIC)`。
- `duration_ns = return_timestamp - entry_timestamp`。
- `tid` 从当前线程 shadow stack 槽读取。

由于 `remote_mmap()` 尚未完成，该链路仍等待完整 attach 后实测验证。

### ret_stub 后续实现边界

完整 `ret_stub` 后续仍需继续优化：

- 当前使用 syscall 获取 tid 和时间戳，正确性优先，后续可优化性能。
- shadow stack 槽位首次分配已使用 `lock cmpxchg` 加固。
- fallback 当前使用 `ud2` 暴露 shadow stack 不一致问题，后续可设计更温和的错误路径，例如写错误事件并尽量恢复控制流。

当前代码已经预留：

- `desc->ret_stub_addr`
- `desc->shadow_stack_addr`
- `lp_x86_64_build_ret_stub()`，当前已能弹出原返回地址、写 return event 并跳回
- `PROBE_EVENT_RETURN`

下一步应在 `remote_mmap()` 完成后对 `getpid`/`malloc` return probe 做实测验证。

## 单元测试

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

这些测试验证的是 A 侧代码生成路径和基础边界条件；真实进程执行仍依赖成员 B 完成 `remote_mmap()`。

## 事件读取

CLI 已支持从远程 event buffer 读取最近事件：

```bash
sudo ./build/lightprobe events --pid <pid> --func getpid --limit 32
sudo ./build/lightprobe events --pid <pid> --func getpid --limit 32 --csv
```

读取流程：

```text
1. 从本地 probe 状态文件找到 probe_desc。
2. 使用 desc->event_buffer_addr 读取目标进程内的 event buffer header。
3. 根据 write_index 和 capacity 计算最近 N 条 ring buffer slot。
4. 逐条 lp_remote_read() 读取 struct probe_event。
5. 输出 human-readable 或 CSV 格式。
```

该功能依赖成员 B 的 `lp_remote_read()`。如果没有已安装 probe，会报告 `probe not found`；如果有 probe 但 remote read 未接通，会报告对应 `errno`。

## 动态开关

`enable/disable` 会同时更新：

```text
1. 本地 /tmp/lightprobe_state.bin 中的 desc->enabled。
2. 目标进程远程 runtime 中 config->enabled 字段。
```

远程写入地址为：

```c
desc->config_addr + offsetof(struct lp_runtime_config, enabled)
```

因此 `entry_stub` 每次执行时读取的 `config->enabled` 与 CLI 状态保持一致。该功能依赖成员 B 的 `lp_remote_write()`。

## controller 当前状态

成员 B 已接入：

- attach/detach
- stop/resume all threads
- maps parser
- ELF symbol resolver
- ptrace remote read/write

`remote_mmap()` 当前仍是 `ENOSYS` 占位实现，因此 attach 链路会在远程 runtime 分配阶段停止。等成员 B 补完 `remote_mmap()` 后，当前 A 侧的 runtime layout、stub 写入、entry patch、events 读取链路即可进入实测。

## Detach 与远程 runtime 清理设计

Detach 的核心目标是先保证目标程序控制流恢复正确，再尽力释放远程 runtime。

当前 detach 顺序：

```text
1. 暂停目标进程所有线程。
2. 恢复目标函数入口原始指令。
3. 如果 desc->runtime_base_addr/runtime_size 有效，尝试 lp_remote_munmap()。
4. 恢复目标进程所有线程。
5. 删除本地 probe 状态并保存 /tmp/lightprobe_state.bin。
```

关键原则：

- 恢复原始指令是 mandatory。
- 远程 runtime 清理是 best-effort。
- `lp_remote_munmap()` 当前允许返回 `ENOSYS`，不阻断 detach。
- 真正 `munmap` 必须在线程暂停期间执行，避免 unmap 正在执行的 stub/trampoline。

已预留接口：

```c
int lp_remote_munmap(pid_t pid, uint64_t remote_addr, size_t size);
```

成员 B 后续可通过 ptrace 注入 syscall：

```text
munmap(remote_addr, size)
```

进一步加固方向：

- detach 前检查是否有线程 RIP 落在 `[runtime_base_addr, runtime_base_addr + runtime_size)`。
- 如果线程仍在 runtime 内，可选择等待、单步或延迟释放。
- 多 probe 共存时，确保只释放该 probe 独占 runtime 区域。
