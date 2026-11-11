#define _GNU_SOURCE
#include <dlfcn.h>
#include <gnu/libc-version.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(__i386__) || !defined(__GLIBC__)
#error This experiment supports only 32-bit x86 glibc.
#endif
_Static_assert(sizeof(void *) == 4, "Requires the i386 DTV ABI");

/* Published once at startup, after pinning the module. Zero keeps every call
 * on the libc fallback. The assembly symbol is private to this DSO. */
__attribute__((visibility("hidden"))) size_t quake_tls_module;
__attribute__((visibility("hidden"))) uint32_t quake_tls_fastpath_count = 0;
__attribute__((visibility("hidden"))) uint32_t quake_tls_fallback_count = 0;
__attribute__((visibility("hidden"))) int quake_tls_enable_count = 0;
static void *pinned_nvidia;

__attribute__((destructor)) static void fini_quake_tls(void)
{
    if (quake_tls_enable_count) {
        fprintf(stderr, "quake-tls pid=%ld tls_lookups: fastpath=%u, fallback=%u, total=%u\n",
                (long)getpid(), quake_tls_fastpath_count, quake_tls_fallback_count,
                quake_tls_fastpath_count + quake_tls_fallback_count);
    }
}

__attribute__((constructor)) static void init_quake_tls(void)
{
    size_t module = 0;
    const char *driver_override = getenv("QUAKE_TLS_DRIVER_SONAME");
    const char *force_glibc = getenv("QUAKE_TLS_FORCE_GLIBC");
    const char *candidates[] = {
        "libnvidia-eglcore.so",
        "libnvidia-eglcore.so.1",
        "libnvidia-eglcore.so.615.78.08",
        NULL
    };

    if (getenv("QUAKE_TLS_COUNT"))
        quake_tls_enable_count = 1;

    /* Fail closed on libc versions not covered by verified tests,
     * unless explicitly permitted via QUAKE_TLS_FORCE_GLIBC=1. */
    if (!force_glibc && strcmp(gnu_get_libc_version(), "2.44") != 0) {
        if (getenv("QUAKE_TLS_DIAGNOSTIC"))
            fprintf(stderr, "quake-tls: inactive (unverified glibc %s; set QUAKE_TLS_FORCE_GLIBC=1 to enable)\n",
                    gnu_get_libc_version());
        return;
    }

    if (driver_override && *driver_override) {
        pinned_nvidia = dlopen(driver_override, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    }
    for (int i = 0; !pinned_nvidia && candidates[i] != NULL; i++) {
        pinned_nvidia = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
    }

    if (getenv("QUAKE_TLS_FORWARD_ONLY")) {
        if (getenv("QUAKE_TLS_DIAGNOSTIC"))
            fprintf(stderr, "quake-tls pid=%ld: control mode active (forward-only to glibc; driver pinned)\n",
                    (long)getpid());
        quake_tls_module = 0;
        return;
    }

    if (pinned_nvidia && dlinfo(pinned_nvidia, RTLD_DI_TLS_MODID, &module) == 0)
        quake_tls_module = module;

    if (getenv("QUAKE_TLS_DIAGNOSTIC"))
        fprintf(stderr, "quake-tls pid=%ld glibc=%s pinned_nvidia_module=%zu (status=%s)\n",
                (long)getpid(), gnu_get_libc_version(), quake_tls_module,
                quake_tls_module ? "active" : "fallback");
}

__attribute__((visibility("default"))) size_t quake_tls_target_module(void)
{
    return quake_tls_module;
}
