/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>

struct NativeHeader {
    uint32_t file_size;
    uint16_t count;
    int16_t delta;
    float scale;
    uint32_t ptr_slot;
};

struct Vec3 {
    float x, y, z;
};

uint32_t gcc_read_disc(const void* bytes, struct NativeHeader* out);
int gcc_count_symbols(void* first, ...);
struct Vec3 gcc_vec_scale(struct Vec3 v, float s, int (*cb)(struct Vec3));
double gcc_mixed_args(int a, double b, float c, long d, double e, float f, int g, double h, float i, int j);
size_t gcc_wchar_size(void);
void* gcc_dp(uint32_t slot, uintptr_t mem1_base);
int gcc_libc(char* buf, size_t n);
