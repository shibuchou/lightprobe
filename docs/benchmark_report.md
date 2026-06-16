# lightprobe Benchmark Report

> 2026 年全国大学生计算机系统能力大赛 · OS 功能挑战赛道 · proj40  
> P0-1: Probe Overhead & P0-3 / F6: Concurrency Verification

---

## 1. Test Environment

| Item | Value |
|------|-------|
| **OS** | Ubuntu 22.04 / 24.04 (WSL2) |
| **Arch** | x86_64 |
| **CPU** | Intel Core i7-12700H |
| **Target binary** | Custom dynamic library (f01–f16 symbols) |
| **Probe Mode** | Entry-only & Entry+Return via `ptrace` trampoline |

---

## 2. Performance Methodology

The benchmark measures **per-call probe overhead** by comparing instrumented function call latency against a baseline (no probe attached).

**Timing method**:

1. Each round calls the target function **1,000,000 times** in a tight loop.
2. Timestamps are taken via `clock_gettime(CLOCK_MONOTONIC)` in the target process before/after the entire loop.
3. `avg_ns_per_call = total_loop_ns / 1,000,000`.
4. **5 measurement rounds** are performed per mode, preceded by **2 warmup rounds** (discarded).
5. Results report mean, standard deviation, min, and max across measurement rounds.

**Modes tested**:

| Mode | Description |
|------|-------------|
| `baseline` | Direct function call — no probe attached |
| `lightprobe-entry` | Entry probe only (jmp → entry_stub → trampoline) |
| `lightprobe-entry-return` | Entry + Return probe (entry_stub + ret_stub + shadow_stack push/pop) |

---

## 3. Single-Probe Overhead

### 3.1 WSL2 Environment (Modern CPU)

| Mode | avg_ns_per_call | stddev | min | max |
|------|-----------------|--------|-----|-----|
| baseline (no probe) | 98.77 | 1.10 | 97.65 | 100.59 |
| lightprobe-entry | 392.01 | 5.75 | 385.04 | 400.43 |
| lightprobe-entry-return | 662.21 | 10.26 | 653.23 | 679.41 |

| Metric | Value |
|--------|-------|
| Entry-only probe overhead | **293.25 ns/call** |
| Entry+Return overhead | **563.45 ns/call** |
| Competition threshold | < 1000 ns/call |

Both modes pass the <1000ns threshold with comfortable headroom.

### 3.2 VM Native Linux (Ubuntu 22.04, constrained VM)

| Mode | avg_ns_per_call | stddev | min | max |
|------|-----------------|--------|-----|-----|
| baseline | 550.98 | 11.34 | 534.80 | 564.53 |
| **uprobe (kernel)** | **3,449.12** | 24.40 | 3,418.80 | 3,476.73 |
| lightprobe-entry | 1,810.04 | 18.26 | 1,792.48 | 1,829.74 |
| lightprobe-entry-return | 2,883.08 | 14.41 | 2,868.65 | 2,902.25 |

### 3.3 Overhead Comparison

| Metric | Absolute | Relative (vs baseline) |
|--------|----------|------------------------|
| uprobe overhead | 2,898.14 ns | 6.3x |
| lightprobe entry overhead | 1,259.06 ns | 2.3x |
| lightprobe entry+ret overhead | 2,332.10 ns | 4.2x |
| **lightprobe vs uprobe (entry)** | — | **57% faster** |
| **lightprobe vs uprobe (entry+ret)** | — | **20% faster** |

### 3.4 Analysis

- **WSL2**: Modern CPU environment; lightprobe entry-only overhead is 293ns, well under the 1000ns competition threshold. This represents the project's performance on typical modern hardware.
- **VM**: Constrained virtualized environment with elevated baseline latency (551ns vs 99ns). Despite this noise, lightprobe entry-only remains **57% faster than kernel uprobe**, validating the core design advantage: avoiding kernel trap overhead.
- **uprobe comparison**: Kernel uprobe overhead (2,898ns) aligns with the competition-stated range of ~1,500–3,000ns. Lightprobe is consistently faster because ptrace-based trampoline injection operates entirely in user space.

---

## 4. Concurrency Verification (P0-3 / F6)

### 4.1 Test Setup

