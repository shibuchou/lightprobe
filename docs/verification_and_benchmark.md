# 验证与 benchmark 说明

本文档记录当前 `lightprobe` 的验证目标、脚本入口、已验证矩阵和 benchmark 输出格式。

## 1. 构建

```bash
make clean
make
make targets
make test
```

`make test` 当前覆盖：

- x86_64 指令长度解析。
- patch 覆盖长度计算。
- entry stub 代码生成。
- retprobe entry stub 代码生成。
- ret stub 代码生成。
- ret stub 缺少 shadow stack 时返回 `EINVAL`。

成功输出：

```text
instruction length tests passed
stub builder tests passed
```

## 2. 端到端目标程序

`make targets` 会生成：

```text
build/tests/target_getpid_loop
build/tests/target_malloc_loop
build/tests/target_multithread_getpid
build/tests/target_multithread_malloc
build/tests/target_strlen_loop
build/tests/target_write_loop
build/tests/target_probe_stress
```

目标程序用途：

| 目标 | 用途 |
| --- | --- |
| `target_getpid_loop` | 验证简单无参函数和返回值 |
| `target_malloc_loop` | 验证参数和堆地址返回值 |
| `target_multithread_getpid` | 验证多线程 entry/return 和 shadow stack |
| `target_multithread_malloc` | 验证多线程参数、返回值和堆地址 |
| `target_strlen_loop` | 验证 glibc IFUNC 场景和 `--addr` |
| `target_write_loop` | 验证 `write(fd, buf, count)` 参数和返回值 |
| `target_probe_stress` | 生成高频、多线程 benchmark 输入 |

## 3. Smoke 脚本

推荐串行运行：

```bash
./tests/scripts/run_getpid_probe_smoke.sh
./tests/scripts/run_malloc_probe_smoke.sh
./tests/scripts/run_multithread_getpid_probe_smoke.sh
./tests/scripts/run_multithread_malloc_probe_smoke.sh
./tests/scripts/run_strlen_probe_smoke.sh
./tests/scripts/run_write_probe_smoke.sh
```

如果需要非交互传入 sudo 密码：

```bash
LIGHTPROBE_SUDO_PASSWORD='<password>' ./tests/scripts/run_getpid_probe_smoke.sh
```

注意：当前 CLI 使用 `/tmp/lightprobe_state.bin` 作为全局本地状态文件，端到端脚本应串行执行。

## 4. 已验证矩阵

| 验证项 | 状态 | 关键证据 |
| --- | --- | --- |
| 单元测试 | 通过 | `instruction length tests passed`、`stub builder tests passed` |
| 单线程 `getpid --ret` | 通过 | `retval == target_pid` |
| 单线程 `malloc --ret` | 通过 | `retval` 为有效堆地址 |
| 多线程 `getpid --ret` | 通过 | 多个不同 `tid` 的 entry/return 成对出现 |
| 多线程 `malloc --ret` | 通过 | 多个不同 `tid` 的 entry/return 成对出现 |
| 单线程 `strlen --ret` | 通过 | 返回值为字符串长度；通过 `--addr` 命中 IFUNC 实现 |
| 单线程 `write --ret` | 通过 | `arg1=fd`、`arg3=count`、`retval=count` |
| `strlen` stress benchmark | 通过 | 输出事件统计和 return duration 摘要 |
| `malloc` stress benchmark | 通过 | 输出事件统计和 return duration 摘要 |

## 5. Benchmark 脚本

压力测试：

```bash
./tests/scripts/run_probe_stress.sh strlen 8 8 100000
./tests/scripts/run_probe_stress.sh malloc 8 8 100000
```

参数含义：

```text
run_probe_stress.sh <func> <duration_seconds> <threads> <sleep_ns>
```

汇总输出：

```bash
./tests/scripts/run_benchmark_summary.sh strlen 8 8 100000
./tests/scripts/run_benchmark_summary.sh malloc 8 8 100000
```

输出示例：

```text
benchmark func=strlen duration=4s events=4096 entry=2048 return=2048 tids=5 return_avg_ns=1534 return_min_ns=1014 return_max_ns=109315
benchmark func=malloc duration=3s events=4096 entry=2048 return=2048 tids=5 return_avg_ns=1598 return_min_ns=1193 return_max_ns=31503
```

字段含义：

- `events`：读取到的 CSV 事件总数。
- `entry`：entry event 数量。
- `return`：return event 数量。
- `tids`：出现过的线程数。
- `return_avg_ns`：return probe 记录的平均函数执行时间。
- `return_min_ns` / `return_max_ns`：return probe 记录的最小/最大函数执行时间。

## 6. IFUNC 说明

glibc 中 `strlen` 是 IFUNC 符号。普通符号解析得到的是 resolver 符号地址，而目标程序实际调用的是 resolver 选择出的 CPU 特定实现地址。

因此 `lightprobe attach` 增加了可选参数：

```bash
--addr <addr>
```

`run_strlen_probe_smoke.sh` 会从目标程序启动日志中读取实际 `strlen` 实现地址，然后执行：

```bash
sudo ./build/lightprobe attach --pid <pid> --lib libc.so.6 --func strlen --addr <actual_addr> --ret
```

这样可以验证 `strlen --ret`，同时保留 `libc.so.6:strlen` 的展示名称。
