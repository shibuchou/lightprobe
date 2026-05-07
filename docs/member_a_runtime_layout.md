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

- `timestamp_ns` 暂时写 0。
- `tid` 暂时写 0。
- 暂时只写 entry event。
- 原始指令长度暂时按 5 字节最小 patch 长度处理，后续需要指令长度解析。
- return probe 需要替换栈顶返回地址并维护 shadow stack。

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

该功能依赖成员 B 的 `lp_remote_read()`。在 controller 仍是桩实现时，如果没有已安装 probe，会报告 `probe not found`；如果有 probe 但 remote read 未接通，会报告对应 `errno`。
