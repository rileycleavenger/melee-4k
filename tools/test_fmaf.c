/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pc_fmaf (src/pc/libm/pc_fmaf.c) rounds once, where computing x*y+z in
 * double and narrowing rounds twice. 1.000244140625^2 is exactly
 * 1 + 2^-11 + 2^-24, halfway between two floats; adding 2^-80 lifts the
 * true sum just past halfway, so it rounds up, but the double sum loses
 * the 2^-80 and the narrowing then rounds the tie to even, down. That is
 * the kind of input mingw-w64's msvcrt fmaf got one ULP wrong (v1.1.0-rc2,
 * a floor normal through ppc_rsqrte). Also checks random inputs against
 * the hardware fused multiply-add where the compiler gives us one. */
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

float pc_fmaf(float x, float y, float z);

static uint32_t bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float from_bits(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

int main(void) {
    const float x = 1.000244140625f; /* 1 + 2^-12 */
    const float z = 0x1p-80f;
    const float want = 1.0f + 0x1p-11f + 0x1p-23f;
    const float twice = (float)((double)x * x + z);
    assert(bits(twice) != bits(want)); /* the case really rounds differently */
    assert(bits(pc_fmaf(x, x, z)) == bits(want));
    assert(bits(pc_fmaf(-x, x, -z)) == bits(-want));
    assert(bits(pc_fmaf(2.0f, 3.0f, 1.0f)) == bits(7.0f));
#if defined(__FP_FAST_FMAF) || defined(__aarch64__)
    uint32_t s = 12345;
    for (int i = 0; i < 1000000; i++) {
        float v[3];
        for (int k = 0; k < 3; k++) {
            s = s * 1664525u + 1013904223u;
            v[k] = from_bits((s & 0x807FFFFFu) | ((100u + (s >> 24) % 56u) << 23));
        }
        assert(bits(pc_fmaf(v[0], v[1], v[2])) == bits(__builtin_fmaf(v[0], v[1], v[2])));
    }
    puts("test_fmaf: ok (and 1000000 random inputs match the hardware fma)");
#else
    puts("test_fmaf: ok");
#endif
    return 0;
}
