/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Built by x86_64-elf-gcc, like the decomp's C will be: the parts of the game's
 * code model that prospero-clang cannot compile and that must interoperate with
 * clang-built code across the SysV ABI. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include "spike.h"

/* On-disc data is big-endian; melee-pc reads it through scalar_storage_order
 * (src/pc/disc.h DISC_STRUCT). */
struct __attribute__((scalar_storage_order("big-endian"))) DiscHeader {
    uint32_t file_size;
    uint16_t count;
    int16_t delta;
    float scale;
    uint32_t ptr_slot;
};

uint32_t gcc_read_disc(const void* bytes, struct NativeHeader* out)
{
    const struct DiscHeader* h = (const struct DiscHeader*) bytes;
    out->file_size = h->file_size;
    out->count = h->count;
    out->delta = h->delta;
    out->scale = h->scale;
    out->ptr_slot = h->ptr_slot;
    return h->file_size + h->count;
}

/* lbArchive-style NULL-terminated pointer list. */
int gcc_count_symbols(void* first, ...)
{
    va_list ap;
    int n = 0;
    va_start(ap, first);
    for (void* p = first; p != NULL; p = va_arg(ap, void*)) {
        const char* name = va_arg(ap, const char*);
        n += name != NULL;
    }
    va_end(ap);
    return n;
}

/* Struct passing/return mixing floats and ints (Vec3, HSD callbacks). */
struct Vec3 gcc_vec_scale(struct Vec3 v, float s, int (*cb)(struct Vec3))
{
    struct Vec3 r = {v.x * s, v.y * s, v.z * s};
    r.x += (float) cb(r);
    return r;
}

double gcc_mixed_args(int a, double b, float c, long d, double e, float f, int g, double h, float i, int j)
{
    return a + b + c + d + e + f + g + h + i + j;
}

size_t gcc_wchar_size(void)
{
    return sizeof(wchar_t);
}

/* MEM1 high-half restore (melee-pc DP()): a 32-bit slot holds the low half,
 * the base supplies the high half. */
void* gcc_dp(uint32_t slot, uintptr_t mem1_base)
{
    return (void*) ((mem1_base & ~(uintptr_t) 0xFFFFFFFF) | slot);
}

int gcc_libc(char* buf, size_t n)
{
    memset(buf, 0, n);
    memcpy(buf, "gcc", 4);
    return (int) strlen(buf);
}
