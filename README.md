# wine-nvidia-egl-tls: NVIDIA EGL TLS Fastpath Helper

A targeted `LD_PRELOAD` helper library for 32-bit x86 glibc Linux that eliminates CPU bottlenecks caused by frequent Thread-Local Storage (TLS) lookups in NVIDIA's proprietary 32-bit EGL driver (`libnvidia-eglcore.so`).

Originally developed while optimizing 32-bit Quake Live under Wine / Proton Wayland.

---

## ⚡ Background & The Problem

When running 32-bit OpenGL games under Wine/Proton Wayland with NVIDIA's EGL driver, CPU sampling revealed that roughly **19% of main thread execution time** was spent inside the host dynamic loader (`ld-linux.so`), heavily concentrated around `XSAVEC` / `XRSTOR` register save/restores during `___tls_get_addr` calls.

NVIDIA's 32-bit `libnvidia-eglcore.so` queries TLS dynamically and frequently on hot render paths. Under standard glibc on x86, every call to the dynamic resolver (`___tls_get_addr`) incurs dynamic link-map checks, slow-path dispatching, and full vector register state preservation, averaging **~43 ns per call**.

Increasing the static TLS reserve (`glibc.rtld.optional_static_tls`) has minimal effect on this path because the module is loaded dynamically at runtime.

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
- With `quake-tls`: **437.4 FPS** (1% Lows: 233.4 FPS)
- **Net Gain: +69.0 FPS (+18.7% average FPS, +15.4% 1% Lows)**

### 3. Non-idTech Benchmark: Roy Longbottom's Windows OpenGL Suite (`videogl32.exe` under GE-Proton Wayland)
Benchmarked at 640x480 across 6 distinct rendering workloads:
| Workload | Baseline (Stock GE-Proton) | With `quake-tls` | Performance Change |
| :--- | :--- | :--- | :--- |
| **Wireframe Kitchen** (23,000 straight lines / draw calls) | **116.5 FPS** | **368.3 FPS** | **+216.1% (>3.1x faster)** 🚀 |
| **Textured Kitchen** (Complex indoor multi-texture scene) | **90.0 FPS** | **234.8 FPS** | **+160.9% (>2.6x faster)** 🚀 |
| **Textured Objects** (Few) | **1012.4 FPS** | **1048.0 FPS** | **+3.5%** |
| **Coloured Objects** (Few) | **1166.7 FPS** | **1194.6 FPS** | **+2.4%** |
| **Textured Objects** (All) | **866.7 FPS** | **872.3 FPS** | **+0.6%** |
| **Coloured Objects** (All) | **1159.1 FPS** | **1170.2 FPS** | **+1.0%** |

*Note: In workloads with high per-frame draw-call and state-query counts (like the Kitchen tests issuing thousands of immediate GL calls), bypassing the dynamic linker's `___tls_get_addr` resolver yields up to a 3.1x framerate improvement.*

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
