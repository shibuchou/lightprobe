# 成员 B 任务说明：controller 进程控制层

本文档用于固定成员 B 的开发边界。成员 B 只负责 `controller/` 模块和必要的内部辅助文件，目标是给成员 A 的插桩链路提供稳定的进程控制、地址解析和远程内存管理能力。

## 负责范围

成员 B 负责：

- `ptrace` attach/detach。
- 暂停和恢复目标进程所有线程。
- 解析 `/proc/<pid>/maps`。
- 查找动态库加载基址。
- 解析 ELF `.dynsym/.dynstr` 符号。
- 计算 ASLR/PIE 下的运行时函数地址。
- 封装目标进程内存读写。
- 实现远程 `mmap`，为 probe stub、trampoline、config、event buffer 预留目标进程内存。

成员 B 不负责：

- 不修改 `injector/` 的 patch、trampoline、stub 逻辑。
- 不修改 `runtime/` 的事件格式和 shadow stack 结构。
- 不修改 `cli/` 命令语义。
- 不修改 `include/probe_types.h`、`include/event.h` 中的公共结构体，除非先和队长确认。

## 可修改文件

优先修改：

- `controller/controller_stub.c`
- `controller/CMakeLists.txt`
- `Makefile`

可以新增：

- `controller/process_attach.c`
- `controller/thread_control.c`
- `controller/maps_parser.c`
- `controller/elf_resolver.c`
- `controller/remote_mem.c`
- `controller/controller_internal.h`

公共接口文件：

- `include/controller.h`

原则上不要改 `include/controller.h` 的函数签名。如果确实需要新增接口，先保证原有接口仍然兼容。

## 必须实现的接口

接口定义在 `include/controller.h`。

```c
int lp_attach_process(pid_t pid);
int lp_detach_process(pid_t pid);

int lp_stop_all_threads(pid_t pid);
int lp_resume_all_threads(pid_t pid);

int lp_find_library_base(pid_t pid, const char *lib_name, uint64_t *base_addr);

int lp_resolve_symbol(
    pid_t pid,
    const char *lib_name,
    const char *symbol_name,
    uint64_t *runtime_addr
);

int lp_remote_read(pid_t pid, uint64_t remote_addr, void *buf, size_t len);
int lp_remote_write(pid_t pid, uint64_t remote_addr, const void *buf, size_t len);

int lp_remote_mmap(
    pid_t pid,
    size_t size,
    int prot,
    uint64_t *remote_addr
);
```

所有接口约定：

- 成功返回 `0`。
- 失败返回 `-1`，并设置 `errno`。
- 输入参数非法时设置 `errno = EINVAL`。
- 不在接口内部直接 `exit()`。
- 不打印大量调试信息，必要日志使用 `fprintf(stderr, ...)`，后续再统一日志模块。

## 第一阶段交付

目标：让下面命令不再因为 `Function not implemented` 失败，并能解析出 `libc:getpid` 的运行时地址。

```bash
make
sudo ./build/lightprobe attach --pid <pid> --lib libc.so.6 --func getpid
```

第一阶段至少完成：

- `lp_attach_process()`
- `lp_detach_process()`
- `lp_stop_all_threads()`
- `lp_resume_all_threads()`
- `lp_find_library_base()`
- `lp_resolve_symbol()`
- `lp_remote_read()`
- `lp_remote_write()`

`lp_remote_mmap()` 第一阶段可以先返回 `ENOSYS`，但第二阶段必须实现。

## 实现建议

### 线程暂停和恢复

遍历：

```text
/proc/<pid>/task/
```

对每个 tid：

```c
ptrace(PTRACE_ATTACH, tid, NULL, NULL);
waitpid(tid, NULL, __WALL);
```

恢复时：

```c
ptrace(PTRACE_DETACH, tid, NULL, NULL);
```

注意：

- 多线程 patch 前必须先暂停所有线程。
- 如果部分 tid attach 失败，需要清理已经 attach 的 tid。
- 目标进程可能正在创建或退出线程，失败路径要稳。

### maps 解析

读取：

```text
/proc/<pid>/maps
```

查找包含 `lib_name` 的可执行映射，例如：

```text
7fxxxxxx-7fyyyyyy r-xp ... /usr/lib/x86_64-linux-gnu/libc.so.6
```

返回动态库运行时基址。注意 ELF 的 `p_vaddr` 可能不为 0，后续要处理好：

```text
runtime_addr = load_bias + symbol_value
```

### ELF 符号解析

从 maps 中拿到动态库真实路径，解析 ELF：

- ELF header
- section header
- `.dynsym`
- `.dynstr`

查找目标符号，例如：

```text
getpid
strlen
malloc
free
```

第一阶段只要求动态符号；后续可以扩展 `.symtab`。

### 远程读写

优先使用：

```c
process_vm_readv()
process_vm_writev()
```

如果写入代码段遇到权限问题，补充：

```c
ptrace(PTRACE_PEEKDATA)
ptrace(PTRACE_POKEDATA)
```

`lp_remote_write()` 必须支持非 8 字节对齐长度，因为 patch 指令常常只有 5 字节。

### 远程 mmap

最终需要通过 ptrace 注入 syscall：

```text
mmap(NULL, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
```

返回目标进程内的远程地址，用于成员 A 写入：

- entry stub
- return stub
- trampoline
- runtime config
- event buffer
- shadow stack

## 参考资料

参考仓库在本地 `reference/` 目录下：

```text
reference/bpftime
reference/lmp
```

参考方式：

- 可以看 bpftime 的 userspace uprobe、binary rewriting、benchmark 思路。
- 可以看 lmp 里和 eBPF/uprobe/符号解析相关的实现。
- 不要把 bpftime/lmp 的大模块直接搬进本项目。
- 本项目主线是轻量级用户态动态探针，不实现完整 eBPF runtime。

## 验收命令

成员 B 每次提交前至少执行：

```bash
make clean
make
./build/lightprobe
```

有测试目标后，再执行：

```bash
sudo ./build/lightprobe attach --pid <pid> --lib libc.so.6 --func getpid
```

第一阶段验收标准：

- `make` 无 warning、无 error。
- 能 attach 并暂停目标进程所有线程。
- 能找到 `libc.so.6` 运行时基址。
- 能解析 `getpid` 的运行时地址。
- `lp_remote_read()` 能读取目标函数入口字节。
- `lp_remote_write()` 能写入目标进程内存。
- 失败路径不会让目标进程永久挂起。

