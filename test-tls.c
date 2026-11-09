#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct tls_index { size_t module, offset; };
typedef void *(__attribute__((regparm(1))) *fast_fn)(struct tls_index *);
typedef void *(*normal_fn)(struct tls_index *);
static size_t target;
static fast_fn fast_tls;
static normal_fn normal_tls;
static __thread int own_tls;
static char fixture_path[1024];

static void init_fixture_path(void)
{
    Dl_info info;
    if (dladdr((void *)&init_fixture_path, &info) && info.dli_fname) {
        const char *slash = strrchr(info.dli_fname, '/');
        if (slash) {
            snprintf(fixture_path, sizeof(fixture_path), "%.*s/libtls-fixture.so",
                     (int)(slash - info.dli_fname), info.dli_fname);
            return;
        }
    }
    snprintf(fixture_path, sizeof(fixture_path), "./libtls-fixture.so");
}

static void *worker(void *unused)
{
    struct tls_index index = { target, 0 };
    void *base = (uintptr_t)unused & 1 ? fast_tls(&index) : normal_tls(&index);
    assert(base);
    assert(base == normal_tls(&index));
    for (unsigned i = 0; i < 100000; i++) {
        index.offset = i & 63;
        assert(fast_tls(&index) == (void *)((uintptr_t)base + index.offset));
        if (!(i % 10000)) {
            void *h = dlopen(fixture_path, RTLD_LAZY | RTLD_LOCAL);
            assert(h);
            /* Check the pinned module before asking libc to update this
             * thread's DTV for the freshly loaded module. */
            assert(fast_tls(&index) == (void *)((uintptr_t)base + index.offset));
            size_t other_module = 0;
            assert(!dlinfo(h, RTLD_DI_TLS_MODID, &other_module));
            assert(other_module && other_module != target);
            struct tls_index other = { other_module, 0 };
            assert(fast_tls(&other) == normal_tls(&other));
            unsigned *(*address)(void) = dlsym(h, "tls_fixture_addr");
            assert(address);
            *address() = (unsigned)(uintptr_t)unused + 100;
            assert(*address() == (unsigned)(uintptr_t)unused + 100);
            dlclose(h);
            assert(fast_tls(&index) == normal_tls(&index));
        }
    }
    return NULL;
}
int main(void)
{
    init_fixture_path();
    size_t (*get_target)(void) = dlsym(RTLD_DEFAULT, "quake_tls_target_module");
    fast_tls = (fast_fn)dlsym(RTLD_DEFAULT, "___tls_get_addr");
    normal_tls = (normal_fn)dlsym(RTLD_DEFAULT, "__tls_get_addr");
    assert(get_target && fast_tls && normal_tls);
    target = get_target();
    assert(target);
    void *self = dlopen(NULL, RTLD_LAZY);
    size_t own_module = 0;
    own_tls = 123;
    assert(!dlinfo(self, RTLD_DI_TLS_MODID, &own_module));
    assert(own_module && own_module != target);
    struct tls_index own_index = { own_module, 0 };
    assert(fast_tls(&own_index) == normal_tls(&own_index));
    assert(own_tls == 123);
    pthread_t threads[8];
    for (unsigned round = 0; round < 5; round++) {
        for (unsigned i = 0; i < 8; i++) assert(!pthread_create(&threads[i], NULL, worker, (void *)(uintptr_t)i));
        for (unsigned i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
    }
    struct tls_index index = { target, 0 };
    void *expected = normal_tls(&index);
    struct { unsigned char bytes[512]; } __attribute__((aligned(16))) before = {0}, after = {0};
    for (unsigned mode = 0; mode < 2; mode++) {
        struct tls_index *input = mode ? &own_index : &index;
        __asm__ volatile("fxsave %1\n\tcall *%3\n\tfxsave %2"
                         : "+a"(input), "=m"(before), "=m"(after)
                         : "m"(fast_tls) : "memory", "cc");
        assert(!memcmp(&before, &after, sizeof(before)));
    }
    volatile uintptr_t sink = 0;
    for (unsigned mode = 0; mode < 2; mode++) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (unsigned i = 0; i < 10000000; i++)
            sink ^= (uintptr_t)(mode ? fast_tls(&index) : normal_tls(&index));
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ns = (b.tv_sec-a.tv_sec)*1e9 + b.tv_nsec-a.tv_nsec;
        printf("%s %.2f ns/call\n", mode ? "pinned-module fast path" : "glibc wrapper", ns/10000000);
    }
    assert(fast_tls(&index) == expected);
    printf("PASS: module=%zu, 4 million checks across 40 threads, first-use fallback, other modules, FPU/SSE preservation, dlopen/dlclose, teardown\n", target);
    return sink == UINTPTR_MAX;
}
