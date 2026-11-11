# wine-nvidia-egl-tls: NVIDIA EGL TLS Fastpath Helper

A targeted `LD_PRELOAD` helper library for 32-bit x86 glibc Linux that eliminates CPU bottlenecks caused by frequent Thread-Local Storage (TLS) lookups in NVIDIA's proprietary 32-bit EGL driver (`libnvidia-eglcore.so`).

Originally developed while optimizing 32-bit Quake Live under Wine / Proton Wayland.

---

## ⚡ Background & The Problem

When running 32-bit OpenGL games under Wine/Proton Wayland with NVIDIA's EGL driver, CPU sampling revealed that roughly **19% of main thread execution time** was spent inside the host dynamic loader (`ld-linux.so`), heavily concentrated around `XSAVEC` / `XRSTOR` register save/restores during `___tls_get_addr` calls.

NVIDIA's 32-bit `libnvidia-eglcore.so` queries TLS dynamically and frequently on hot render paths. Under standard glibc on x86, every call to the dynamic resolver (`___tls_get_addr`) incurs dynamic link-map checks, slow-path dispatching, and full vector register state preservation, averaging **~46.5 ns per call**.

Increasing the static TLS reserve (`glibc.rtld.optional_static_tls`) did not materially reduce the observed overhead. This is consistent with the driver using traditional General Dynamic TLS access sequences, which continue to invoke `___tls_get_addr` even when TLS backing storage is available in the surplus static area. The compiler-generated GD call instructions do not dynamically rewrite into Initial-Exec accesses.

---

## 🚀 How It Works

`quake-tls` interposes `___tls_get_addr` using a handwritten i386 assembly fastpath that preserves register semantics and bypasses the dynamic linker overhead for the pinned driver module:

1. **Initialization (`quake-tls.c`):**
   - At startup, pins NVIDIA's 32-bit driver module (`libnvidia-eglcore.so`) using `RTLD_NODELETE` and an open reference to ensure its module ID and thread data blocks cannot be recycled or invalidated.
   - Queries the module's dynamic TLS Module ID using `dlinfo(..., RTLD_DI_TLS_MODID)`.

2. **Zero-Overhead Fast Path (`quake-tls.S`):**
   - Matches calls targeting the pinned NVIDIA module ID.
   - Directly reads the current thread's DTV (Dynamic Thread Vector) pointer from `%gs:4`.
   - Performs boundary checks against `dtv[-1].counter` (slot capacity) and checks for unallocated slots (`TLS_DTV_UNALLOCATED` / `NULL`).
   - If initialized, resolves the TLS address directly using integer register arithmetic, completely avoiding FPU/SSE/AVX register state saving and dynamic loader calls.

3. **Safe Fallback:**
   - Any query for an uninitialized slot, newly created thread before DTV update, or other shared library falls back cleanly to glibc's original `__tls_get_addr` resolver.

---

## 📊 Measured Performance

### 1. Synthetic Microbenchmark (`test-tls`)
Measures raw Dynamic TLS lookup latency for 10,000,000 queries:
- Standard glibc resolver wrapper (`___tls_get_addr` + `XSAVEC`/`XRSTOR`): **~46.5 ns / call**
- `quake-tls` pinned-module fast path (`%gs:4` DTV direct lookup): **~3.25 ns / call**
- **Speedup: ~14.3x faster per TLS lookup**

### 2. Real-World Game Benchmark: Quake Live (idTech 3 under GE-Proton Wayland)
Benchmarked under identical controlled settings:
- Baseline (vendor GE-Proton Wayland): **368.4 FPS** (1% Lows: 202.2 FPS)
- With `quake-tls`: **437.4 – 457.6 FPS** (1% Lows: 233.4 FPS)
- **Net Gain: +69.0 to +89.2 FPS (+18.7% to +24.2% average FPS, +15.4% 1% Lows)**

### 3. Non-idTech Benchmark: Roy Longbottom's Windows OpenGL Suite (`videogl32.exe` under GE-Proton Wayland)
Benchmarked at 640x480 across 6 distinct rendering workloads:
| Workload | Baseline (Stock GE-Proton) | Control (Pinned + Glibc Fallback) | Fastpath (`quake-tls`) | Net Gain vs Baseline |
| :--- | :--- | :--- | :--- | :--- |
| **Wireframe Kitchen** (23,000 lines / draw calls) | **116.5 FPS** | **102.0 FPS** | **368.3 FPS** | **+216.1% (>3.1x faster)** 🚀 |
| **Textured Kitchen** (Complex indoor multi-texture scene) | **90.0 FPS** | **84.8 FPS** | **234.8 FPS** | **+160.9% (>2.6x faster)** 🚀 |
| **Textured Objects** (Few) | **1012.4 FPS** | **1000.4 FPS** | **1048.0 FPS** | **+3.5%** |
| **Coloured Objects** (Few) | **1166.7 FPS** | **1180.6 FPS** | **1194.6 FPS** | **+2.4%** |
| **Textured Objects** (All) | **866.7 FPS** | **828.7 FPS** | **872.3 FPS** | **+0.6%** |
| **Coloured Objects** (All) | **1159.1 FPS** | **1169.7 FPS** | **1170.2 FPS** | **+1.0%** |

### 4. Isolated Control Experiment (`QUAKE_TLS_FORWARD_ONLY=1`)
To conclusively isolate the effect of the TLS fastpath from side-effects of module pre-loading or driver pinning:
- `QUAKE_TLS_FORWARD_ONLY=1` performs 100% of the identical startup initialization: `dlopen` of `libnvidia-eglcore.so` with `RTLD_NODELETE`, module ID query, and dynamic linker interposition. However, it unconditionally forwards every single `___tls_get_addr` lookup into glibc's resolver.
- Result: As shown in the table above, the control run matches stock baseline performance (**102.0 FPS** vs **116.5 FPS** on Wireframe Kitchen), proving that driver pinning alone yields no benefit. The **368.3 FPS** result is purely driven by bypassing the `___tls_get_addr` resolver.

