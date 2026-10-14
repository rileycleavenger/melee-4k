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

/* ---- Character select's CUSTOM switch --------------------------------------
 * A tile shaped and framed like the HD character icons (320x280 for the
 * disc's 64x56, measured from Melee HD's: a light grey rounded frame with a
 * soft inner shadow, a dark red name bar across the bottom quarter with the
 * name in light capitals), its window left clear for the screen's striped
 * backdrop, with ON (green) or OFF (grey) in it. RGBA8. */
#define TILE_W 320
#define TILE_H 280

static bool in_rounded(float x, float y, float x0, float y0, float x1, float y1, float r) {
    if (x < x0 || x > x1 || y < y0 || y > y1)
        return false;
    const float cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x;
    const float cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

static void over(float* px, float r, float g, float b, float a) {
    px[0] = px[0] * (1.0f - a) + r * a;
    px[1] = px[1] * (1.0f - a) + g * a;
    px[2] = px[2] * (1.0f - a) + b * a;
    px[3] = px[3] + a * (1.0f - px[3]);
}

/* Draw text into [x0,y0,x1,y1] of the tile, fitted, centred: fill colour
 * with an outline of `outline` label pixels (0: none). */
static void tile_text(float* img, const char* text, float x0, float y0, float x1, float y1,
                      const float* fill, const float* edge, float outline) {
    u8* cov = calloc(LABEL_W * LABEL_H, 1);
    float* dist = malloc(sizeof(float) * LABEL_W * LABEL_H);
    rasterize(text, cov);
    distance(cov, dist);
    int bx0 = LABEL_W, by0 = LABEL_H, bx1 = 0, by1 = 0;
    for (int y = 0; y < LABEL_H; y++)
        for (int x = 0; x < LABEL_W; x++)
            if (dist[y * LABEL_W + x] <= outline + 1.0f) {
                bx0 = x < bx0 ? x : bx0, bx1 = x > bx1 ? x : bx1;
                by0 = y < by0 ? y : by0, by1 = y > by1 ? y : by1;
            }
    if (bx1 > bx0 && by1 > by0) {
        const float sw = (float)(bx1 - bx0 + 1), sh = (float)(by1 - by0 + 1);
        const float k = fminf((x1 - x0) / sw, (y1 - y0) / sh);
        const float ox = 0.5f * (x0 + x1 - sw * k), oy = 0.5f * (y0 + y1 - sh * k);
        for (int y = (int)oy; y < (int)(oy + sh * k) + 1 && y < TILE_H; y++) {
            for (int x = (int)ox; x < (int)(ox + sw * k) + 1 && x < TILE_W; x++) {
                if (x < 0 || y < 0)
                    continue;
                /* the label texel under this tile texel (box filter of
                 * its 1/k x 1/k footprint, sampled 3x3) */
                float f = 0.0f, a = 0.0f;
                for (int sy = 0; sy < 3; sy++) {
                    for (int sx = 0; sx < 3; sx++) {
                        const int lx = bx0 + (int)(((x - ox) + (sx + 0.5f) / 3.0f) / k);
                        const int ly = by0 + (int)(((y - oy) + (sy + 0.5f) / 3.0f) / k);
                        if (lx < 0 || ly < 0 || lx >= LABEL_W || ly >= LABEL_H)
                            continue;
                        const float d = dist[ly * LABEL_W + lx];
                        float fi = ramp(EMBOLDEN, d);
                        const float ink = cov[ly * LABEL_W + lx] / 255.0f;
                        fi = ink > fi ? ink : fi;
                        float al = outline > 0.0f ? ramp(outline + EMBOLDEN, d) : fi;
                        f += fi, a += al > fi ? al : fi;
                    }
                }
                f /= 9.0f, a /= 9.0f;
                float* px = &img[(y * TILE_W + x) * 4];
                if (a > f)
                    over(px, edge[0], edge[1], edge[2], a - f > 0.0f ? (a - f) / (1.0f - f + 1e-6f) : 0.0f);
                over(px, fill[0], fill[1], fill[2], f);
            }
        }
    }
    free(dist);
    free(cov);
}

static void render_tile(bool on, u8* texels) {
    float* img = calloc(TILE_W * TILE_H * 4, sizeof(float));
    const float fw = 10.0f, rad = 22.0f, bar0 = 0.735f * TILE_H;
    for (int y = 0; y < TILE_H; y++) {
        for (int x = 0; x < TILE_W; x++) {
            float* px = &img[(y * TILE_W + x) * 4];
            const float cx = x + 0.5f, cy = y + 0.5f;
            const bool outer = in_rounded(cx, cy, 0, 0, TILE_W, TILE_H, rad);
            const bool inner = in_rounded(cx, cy, fw, fw, TILE_W - fw, TILE_H - fw, rad - fw);
            if (!outer)
                continue;
            if (!inner) {
                const bool rim = !in_rounded(cx, cy, 2, 2, TILE_W - 2, TILE_H - 2, rad - 2);
                const float v = (rim ? 224 : 197) / 255.0f;
                over(px, v, v, v, 1.0f);
                continue;
            }
            /* the inner shadow, fading over 6 px from the frame */
            float edge = 1e9f;
            for (int k = 1; k <= 6; k++) {
                if (!in_rounded(cx, cy, fw + k, fw + k, TILE_W - fw - k, TILE_H - fw - k, rad - fw)) {
                    edge = (float)k;
                    break;
                }
            }
            if (cy >= bar0) {
                over(px, 24 / 255.0f, 4 / 255.0f, 4 / 255.0f, 1.0f);
            } else {
                /* a faint dark wash, so the words read on the stripes */
                over(px, 0.02f, 0.03f, 0.06f, 0.45f);
            }
            if (edge < 1e8f)
                over(px, 36 / 255.0f, 36 / 255.0f, 36 / 255.0f, 0.75f * (1.0f - (edge - 1) / 6.0f));
        }
    }
    const float grey[3] = {204 / 255.0f, 204 / 255.0f, 204 / 255.0f};
    const float dark[3] = {0.02f, 0.02f, 0.03f};
    const float green[3] = {120 / 255.0f, 236 / 255.0f, 120 / 255.0f};
    const float off[3] = {150 / 255.0f, 150 / 255.0f, 156 / 255.0f};
    tile_text(img, "CUSTOM", 26, bar0 + 12, TILE_W - 26, TILE_H - fw - 10, grey, dark, 0.0f);
    tile_text(img, on ? "ON" : "OFF", 40, 40, TILE_W - 40, bar0 - 26, on ? green : off, dark, OUTLINE);
    /* GX RGBA8: 4x4 tiles of AR pairs, then GB pairs */
    for (int ty = 0; ty < TILE_H; ty += 4) {
        for (int tx = 0; tx < TILE_W; tx += 4) {
            u8 ar[32], gb[32];
            int n = 0;
            for (int y = ty; y < ty + 4; y++) {
                for (int x = tx; x < tx + 4; x++, n++) {
                    const float* px = &img[(y * TILE_W + x) * 4];
                    const float a = px[3];
                    /* straight alpha: the colour composited over nothing */
                    const float k = a > 0.0f ? 1.0f / a : 0.0f;
                    ar[n * 2] = (u8)lroundf(fminf(a, 1.0f) * 255.0f);
                    ar[n * 2 + 1] = (u8)lroundf(fminf(px[0] * k, 1.0f) * 255.0f);
                    gb[n * 2] = (u8)lroundf(fminf(px[1] * k, 1.0f) * 255.0f);
                    gb[n * 2 + 1] = (u8)lroundf(fminf(px[2] * k, 1.0f) * 255.0f);
                }
            }
            memcpy(texels, ar, 32);
            memcpy(texels + 32, gb, 32);
            texels += 64;
        }
    }
    free(img);
}

HSD_ImageDesc* pc_menu_toggle_tile(bool on) {
    static HSD_ImageDesc desc[2];
    static u8* texels[2];
    const int i = on ? 1 : 0;
    if (!load_font()) {
        return NULL;
    }
    if (texels[i] == NULL) {
        texels[i] = malloc((size_t)TILE_W * TILE_H * 4);
        render_tile(on, texels[i]);
        DP_SET(desc[i].image_ptr, texels[i]);
        desc[i].width = TILE_W;
        desc[i].height = TILE_H;
        desc[i].format = GX_TF_RGBA8;
    }
    return &desc[i];
}