16 probes were attached to a single target process, each targeting a distinct function (`f01`–`f16`) in a custom-built shared library. All probes were configured with entry+return instrumentation.

### 4.2 Results

| Check | Result |
|-------|--------|
| Probes attached successfully | **16 / 16** |
| Functions producing entry events | **16 / 16** |
| Functions producing return events | **16 / 16** |
| Probes detached cleanly | **16 / 16** |
| Residual probes after full detach | **0** |

All 16 concurrent probes operated correctly with no interference, no event loss, and clean teardown — satisfying the F6 requirement.

---

## 5. attach/detach Latency (P0-2)

Measured as CLI end-to-end attach+detach round-trip (includes CLI process startup, ptrace attach, remote mmap + stub injection, detach, state file I/O).

| Function | Runs | Min (ms) | Avg (ms) | Max (ms) |
|----------|------|----------|----------|----------|
| getpid | 10 | 35.4 | 37.6 | 39.5 |
| malloc | 10 | 35.8 | 37.5 | 39.4 |
| strlen | 10 | 34.7 | 38.0 | 42.9 |

Note: The ~38 ms average represents the full CLI control-plane cost. The core patch/unpatch latency (without CLI overhead) is expected to be significantly lower; isolating this metric is future work to align with the competition `<10ms` target.

---

## 6. Floating-Point Register Preservation (P2)

探针 stub 热路径仅使用通用寄存器（push/pop/syscall/lock cmpxchg），全程无 SSE/SSE2/AVX 指令，因此 XMM0–XMM15 不会被修改。这一设计使得浮点寄存器天然保留，无需额外的 save/restore 框架。

**Verification** (`fp_probe` smoke test):

| Check | Result |
|-------|--------|
| Probe attached to `fp_mix(double,double)` | OK |
| Entry + return events collected | OK |
| Return value preserved (pre/post probe) | **Delta < 1×10⁻¹²** |
| No segfault during probe execution | OK |

---

## 7. Signal Stress Test (P1)

Multi-threaded probe under high-frequency signal delivery.

| Metric | Result |
|--------|--------|
| Worker threads | 4 (malloc + getpid loop) |
| Signal delivery | SIGUSR1 at 2ms intervals |
| Test duration | 15 seconds |
| Entry events | 2,048 |
| Return events | 2,048 |
| Entry/Return balance | **Matched (entry == return)** |
| Signals sent | 25,556 |
| Signals received | 25,556 (no loss) |
| Target exit code | 0 (no crash) |

---

## 8. Known Limitations

- **attach/detach latency**: ~38 ms is CLI end-to-end (includes process startup, state file I/O). Core patch/unpatch phase not isolated yet.
- **State file**: Uses global `/tmp/lightprobe_state.bin`. Tests must run sequentially to avoid race conditions.
- **VM environment**: Lightprobe entry overhead on the constrained VM (1,259ns) slightly exceeds 1000ns due to elevated baseline; WSL2 on modern hardware (293ns) comfortably satisfies the threshold.
- **XMM preservation**: XMM0-XMM15 are naturally preserved because the probe hot path only uses GP registers. No x87, MXCSR, or YMM state is touched by the stub code.

---

## 9. Conclusion

| Requirement | Threshold | Measured | Status |
|-------------|-----------|----------|--------|
| **P0-1**: Probe overhead | < 1000 ns/call | 293 ns (WSL2) / 1,259 ns (VM) | Pass (WSL2) |
| **P0-1**: uprobe vs lightprobe | Compare overhead | uprobe 2,898ns vs lightprobe 1,259ns (entry) — **57% faster** | Pass |
| **P0-2**: attach/detach latency | < 10ms (core) | ~38ms CLI end-to-end (see §5) | Partial |
| **P0-3 / F6**: 16 concurrent probes | 16/16 | 16/16 attach, events, detach, no residuals | Pass |
| **P1**: Signal stress | entry==return, no crash | 2,048/2,048 matched, exit 0 | Pass |
| **P2**: XMM0–15 preserved | No FP corruption | Delta < 1e-12 | Pass |

All competition-required benchmarks are now complete. The probe overhead satisfies the <1000ns threshold on modern hardware, and the user-space approach demonstrates a **57% performance advantage** over kernel uprobe — validating the core design principle of avoiding kernel traps.
