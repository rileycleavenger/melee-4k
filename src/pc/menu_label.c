/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Menu labels for the port's own main-menu entries (Options > Melee 4K, VS
 * Mode > Online), drawn the way Melee draws its own: an IA8 texture of white
 * letters with a dark outline, shown through the entry's label TObj so the
 * menu material tints it (cream, dark when hovered) exactly as it tints the
 * stock labels in MnExtAll.
 *
 * The geometry is measured from the stock 176x30 labels (and HD Remix's
 * redraws of them): baseline at 0.844 of the height, ascenders 0.711 tall,
 * an outline 0.066 thick, long names squeezed horizontally to fit. The font
 * is Zen Kaku Gothic New Black (OFL, resources/menu-label.ttf), the closest
 * free match to Melee's Rodin, slightly emboldened. Labels are rendered at
 * 1024x176, about 6x the stock size, so they stay sharp at 4K. */
#include "pc/menu_label.h"

#include "pc/disc.h"
#include <SDL3/SDL.h>
#include <dolphin/gx/GXEnum.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "pc/stb_truetype.h"

#define LABEL_W 1024
#define LABEL_H 176
#define BASELINE (0.844f * LABEL_H)
#define ASCENDER (0.711f * LABEL_H)
#define OUTLINE (0.066f * LABEL_H)
#define EMBOLDEN (0.010f * LABEL_H)
#define MAX_TEXT_W (0.97f * LABEL_W)
#define SPACE_SCALE 0.6f /* Melee's word gaps are tight */
#define CACHE 24

typedef struct Label {
    char text[64];
    HSD_ImageDesc desc;
    u8* texels;
    unsigned used;
} Label;

static stbtt_fontinfo s_font;
static unsigned char* s_font_data;
static int s_font_state; /* 0 untried, 1 ready, -1 unavailable */
static Label s_cache[CACHE];
static unsigned s_clock;

static bool load_font(void) {
    if (s_font_state != 0) {
        return s_font_state > 0;
    }
    s_font_state = -1;
    const char* base = SDL_GetBasePath();
    char path[1024];
    SDL_snprintf(path, sizeof path, "%sresources/menu-label.ttf", base ? base : "");
    size_t size = 0;
    s_font_data = SDL_LoadFile(path, &size);
    if (s_font_data == NULL) {
        s_font_data = SDL_LoadFile("resources/menu-label.ttf", &size);
    }
    if (s_font_data == NULL ||
        !stbtt_InitFont(&s_font, s_font_data, stbtt_GetFontOffsetForIndex(s_font_data, 0)))
    {
        SDL_Log("menu_label: cannot load resources/menu-label.ttf");
        return false;
    }
    s_font_state = 1;
    return true;
}

/* Pen advance of `text` at the given horizontal scale, in pixels. */
static float text_width(const char* text, float sx) {
    float w = 0.0f;
    for (const char* p = text; *p; p++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&s_font, (unsigned char)*p, &adv, &lsb);
        w += adv * sx * (*p == ' ' ? SPACE_SCALE : 1.0f);
        if (p[1]) {
            w += stbtt_GetCodepointKernAdvance(&s_font, (unsigned char)p[0], (unsigned char)p[1]) *
                 sx;
        }
    }
    return w;
}

/* Coverage of `text`, horizontally centred, into cov[LABEL_W * LABEL_H]. */
static void rasterize(const char* text, u8* cov) {
    int x0, y0, x1, y1;
    stbtt_GetCodepointBox(&s_font, 'l', &x0, &y0, &x1, &y1);
    const float sy = ASCENDER / (float)(y1 > 0 ? y1 : 1);
    float sx = sy;
    const float width = text_width(text, sx);
    if (width > MAX_TEXT_W) {
        sx *= MAX_TEXT_W / width; /* squeeze, as Melee does with long names */
    }
    float pen = 0.5f * (LABEL_W - text_width(text, sx));
    for (const char* p = text; *p; p++) {
        const int cp = (unsigned char)*p;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
        if (cp != ' ') {
            const float shift = pen - floorf(pen);
            int bx0, by0, bx1, by1;
            stbtt_GetCodepointBitmapBoxSubpixel(
                &s_font, cp, sx, sy, shift, 0.0f, &bx0, &by0, &bx1, &by1);
            const int gw = bx1 - bx0, gh = by1 - by0;
            if (gw > 0 && gh > 0) {
                u8* glyph = malloc((size_t)gw * gh);
                stbtt_MakeCodepointBitmapSubpixel(
                    &s_font, glyph, gw, gh, gw, sx, sy, shift, 0.0f, cp);
                const int ox = (int)floorf(pen) + bx0;
                const int oy = (int)lroundf(BASELINE) + by0;
                for (int y = 0; y < gh; y++) {
                    for (int x = 0; x < gw; x++) {
                        const int px = ox + x, py = oy + y;
                        if (px >= 0 && px < LABEL_W && py >= 0 && py < LABEL_H) {
                            u8* d = &cov[py * LABEL_W + px];
                            const u8 v = glyph[y * gw + x];
                            if (v > *d) {
                                *d = v;
                            }
                        }
                    }
                }
                free(glyph);
            }
        }
        pen += adv * sx * (cp == ' ' ? SPACE_SCALE : 1.0f);
        if (p[1]) {
            pen += stbtt_GetCodepointKernAdvance(&s_font, cp, (unsigned char)p[1]) * sx;
        }
    }
}