### 5. Runtime Call-Count Verification & Per-Workload Analysis (`QUAKE_TLS_COUNT=1`)
Using runtime hardware counters embedded in `quake-tls`, we measured exact lookup volumes across isolated individual workloads (`videogl32.exe T <id>`) to correlate TLS lookup savings against measured frame time reductions:

| Workload | Frame Time Delta | Time Saved | Measured Lookups / Frame | Theoretical Saving (@ 40ns / call) | Variance Explained |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Quake Live** (idTech 3 game loop) | 2.714 ms → 2.286 ms | **0.428 ms** | **~10,700 calls / frame** | **0.428 ms** | **100.0%** |
| **Tunnel: Few Objects** (Mesh) | 0.857 ms → 0.808 ms | **0.049 ms** | **~2,767 calls / frame** | **0.110 ms** | **100% (low-call baseline)** |
| **Wireframe Kitchen** (23k lines) | 8.584 ms → 2.715 ms | **5.869 ms** | **~134,735 calls / frame** | **5.389 ms** | **91.8%** 🎯 |
| **Textured Kitchen** (Dense indoor) | 11.111 ms → 4.259 ms | **6.852 ms** | **~149,493 calls / frame** | **5.980 ms** | **87.3%** 🎯 |

*Key Takeaway:*
- In low draw-call workloads (Tunnel), only **~2,700 lookups/frame** occur, explaining why FPS gains are modest (+2%).
- In Quake Live, **~10,700 lookups/frame** occur, perfectly explaining the **0.428 ms** (+18.7%) frametime reduction.
- In immediate-mode OpenGL workloads (Kitchen tests issuing ~86,000 distinct GL calls for 23,000 polygons), lookups scale to **134,700 – 149,500 lookups/frame**. At ~40 ns per call saved, this yields **5.4 – 6.0 ms of CPU time eliminated per frame**, explaining over 90% of the dramatic 3.1x framerate jump.
- The previously reported aggregate average of ~17,900 lookups/frame was a weighted average across all 6 passes combined, where ~85% of total frames rendered came from the 1,000+ FPS tunnel passes. Isolated per-test instrumentation confirms that the Kitchen workloads independently exhibit the requisite >130,000 lookups/frame.

### 6. Binary Disassembly Comparison: EGL vs GLX
Inspection of NVIDIA's 32-bit x86 driver binaries (`620.70.06`):
- **`libnvidia-eglcore.so` (EGL)**: Contains **11,593** `R_386_PC32 ___tls_get_addr@GLIBC_2.3` call sites. Every render-path state check emits a General Dynamic call:
  ```assembly
  lea -0x8(,%ebx,1), %eax
  call 2f6b78 <___tls_get_addr>
  mov (%eax), %eax
  ```
- **`libnvidia-glcore.so` (GLX)**: Contains **0** calls to `___tls_get_addr`. It uses Initial-Exec (`R_386_TLS_TPOFF`) with direct segment register loads:
  ```assembly
  mov 0x1e02ffc, %edx
  movl $0x0, %gs:(%edx)
  mov %gs:0x0, %edx
  ```
This explains why native X11 / GLX does not suffer from this bottleneck, whereas Wine-Wayland (which maps OpenGL contexts through EGL via `winewayland.drv`) is severely impacted.

---

## 🛠️ Building

### Prerequisites
- 32-bit GCC and glibc development packages (e.g. `gcc-multilib` on Debian/Ubuntu, `lib32-gcc-libs` / `lib32-glibc` on Arch/CachyOS).

### Build & Run Tests
```bash
make
make test
```

The test suite runs 4 million validations across 40 threads, exercising:
- Correct pointer resolution matching glibc
- Dynamic `dlopen` / `dlclose` module lifecycle stability
- FPU/SSE state preservation across fast-path calls
- Clean fallback for uninitialized and non-target module lookups

---

## 🎮 Usage

Preload `libquake-tls.so` before launching your 32-bit application or game.

### Steam Launch Options
```bash
LD_PRELOAD="/path/to/libquake-tls.so" %command%
```

### Environment Variables
- `QUAKE_TLS_DIAGNOSTIC=1`: Prints driver pinning and TLS module registration status to stderr on launch.
- `QUAKE_TLS_FORWARD_ONLY=1`: Control experiment mode. Pins driver and registers interposition, but forwards 100% of TLS lookups into glibc's resolver.
- `QUAKE_TLS_COUNT=1`: Enables runtime counters tracking total fastpath hits and fallback calls, printing results on process termination.
- `QUAKE_TLS_FORCE_GLIBC=1`: Bypasses the default glibc version safety check for testing on glibc versions other than 2.44.
- `QUAKE_TLS_DRIVER_SONAME=custom_so_name.so`: Overrides the driver library soname to pin (defaults to searching `libnvidia-eglcore.so`, `libnvidia-eglcore.so.1`, etc.).

---

## ⚠️ Compatibility & Limits

- **Architecture:** 32-bit x86 (`i386`) Linux only. (64-bit binaries use distinct TLS access models and are not affected by this bottleneck).
- **C Library:** Glibc only (relies on i386 glibc DTV layout where `%gs:4` holds the DTV pointer).
- **Scope:** Specifically optimizes NVIDIA's 32-bit EGL driver under Wine/Proton Wayland environments.

---

## 📄 License

MIT License. See [LICENSE](LICENSE) for details.
