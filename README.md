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

### Microbenchmark (`test-tls`)
- Standard glibc resolver wrapper: **~42.7 ns / call**
- `quake-tls` pinned-module fast path: **~2.8 ns / call**
- **Speedup: ~15x faster per TLS lookup**

### Real-World Game Benchmark (Quake Live under GE-Proton Wayland)
Benchmarked under identical controlled settings:
- Baseline (vendor GE-Proton Wayland): **368.4 FPS** (1% Lows: 202.2 FPS)
- With `quake-tls`: **437.4 FPS** (1% Lows: 233.4 FPS)
- **Net Gain: +69.0 FPS (+18.7% average FPS, +15.4% 1% Lows)**

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
