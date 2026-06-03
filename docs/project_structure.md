# 项目结构说明

本文档用于快速理解 `lightprobe` 仓库中各目录的职责边界。

## 顶层目录

```text
lightprobe/
├── cli/
├── controller/
├── injector/
├── runtime/
├── include/
├── tests/
├── docs/
├── Makefile
└── README.md
```

## cli/

命令行入口层，负责把用户命令转换为 probe 管理操作。

当前命令：

- `attach`：安装 probe。
- `detach`：卸载 probe 并恢复目标函数入口。
- `enable` / `disable`：动态开关事件记录。
- `events`：读取目标进程内 event buffer。
- `list`：查看本地 probe 状态。

关键文件：

- `cli/main.c`
- `cli/cmd_attach.c`
- `cli/cmd_detach.c`
- `cli/cmd_events.c`
- `cli/cmd_list.c`

## controller/

进程控制层，主要负责和目标进程交互。

职责：

- 通过 `ptrace` attach/detach 目标进程。
- 暂停和恢复目标进程所有线程。
- 解析 `/proc/<pid>/maps` 和 ELF 符号。
- 远程读写目标进程内存。
- 通过远程 syscall 完成 `mmap/munmap`。

关键文件：

- `process_attach.c`
- `thread_control.c`
- `maps_parser.c`
- `elf_resolver.c`
- `remote_mem_ptrace.c`
- `remote_syscall.c`

## injector/

成员 A 的核心实现区域，负责插桩控制流。

职责：

- 解析 x86_64 指令长度，避免 patch 截断指令。
- 保存和恢复目标函数入口原始指令。
- 生成 trampoline。
- 生成 entry stub 和 ret stub。
- 管理 probe 生命周期和本地状态文件。

关键文件：

- `instruction_x86_64.c`
- `patch_x86_64.c`
- `trampoline_x86_64.c`
- `probe_manager.c`

## runtime/

定义目标进程内的远程 runtime 数据布局。

远程 runtime 包含：

- `runtime_config`：probe id、enabled、地址配置。
- `event_buffer`：固定容量 ring buffer。
- `shadow_stack`：return probe 的线程返回地址栈。
- `trampoline`：执行被覆盖的原始指令并跳回目标函数。
- `entry_stub` / `ret_stub`：采集 entry/return 事件。

关键文件：

- `remote_layout.c`
- `runtime_config.c`
- `event_buffer.c`
- `shadow_stack.c`

## include/

公共头文件目录，定义模块之间的接口和结构体。

关键文件：

- `probe_types.h`：`probe_desc`、`lp_probe_spec`。
- `event.h`：entry/return 事件格式。
- `runtime.h`：远程 runtime 布局接口。
- `controller.h`：controller 对外接口。
- `injector.h`：x86_64 patch/stub 构造接口。

## tests/

测试和演示资产目录。

```text
tests/
├── unit/
├── targets/
└── scripts/
```

- `unit/`：单元测试。
- `targets/`：端到端验证目标程序。
- `scripts/`：smoke、stress、benchmark 脚本。

当前验证目标：

- `target_getpid_loop`
- `target_malloc_loop`
- `target_multithread_getpid`
- `target_multithread_malloc`
- `target_strlen_loop`
- `target_write_loop`
- `target_probe_stress`

## docs/

设计、分工、验证和展示材料。

- `member_a_runtime_layout.md`：成员 A 设计说明。
- `member_b_task.md`：成员 B 任务说明。
- `verification_and_benchmark.md`：验证与 benchmark 说明。
- `project_structure.md`：本文档。
- `figures/`：展示图示源文件和导出文件。