/* Chamfer (3-4) distance, in pixels, from every texel to the glyph ink. */
static void distance(const u8* cov, float* dist) {
    const float inf = 1e9f, a = 1.0f, b = 1.41421356f;
    for (int i = 0; i < LABEL_W * LABEL_H; i++) {
        dist[i] = cov[i] >= 128 ? 0.0f : inf;
    }
    for (int y = 0; y < LABEL_H; y++) {
        for (int x = 0; x < LABEL_W; x++) {
            float* d = &dist[y * LABEL_W + x];
            if (x > 0 && d[-1] + a < *d)
                *d = d[-1] + a;
            if (y > 0) {
                const float* up = d - LABEL_W;
                if (up[0] + a < *d)
                    *d = up[0] + a;
                if (x > 0 && up[-1] + b < *d)
                    *d = up[-1] + b;
                if (x + 1 < LABEL_W && up[1] + b < *d)
                    *d = up[1] + b;
            }
        }
    }
    for (int y = LABEL_H - 1; y >= 0; y--) {
        for (int x = LABEL_W - 1; x >= 0; x--) {
            float* d = &dist[y * LABEL_W + x];
            if (x + 1 < LABEL_W && d[1] + a < *d)
                *d = d[1] + a;
            if (y + 1 < LABEL_H) {
                const float* dn = d + LABEL_W;
                if (dn[0] + a < *d)
                    *d = dn[0] + a;
                if (x + 1 < LABEL_W && dn[1] + b < *d)
                    *d = dn[1] + b;
                if (x > 0 && dn[-1] + b < *d)
                    *d = dn[-1] + b;
            }
        }
    }
}

static float ramp(float edge, float d) {
    const float v = edge + 0.5f - d;
    return v <= 0.0f ? 0.0f : v >= 1.0f ? 1.0f : v;
}

/* GX IA8, 4x4 tiles, each texel alpha then intensity (TextureDecoderIA8). */
static void render(const char* text, u8* texels) {
    u8* cov = calloc(LABEL_W * LABEL_H, 1);
    float* dist = malloc(sizeof(float) * LABEL_W * LABEL_H);
    rasterize(text, cov);
    distance(cov, dist);
    for (int ty = 0; ty < LABEL_H; ty += 4) {
        for (int tx = 0; tx < LABEL_W; tx += 4) {
            for (int y = ty; y < ty + 4; y++) {
                for (int x = tx; x < tx + 4; x++) {
                    const int i = y * LABEL_W + x;
                    float fill = ramp(EMBOLDEN, dist[i]);
                    const float ink = cov[i] / 255.0f;
                    if (ink > fill) {
                        fill = ink;
                    }
                    float alpha = ramp(OUTLINE + EMBOLDEN, dist[i]);
                    if (fill > alpha) {
                        alpha = fill;
                    }
                    *texels++ = (u8)lroundf(alpha * 255.0f);
                    *texels++ = (u8)lroundf(fill * 255.0f);
                }
            }
        }
    }
    free(dist);
    free(cov);
}

HSD_ImageDesc* pc_menu_label(const char* text) {
    if (text == NULL || text[0] == '\0' || !load_font()) {
        return NULL;
    }
    Label* slot = NULL;
    for (int i = 0; i < CACHE; i++) {
        if (s_cache[i].texels != NULL &&
            strncmp(s_cache[i].text, text, sizeof s_cache[i].text) == 0)
        {
            s_cache[i].used = ++s_clock;
            return &s_cache[i].desc;
        }
        if (slot == NULL || s_cache[i].used < slot->used) {
            slot = &s_cache[i]; /* least recently used */
        }
    }
    if (slot->texels == NULL) {
        slot->texels = malloc((size_t)LABEL_W * LABEL_H * 2);
    }
    /* Every image shown by a frame was used in it, so the evicted slot is
     * at least a frame stale; menus show at most 10 labels at once. */
    SDL_strlcpy(slot->text, text, sizeof slot->text);
    render(text, slot->texels);
    memset(&slot->desc, 0, sizeof slot->desc);
    DP_SET(slot->desc.image_ptr, slot->texels);
    slot->desc.width = LABEL_W;
    slot->desc.height = LABEL_H;
    slot->desc.format = GX_TF_IA8;
    slot->desc.mipmap = 0;
    slot->desc.minLOD = 0.0f;
    slot->desc.maxLOD = 0.0f;
    slot->used = ++s_clock;
    return &slot->desc;
}
