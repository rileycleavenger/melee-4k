/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Built by prospero-clang. Calls into the GCC object, maps a MEM1 stand-in the
 * way melee-pc does on PIE platforms, and reports to stdout plus, when
 * SPIKE_HOST is compiled in, a TCP listener on the development machine
 * (`nc -l 9030`), since a payload's stdout only reaches the console's klog. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wchar.h>

#include "spike.h"

static int g_sock = -1;
static int g_failed;

static void report(const char* fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    if (g_sock >= 0 && n > 0)
        write(g_sock, line, (size_t) n < sizeof line ? (size_t) n : sizeof line - 1);
}

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond))                                                        \
            g_failed++;                                                     \
        report("%s ", (cond) ? "PASS" : "FAIL");                            \
        report(__VA_ARGS__);                                                \
        report("\n");                                                       \
    } while (0)

static int cb_sum(struct Vec3 v)
{
    return (int) (v.x + v.y + v.z);
}

static void connect_back(void)
{
#ifdef SPIKE_HOST
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(9030)};
    inet_pton(AF_INET, SPIKE_HOST, &sa.sin_addr);
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock >= 0 && connect(g_sock, (struct sockaddr*) &sa, sizeof sa) != 0) {
        close(g_sock);
        g_sock = -1;
    }
#endif
}

int main(void)
{
    connect_back();
    report("melee-4k PS5 GCC/clang interop spike\n");

    static const unsigned char disc[] = {
        0x00, 0x01, 0x02, 0x03, 0x00, 0x05, 0xFF, 0xFE,
        0x3F, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x12, 0x34,
    };
    struct NativeHeader h;
    uint32_t sum = gcc_read_disc(disc, &h);
    CHECK(h.file_size == 0x00010203 && h.count == 5 && h.delta == -2 && h.scale == 1.5f &&
              h.ptr_slot == 0x80001234 && sum == 0x00010208,
          "big-endian disc struct via scalar_storage_order (size %#x count %u delta %d scale %g)",
          h.file_size, h.count, h.delta, (double) h.scale);

    int a, b, c;
    CHECK(gcc_count_symbols(&a, "x", &b, "y", &c, "z", NULL) == 3, "NULL-terminated variadic list");

    struct Vec3 r = gcc_vec_scale((struct Vec3){1, 2, 3}, 2.0f, cb_sum);
    CHECK(r.x == 14 && r.y == 4 && r.z == 6, "struct by value + clang callback (%g %g %g)",
          (double) r.x, (double) r.y, (double) r.z);

    double m = gcc_mixed_args(1, 2.5, 3.5f, 4, 5.25, 6.75f, 7, 8.5, 9.5f, 10);
    CHECK(m == 58.0, "10 mixed int/float/double args (%g)", m);

    CHECK(gcc_wchar_size() == sizeof(wchar_t), "wchar_t size gcc %zu clang %zu", gcc_wchar_size(),
          sizeof(wchar_t));

    char buf[16];
    CHECK(gcc_libc(buf, sizeof buf) == 3 && strcmp(buf, "gcc") == 0, "SDK libc called from gcc object");

    /* MEM1: melee-pc wants 24 MB whose low 32 bits start at 0x80000000. */
    const size_t mem1_size = 24u << 20;
    void* hint = (void*) (uintptr_t) 0x180000000ull;
    void* mem1 = mmap(hint, mem1_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    int low_ok = mem1 != MAP_FAILED && ((uintptr_t) mem1 & 0xFFFFFFFFu) == 0x80000000u;
    CHECK(low_ok, "MEM1 mmap with low half 0x80000000 (got %p)", mem1);
    if (low_ok) {
        unsigned char* p = gcc_dp(0x80000010u, (uintptr_t) mem1);
        p[0] = 0x5A;
        CHECK(p == (unsigned char*) mem1 + 0x10 && ((unsigned char*) mem1)[0x10] == 0x5A,
              "DP() high-half restore into MEM1");
    }

    report("%s: %d failure(s)\n", g_failed ? "FAILED" : "ALL PASSED", g_failed);
    if (g_sock >= 0)
        close(g_sock);
    return g_failed;
}
