/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Where the mouse pointer is in a menu's world: the point on the plane
 * z = plane_z under the pointer, seen through `cobj`. Character and stage
 * select move their hand there (pc_mouse_menu in keyboard.c supplies the
 * pointer). Game C, since it reads the HSD camera. */
#include "pc/mouse_menu.h"

#include "pc/net.h"
#include "pc/pc.h"
#include "pc/widescreen.h"
#include <dolphin/gx/GXAurora.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <sysdolphin/baselib/cobj.h>

#define ORIGINAL_ASPECT (73.0f / 60.0f)

/* The horizontal widening setupNormalCamera applies (widescreen.c), from the
 * framebuffer's shape: this runs during a think, outside any render pass. */
static float widen(HSD_CObj* cobj) {
    u32 w = 0, h = 0;
    if (HSD_CObjGetFlags(cobj) & PC_COBJ_FILL_FRAME) {
        return 1.0f;
    }
    AuroraGetRenderSize(&w, &h);
    if (w == 0 || h == 0) {
        return 1.0f;
    }
    const float s = ((float)w / (float)h) / ORIGINAL_ASPECT;
    return s > 1.0f ? s : 1.0f;
}

/* 4x4 inverse (general, cofactor expansion); false if singular. */
static bool invert4(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-12f) {
        return false;
    }
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) {
        out[i] = inv[i] * det;
    }
    return true;
}

/* NDC point (x, y, z) through the inverse of clip = P * V, in world. */
static Vec3 unproject(const float inv[16], float x, float y, float z) {
    const float wx = inv[0] * x + inv[1] * y + inv[2] * z + inv[3];
    const float wy = inv[4] * x + inv[5] * y + inv[6] * z + inv[7];
    const float wz = inv[8] * x + inv[9] * y + inv[10] * z + inv[11];
    const float ww = inv[12] * x + inv[13] * y + inv[14] * z + inv[15];
    Vec3 out = {wx / ww, wy / ww, wz / ww};
    return out;
}

bool pc_mouse_menu_world(HSD_CObj* cobj, float plane_z, float* x, float* y) {
    float u, v;
    if (cobj == NULL || !pc_mouse_menu(&u, &v)) {
        return false;
    }
    /* The pointer in the camera's viewport (logical 640x480), as NDC. */
    const float vx0 = cobj->viewport.xmin / 640.0f, vx1 = cobj->viewport.xmax / 640.0f;
    const float vy0 = cobj->viewport.ymin / 480.0f, vy1 = cobj->viewport.ymax / 480.0f;
    if (vx1 <= vx0 || vy1 <= vy0) {
        return false;
    }
    const float ndc_x = 2.0f * (u - vx0) / (vx1 - vx0) - 1.0f;
    const float ndc_y = 1.0f - 2.0f * (v - vy0) / (vy1 - vy0);

    /* The exact matrices the renderer submits (cobj.c setupNormalCamera),
     * including the widescreen division of the horizontal row. */
    Mtx44 p;
    const int type = makeProjectionMtx(cobj, p);
    const float s = widen(cobj);
    p[0][0] /= s;
    if (type == GX_ORTHOGRAPHIC) {
        p[0][3] /= s;
    } else {
        p[0][2] /= s;
    }
    MtxPtr view = HSD_CObjGetViewingMtxPtr(cobj);
    if (view == NULL) {
        return false;
    }
    float clip[16];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            clip[r * 4 + c] = p[r][0] * view[0][c] + p[r][1] * view[1][c] + p[r][2] * view[2][c] +
                              (c == 3 ? p[r][3] : 0.0f);
        }
    }
    float inv[16];
    if (!invert4(clip, inv)) {
        return false;
    }
    /* GX clip z runs -1..0 (near..far); any two depths give the ray. */
    const Vec3 a = unproject(inv, ndc_x, ndc_y, -0.9f);
    const Vec3 b = unproject(inv, ndc_x, ndc_y, -0.1f);
    const float dz = b.z - a.z;
    if (fabsf(dz) < 1e-6f) {
        return false;
    }
    const float k = (plane_z - a.z) / dz;
    *x = a.x + k * (b.x - a.x);
    *y = a.y + k * (b.y - a.y);
    return true;
}

bool pc_mouse_menu_port(HSD_CObj* cobj, float plane_z, int port, float* x, float* y) {
    if (!pc_net_active()) {
        return port == 0 && pc_mouse_menu_world(cobj, plane_z, x, y);
    }
    if (port == pc_net_local_player()) {
        float lx = 0.0f, ly = 0.0f;
        const bool live = pc_mouse_menu_world(cobj, plane_z, &lx, &ly);
        pc_net_pointer_sample(live, lx, ly);
    }
    return pc_net_pointer(port, x, y);
}

void pc_mouse_menu_port_stick(int port) {
    if (port == (pc_net_active() ? pc_net_local_player() : 0)) {
        pc_mouse_menu_stick_moved();
    }
}
