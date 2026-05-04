# lightprobe

轻量级用户态动态探针原型，面向 2026 OS 功能挑战赛“Lightweight Dynamic probe for User Space”。

当前仓库先固定成员 A 的主链路边界：

- `include/`：公共 ABI、事件格式、controller/injector/runtime 接口。
- `injector/`：x86_64 入口 patch、trampoline、probe 生命周期管理骨架。
- `runtime/`：事件缓冲区、配置区、shadow stack 数据结构骨架。
- `cli/`：`attach/detach/enable/disable/events` 命令框架。
- `controller/`：成员 B 后续替换的进程控制层接口桩。

## 构建

```bash
make
```

如果环境里有 CMake，也可以使用：

```bash
cmake -S . -B build
cmake --build build
```

## 计划中的命令

```bash
sudo ./build/lightprobe attach --pid <pid> --lib libc.so.6 --func getpid --ret
sudo ./build/lightprobe disable --pid <pid> --func getpid
sudo ./build/lightprobe enable --pid <pid> --func getpid
sudo ./build/lightprobe events --pid <pid>
sudo ./build/lightprobe detach --pid <pid> --func getpid
```

当前 `controller/` 仍是桩实现，真实 attach、maps/ELF 解析、远程 mmap 接入后，`injector/` 的 patch 主链路即可进入最小闭环验证。
