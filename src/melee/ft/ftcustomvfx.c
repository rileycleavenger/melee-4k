/* A custom fighter's own visual effects (ftcustomvfx.h), from its pack's
 * vfx.cfg: one effect per line,
 *
 *     <motions> <kind> <from> <to> key=value ...
 *
 * <motions>: the base's motion names the effect plays in ("SpecialN",
 * "AttackS4,AttackAirF"; "SpecialS*" every motion starting so), or * for
 * every one (a blade always lit); <from>
 * <to>: the animation frames it shows on (* for all). Kinds:
 *
 *   glow    a soft ball of light at a point            r0 r1 pulse
 *   beam    a beam from a point along dir              len w
 *   blade   a glowing bar between two points           off2 at2 w
 *   trail   the ribbon a blade sweeps (its last life frames) off2 at2 life
 *   stream  particles poured from a point along dir    n speed spread life w grow shape
 *   burst   particles thrown out all ways at <from>     n speed life w grow shape
 *   ring    a widening ring (shock wave)               r0 r1 w ground
 *   shot    the look of its projectiles: item=ray (the Ray Gun's shot the
 *           specials fire) or item=star (the Star Rod's), drawn instead of
 *           the item's own model: shape=glow (an orb), spear (a bolt along
 *           its flight, len long), disc (a spinning ring); w, color, core,
 *           n (trail: that many fading copies behind it); motions and
 *           frames are ignored (write * * *)
 *
 * Points: at=<joint> off=x,y,z (in the joint's own axes, game units);
 * at=0 is the fighter's feet (off x forward, y up); axes=fighter takes
 * every offset as ahead, up, to the side from the joint instead. dir=<degrees> in the
 * fighter's plane: 0 ahead, 90 up, -90 down. color=RRGGBB, core=RRGGBB
 * (the hot centre), alpha=0..1. Shapes: glow, flake (a snow crystal),
 * spark (a streak along its path). ground=1: a ring flat on the floor
 * under the fighter; otherwise it faces ahead. vis=1: shown while the
 * fighter is invisible too.
 *
 * Everything here is drawn from the fighter as it is (its animation, frame
 * and joints) and kept nowhere the simulation reads: the trails' history
 * and the parsed files are presentation only. Particles are spread by a
 * hash of their index, not the game's random numbers. */
#include "ftcustomvfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fighter.h"
#include "ftcustom.h"
#include "inlines.h"
#include "types.h"
#include "pc/fighter_pack.h"
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <melee/it/inlines.h>
#include <melee/it/types.h>
#include <melee/ft/ftlib.h>
#include <stdint.h>
#include <dolphin/mtx.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>

enum { VFX_GLOW, VFX_BEAM, VFX_BLADE, VFX_TRAIL, VFX_STREAM, VFX_BURST, VFX_RING, VFX_SHOT };
enum { SHAPE_GLOW, SHAPE_FLAKE, SHAPE_SPARK, SHAPE_SPEAR, SHAPE_DISC };
enum { SHOT_RAY = 1, SHOT_STAR = 2 };

#define VFX_MAX 96
#define VFX_MOTIONS 8
#define TRAIL_LEN 16

typedef struct Vfx {
    char motion[VFX_MOTIONS][28];
    int n_motions; /* 0: every motion */
    int kind, from, to; /* to < 0: every frame */
    int at, at2;
    Vec3 off, off2;
    float dir, len, w, r0, r1, alpha, life, speed, spread, n, grow, pulse;
    GXColor color, core;
    int shape;
    int item; /* shot: SHOT_RAY, SHOT_STAR */
    bool ground, vis, ahead; /* ahead: offsets in the fighter's axes */
} Vfx;

static Vfx s_vfx[PC_FIGHTER_PACKS_MAX][VFX_MAX];
static int s_count[PC_FIGHTER_PACKS_MAX];
static bool s_loaded[PC_FIGHTER_PACKS_MAX];

/* The trails' last positions, per player and trail. */
typedef struct VfxTrail {
    int anim;
    float frame;
    int n;
    Vec3 a[TRAIL_LEN], b[TRAIL_LEN];
} VfxTrail;
static VfxTrail s_trail[8][VFX_MAX];

void ftCustomVfx_Reset(void)
{
    memset(s_loaded, 0, sizeof s_loaded);
    memset(s_count, 0, sizeof s_count);
}

static GXColor parse_color(const char* s)
{
    unsigned v = (unsigned) strtoul(s, NULL, 16);
    GXColor c = { (u8) (v >> 16), (u8) (v >> 8), (u8) v, 0xFF };
    return c;
}

static void parse_vec(const char* s, Vec3* v)
{
    v->x = v->y = v->z = 0;
    sscanf(s, "%f,%f,%f", &v->x, &v->y, &v->z);
}

static void parse_line(Vfx* e, char* line)
{
    static const char* kinds[] = { "glow", "beam", "blade", "trail", "stream", "burst", "ring", "shot" };
    char* tok[32];
    int n = 0, i;
    for (char* t = strtok(line, " \t\r\n"); t != NULL && n < 32; t = strtok(NULL, " \t\r\n")) {
        tok[n++] = t;
    }
    memset(e, 0, sizeof *e);
    e->kind = -1;
    if (n < 4) {
        return;
    }
    if (strcmp(tok[0], "*") != 0) {
        for (char* m = strtok(tok[0], ","); m != NULL && e->n_motions < VFX_MOTIONS; m = strtok(NULL, ",")) {
            snprintf(e->motion[e->n_motions++], sizeof e->motion[0], "%s", m);
        }
    }
    for (i = 0; i < (int) (sizeof kinds / sizeof kinds[0]); i++) {
        if (strcmp(tok[1], kinds[i]) == 0) {
            e->kind = i;
        }
    }
    e->from = strcmp(tok[2], "*") == 0 ? 0 : atoi(tok[2]);
    e->to = strcmp(tok[3], "*") == 0 ? -1 : atoi(tok[3]);
    e->at2 = -1;
    e->len = 20;
    e->w = 1.5F;
    e->r0 = 2;
    e->r1 = 6;
    e->alpha = 1;
    e->life = 12;
    e->speed = 1;
    e->spread = 20;
    e->n = 1;
    e->grow = 1;
    e->color = parse_color("FFFFFF");
    e->core = parse_color("FFFFFF");
    for (i = 4; i < n; i++) {
        char* eq = strchr(tok[i], '=');
        const char* k = tok[i];
        const char* v;
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        v = eq + 1;
        if (!strcmp(k, "at")) e->at = atoi(v);
        else if (!strcmp(k, "at2")) e->at2 = atoi(v);
        else if (!strcmp(k, "off")) parse_vec(v, &e->off);
        else if (!strcmp(k, "off2")) parse_vec(v, &e->off2);
        else if (!strcmp(k, "dir")) e->dir = (float) atof(v);
        else if (!strcmp(k, "len")) e->len = (float) atof(v);
        else if (!strcmp(k, "w")) e->w = (float) atof(v);
        else if (!strcmp(k, "r0")) e->r0 = (float) atof(v);
        else if (!strcmp(k, "r1")) e->r1 = (float) atof(v);
        else if (!strcmp(k, "alpha")) e->alpha = (float) atof(v);
        else if (!strcmp(k, "life")) e->life = (float) atof(v);
        else if (!strcmp(k, "speed")) e->speed = (float) atof(v);
        else if (!strcmp(k, "spread")) e->spread = (float) atof(v);
        else if (!strcmp(k, "n")) e->n = (float) atof(v);
        else if (!strcmp(k, "grow")) e->grow = (float) atof(v);
        else if (!strcmp(k, "pulse")) e->pulse = (float) atof(v);
        else if (!strcmp(k, "color")) e->color = parse_color(v);
        else if (!strcmp(k, "core")) e->core = parse_color(v);
        else if (!strcmp(k, "ground")) e->ground = atoi(v) != 0;
        else if (!strcmp(k, "vis")) e->vis = atoi(v) != 0;
        else if (!strcmp(k, "axes")) e->ahead = !strcmp(v, "fighter");
        else if (!strcmp(k, "item")) e->item = !strcmp(v, "ray") ? SHOT_RAY : !strcmp(v, "star") ? SHOT_STAR : 0;
        else if (!strcmp(k, "shape")) {
            e->shape = !strcmp(v, "flake")  ? SHAPE_FLAKE
                       : !strcmp(v, "spark") ? SHAPE_SPARK
                       : !strcmp(v, "spear") ? SHAPE_SPEAR
                       : !strcmp(v, "disc")  ? SHAPE_DISC
                                             : SHAPE_GLOW;
        }
    }
    if (e->at2 < 0) {
        e->at2 = e->at;
    }
    if (e->life < 1) {
        e->life = 1;
    }
}

static void load(int p)
{
    char path[600], line[512];
    FILE* f;
    s_loaded[p] = true;
    s_count[p] = 0;
    snprintf(path, sizeof path, "%s/vfx.cfg", pc_fighter_pack(p)->dir);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL && s_count[p] < VFX_MAX) {
        char* c = strchr(line, '#');
        if (c != NULL) {
            *c = '\0';
        }
        parse_line(&s_vfx[p][s_count[p]], line);
        if (s_vfx[p][s_count[p]].kind >= 0) {
            s_count[p]++;
        }
    }
    fclose(f);
}

/* ---- drawing ------------------------------------------------------------- */

static Vec3 s_eye, s_right, s_up;

static float hrand(u32 a, u32 b)
{
    u32 x = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    return (float) (x & 0xFFFFFF) / 16777216.0F;
}

static Vec3 v3(float x, float y, float z)
{
    Vec3 v = { x, y, z };
    return v;
}

static Vec3 vadd(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static Vec3 vsub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static Vec3 vmul(Vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }

static Vec3 vcross(Vec3 a, Vec3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static Vec3 vnorm(Vec3 a)
{
    float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 1e-6F ? vmul(a, 1.0F / l) : v3(0, 1, 0);
}

static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

static void vtx(Vec3 p, GXColor c, float a)
{
    GXPosition3f32(p.x, p.y, p.z);
    GXColor4u8(c.r, c.g, c.b, (u8) (clamp01(a) * 255.0F));
}

static GXColor mix(GXColor a, GXColor b, float t)
{
    GXColor c = { (u8) (a.r + (b.r - a.r) * t), (u8) (a.g + (b.g - a.g) * t), (u8) (a.b + (b.b - a.b) * t),
                  0xFF };
    return c;
}

/* A soft disc facing the camera: bright at the centre, gone at the rim. */
static void glow(Vec3 c, float r, GXColor col, float a)
{
    int i;
    if (r <= 0 || a <= 0) {
        return;
    }
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 14);
    vtx(c, col, a);
    for (i = 0; i <= 12; i++) {
        float t = (float) i * (6.2831853F / 12.0F);
        vtx(vadd(c, vadd(vmul(s_right, r * cosf(t)), vmul(s_up, r * sinf(t)))), col, 0);
    }
    GXEnd();
}

/* A soft bar from a to b facing the camera, w wide either side. */
static void strip(Vec3 a, Vec3 b, float w, GXColor col, float al)
{
    Vec3 s = vmul(vnorm(vcross(vsub(b, a), vsub(a, s_eye))), w);
    if (w <= 0 || al <= 0) {
        return;
    }
    GXBegin(GX_QUADS, GX_VTXFMT0, 8);
    vtx(vsub(a, s), col, 0);
    vtx(vsub(b, s), col, 0);
    vtx(b, col, al);
    vtx(a, col, al);
    vtx(a, col, al);
    vtx(b, col, al);
    vtx(vadd(b, s), col, 0);
    vtx(vadd(a, s), col, 0);
    GXEnd();
}

static void bar(Vec3 a, Vec3 b, float w, GXColor col, GXColor core, float al)
{
    strip(a, b, w, col, al * 0.8F);
    strip(a, b, w * 0.4F, core, al);
    glow(a, w * 1.4F, col, al * 0.6F);
    glow(b, w * 1.4F, col, al * 0.6F);
}

/* A ring of radius r about c in the plane across n, w thick. */
static void ring(Vec3 c, Vec3 n, float r, float w, GXColor col, float al)
{
    Vec3 u = vnorm(vcross(n, fabsf(n.y) < 0.9F ? v3(0, 1, 0) : v3(1, 0, 0)));
    Vec3 v = vcross(n, u);
    int i;
    if (r <= 0 || al <= 0) {
        return;
    }
    GXBegin(GX_QUADS, GX_VTXFMT0, 24 * 8);
    for (i = 0; i < 24; i++) {
        float t0 = (float) i * (6.2831853F / 24), t1 = (float) (i + 1) * (6.2831853F / 24);
        Vec3 d0 = vadd(vmul(u, cosf(t0)), vmul(v, sinf(t0)));
        Vec3 d1 = vadd(vmul(u, cosf(t1)), vmul(v, sinf(t1)));
        vtx(vadd(c, vmul(d0, r - w)), col, 0);
        vtx(vadd(c, vmul(d1, r - w)), col, 0);
        vtx(vadd(c, vmul(d1, r)), col, al);
        vtx(vadd(c, vmul(d0, r)), col, al);
        vtx(vadd(c, vmul(d0, r)), col, al);
        vtx(vadd(c, vmul(d1, r)), col, al);
        vtx(vadd(c, vmul(d1, r + w)), col, 0);
        vtx(vadd(c, vmul(d0, r + w)), col, 0);
    }
    GXEnd();
}

static void particle(int shape, Vec3 p, Vec3 vel, float size, GXColor col, GXColor core, float a, u32 seed)
{
    switch (shape) {
    case SHAPE_FLAKE: {
        /* three crossed bars: a six-pointed crystal, turning */
        const float spin = hrand(seed, 9) * 6.2831853F;
        int k;
        for (k = 0; k < 3; k++) {
            const float t = spin + (float) k * 1.0471976F;
            const Vec3 d = vadd(vmul(s_right, size * cosf(t)), vmul(s_up, size * sinf(t)));
            strip(vsub(p, d), vadd(p, d), size * 0.18F, col, a);
        }
        glow(p, size * 0.6F, core, a * 0.7F);
        break;
    }
    case SHAPE_SPARK:
        strip(vsub(p, vmul(vnorm(vel), size * 1.5F)), vadd(p, vmul(vnorm(vel), size * 0.5F)), size * 0.3F, col,
              a);
        glow(p, size * 0.5F, core, a);
        break;
    default:
        glow(p, size, col, a * 0.7F);
        glow(p, size * 0.4F, core, a);
        break;
    }
}

/* A point on the fighter: at a joint with an offset in its axes, or (joint
 * 0) at its feet, offset forward and up. */
static bool point(Fighter* fp, int at, const Vec3* off, float sc, Vec3* out, bool ahead)
{
    FighterPartsTable* parts = DP(FighterPartsTable, ftPartsTable[fp->kind].v);
    if (ahead && at > 0) {
        const Vec3 zero = { 0, 0, 0 };
        if (!point(fp, at, &zero, sc, out, false)) {
            return false;
        }
        out->x += off->x * sc * fp->facing_dir;
        out->y += off->y * sc;
        out->z += off->z * sc;
        return true;
    }
    if (at <= 0) {
        *out = v3(fp->cur_pos.x + off->x * sc * fp->facing_dir, fp->cur_pos.y + off->y * sc, fp->cur_pos.z + off->z * sc);
        return true;
    }
    if ((u32) at >= parts->parts_num || fp->parts[at].joint == NULL) {
        return false;
    }
    lb_8000B1CC(fp->parts[at].joint, (Vec3*) off, out);
    return true;
}

static Vec3 ahead(Fighter* fp, float deg)
{
    const float r = deg * (3.14159265F / 180.0F);
    return v3(cosf(r) * fp->facing_dir, sinf(r), 0);
}

static void draw_particles(Fighter* fp, const Vfx* e, int idx, float f, float sc, float env, Vec3 o)
{
    const bool burst = e->kind == VFX_BURST;
    const float rate = e->n > 0 ? e->n : 1;
    const float last = e->to < 0 ? f : (f < (float) e->to ? f : (float) e->to);
    int k, k0, k1;
    if (burst) {
        k0 = 0;
        k1 = (int) e->n - 1;
    } else {
        k0 = (int) ceilf((f - e->life - (float) e->from) * rate);
        k1 = (int) floorf((last - (float) e->from) * rate);
        if (k0 < 0) {
            k0 = 0;
        }
        if (k1 - k0 > 160) {
            k0 = k1 - 160;
        }
    }
    for (k = k0; k <= k1; k++) {
        const u32 seed = (u32) (k * 131 + idx * 7919 + fp->player_idx * 104729);
        const float born = burst ? (float) e->from : (float) e->from + (float) k / rate;
        const float age = f - born;
        Vec3 d, p;
        float t, size;
        if (age < 0 || age > e->life) {
            continue;
        }
        t = age / e->life;
        if (burst) {
            /* any way out: a direction on the sphere, flattened a little */
            const float th = hrand(seed, 1) * 6.2831853F, z = hrand(seed, 2) * 2 - 1;
            const float rr = sqrtf(1 - z * z);
            d = v3(rr * cosf(th), rr * sinf(th), z * 0.5F);
        } else {
            const float a = e->dir + (hrand(seed, 1) - 0.5F) * e->spread;
            d = ahead(fp, a);
            d.z = (hrand(seed, 2) - 0.5F) * sinf(e->spread * (3.14159265F / 180.0F));
        }
        p = vadd(o, vmul(d, e->speed * sc * age * (0.75F + 0.5F * hrand(seed, 3))));
        size = e->w * sc * (1 + (e->grow - 1) * t) * (0.7F + 0.6F * hrand(seed, 4));
        particle(e->shape, p, d, size, e->color, e->core, e->alpha * env * (1 - t * t), seed);
    }
}

static void draw_trail(Fighter* fp, const Vfx* e, int idx, float f, float sc, float env)
{
    VfxTrail* tr = &s_trail[fp->player_idx & 7][idx];
    Vec3 a, b;
    int k, n;
    if (!point(fp, e->at, &e->off, sc, &a, e->ahead) || !point(fp, e->at2, &e->off2, sc, &b, e->ahead)) {
        return;
    }
    if (tr->anim != fp->anim_id || f < tr->frame) {
        tr->n = 0;
    }
    if (tr->n == 0 || f != tr->frame) {
        memmove(&tr->a[1], &tr->a[0], sizeof(Vec3) * (TRAIL_LEN - 1));
        memmove(&tr->b[1], &tr->b[0], sizeof(Vec3) * (TRAIL_LEN - 1));
        tr->a[0] = a;
        tr->b[0] = b;
        if (tr->n < TRAIL_LEN) {
            tr->n++;
        }
    }
    tr->anim = fp->anim_id;
    tr->frame = f;
    n = tr->n < (int) e->life ? tr->n : (int) e->life;
    if (n > TRAIL_LEN) {
        n = TRAIL_LEN;
    }
    if (n < 2) {
        return;
    }
    GXBegin(GX_QUADS, GX_VTXFMT0, (u16) ((n - 1) * 4));
    for (k = 0; k + 1 < n; k++) {
        const float a0 = e->alpha * env * (1 - (float) k / (float) n);
        const float a1 = e->alpha * env * (1 - (float) (k + 1) / (float) n);
        const GXColor c0 = mix(e->core, e->color, (float) k / (float) n);
        const GXColor c1 = mix(e->core, e->color, (float) (k + 1) / (float) n);
        vtx(tr->a[k], c0, a0 * 0.3F);
        vtx(tr->b[k], c0, a0);
        vtx(tr->b[k + 1], c1, a1);
        vtx(tr->a[k + 1], c1, a1 * 0.3F);
    }
    GXEnd();
}

static void draw_one(Fighter* fp, const Vfx* e, int idx, float f, float sc)
{
    const bool always = e->to < 0;
    const float span = always ? 1.0F : (float) (e->to - e->from > 0 ? e->to - e->from : 1);
    const float t = always ? 0 : clamp01((f - (float) e->from) / span);
    float env = 1;
    float pulse = e->pulse > 0 ? 1 + 0.15F * sinf(f * e->pulse) : 1;
    Vec3 o, b;
    if (!always && e->kind != VFX_STREAM && e->kind != VFX_BURST && e->kind != VFX_TRAIL) {
        env = clamp01((f - (float) e->from + 1) / 2);
        if (e->kind != VFX_RING) {
            const float out = clamp01(((float) e->to - f + 1) / 3);
            env = env < out ? env : out;
        }
    }
    switch (e->kind) {
    case VFX_GLOW:
        if (point(fp, e->at, &e->off, sc, &o, e->ahead)) {
            const float r = (e->r0 + (e->r1 - e->r0) * t) * sc * pulse;
            glow(o, r, e->color, e->alpha * env * 0.8F);
            glow(o, r * 0.45F, e->core, e->alpha * env);
        }
        break;
    case VFX_BEAM:
        if (point(fp, e->at, &e->off, sc, &o, e->ahead)) {
            const float grow = always ? 1 : clamp01((f - (float) e->from + 1) / 3);
            b = vadd(o, vmul(ahead(fp, e->dir), e->len * sc * grow));
            bar(o, b, e->w * sc * (1 + 0.12F * sinf(f * 2.3F)), e->color, e->core, e->alpha * env);
        }
        break;
    case VFX_BLADE:
        if (point(fp, e->at, &e->off, sc, &o, e->ahead) && point(fp, e->at2, &e->off2, sc, &b, e->ahead)) {
            bar(o, b, e->w * sc * pulse, e->color, e->core, e->alpha * env);
        }
        break;
    case VFX_TRAIL:
        draw_trail(fp, e, idx, f, sc, 1);
        break;
    case VFX_STREAM:
    case VFX_BURST:
        if (point(fp, e->at, &e->off, sc, &o, e->ahead)) {
            draw_particles(fp, e, idx, f, sc, 1, o);
        }
        break;
    case VFX_RING: {
        Vec3 n = e->ground ? v3(0, 1, 0) : ahead(fp, e->dir);
        if (e->ground) {
            o = v3(fp->cur_pos.x, fp->cur_pos.y + 0.3F, fp->cur_pos.z);
        } else if (!point(fp, e->at, &e->off, sc, &o, e->ahead)) {
            break;
        }
        ring(o, n, (e->r0 + (e->r1 - e->r0) * t) * sc, e->w * sc, e->color, e->alpha * (1 - t) * env);
        ring(o, n, (e->r0 + (e->r1 - e->r0) * t) * sc, e->w * sc * 0.35F, e->core, e->alpha * (1 - t) * env);
        break;
    }
    }
}

/* Additive, lit by nothing, tested against the scene's depth but not
 * written to it. */
static void gx_begin(HSD_CObj* cobj)
{
    Mtx view;
    HSD_StateInvalidate(-1);
    HSD_StateInitTev();
    HSD_CObjGetViewingMtx(cobj, view);
    HSD_CObjGetEyePosition(cobj, &s_eye);
    s_right = vnorm(v3(view[0][0], view[0][1], view[0][2]));
    s_up = vnorm(v3(view[1][0], view[1][1], view[1][2]));
    GXSetColorUpdate(GX_ENABLE);
    GXSetAlphaUpdate(GX_DISABLE);
    {
        GXColor black = { 0, 0, 0, 0 };
        GXSetFog(GX_FOG_NONE, 0.0F, 0.0F, 0.0F, 0.0F, black);
    }
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetZCompLoc(GX_TRUE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    HSD_ClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXLoadPosMtxImm(view, 0);
}

static const Vfx* shot_of(int p, int item)
{
    int i;
    for (i = 0; i < s_count[p]; i++) {
        if (s_vfx[p][i].kind == VFX_SHOT && s_vfx[p][i].item == item) {
            return &s_vfx[p][i];
        }
    }
    return NULL;
}

static void draw_shot(const Vfx* e, Vec3 pos, Vec3 vel, float sc, u32 seed, float t)
{
    const Vec3 d = vnorm(fabsf(vel.x) + fabsf(vel.y) > 1e-4F ? vel : v3(1, 0, 0));
    const float w = e->w * sc * (1 + 0.1F * sinf(t * 0.9F));
    const float step = e->len > 0 ? e->len * sc * 0.35F : w;
    int k;
    /* the trail: fading copies back along its flight */
    for (k = (int) e->n; k >= 1; k--) {
        const float a = e->alpha * 0.5F * (1 - (float) k / (e->n + 1));
        glow(vsub(pos, vmul(d, step * (float) k)), w * (1 - 0.08F * (float) k), e->color, a);
    }
    switch (e->shape) {
    case SHAPE_SPEAR:
        bar(vsub(pos, vmul(d, e->len * sc * 0.5F)), vadd(pos, vmul(d, e->len * sc * 0.5F)), w, e->color, e->core,
            e->alpha);
        break;
    case SHAPE_DISC: {
        /* a ring spinning about its flight, a bright hub */
        const float a = t * 0.5F + hrand(seed, 3) * 6.28F;
        ring(pos, vnorm(v3(cosf(a), 0.35F, sinf(a))), w * 1.6F, w * 0.45F, e->color, e->alpha);
        glow(pos, w, e->core, e->alpha);
        break;
    }
    case SHAPE_FLAKE:
    case SHAPE_SPARK:
        particle(e->shape, pos, d, w, e->color, e->core, e->alpha, seed);
        break;
    default:
        glow(pos, w * 1.8F, e->color, e->alpha * 0.8F);
        glow(pos, w * 0.7F, e->core, e->alpha);
        break;
    }
}

bool ftCustomVfx_DrawItem(HSD_GObj* gobj, int pass)
{
    Item* ip = GET_ITEM(gobj);
    const Vfx* e;
    Fighter* fp;
    HSD_CObj* cobj;
    int p, item;
    if (ip == NULL || ip->owner == NULL || !ftLib_IsFighter(ip->owner)) {
        return false;
    }
    item = ip->kind == It_Kind_L_Gun_Ray ? SHOT_RAY : ip->kind == It_Kind_StarRod_Star ? SHOT_STAR : 0;
    fp = GET_FIGHTER(ip->owner);
    p = item != 0 ? ftCustom_Pack(fp->kind, fp->costume_id) : -1;
    if (p < 0 || p >= PC_FIGHTER_PACKS_MAX) {
        return false;
    }
    if (!s_loaded[p]) {
        load(p);
    }
    if ((e = shot_of(p, item)) == NULL) {
        return false;
    }
    if (pass == 2 && (cobj = HSD_CObjGetCurrent()) != NULL) {
        const u32 seed = (u32) (uintptr_t) gobj;
        gx_begin(cobj);
        draw_shot(e, ip->pos, ip->x40_vel, fp->x34_scale.y * pc_fighter_pack(p)->size, seed,
                  (float) (VIGetRetraceCount() & 0xFFFF));
        HSD_StateInvalidate(-1);
        HSD_StateInitTev();
    }
    return true;
}

/* The motion the fighter's animation is ("SpecialN"), as its row's name. */
static bool motion_is(Fighter* fp, const Vfx* e)
{
    const char* sym;
    const char* at;
    int i;
    if (e->n_motions == 0) {
        return true;
    }
    if (fp->anim_id < 0 || fp->x24 == NULL || (u32) fp->anim_id >= fp->x58C) {
        return false;
    }
    sym = DP(char, fp->x24[fp->anim_id].x0);
    at = sym != NULL ? strstr(sym, "_ACTION_") : NULL;
    if (at == NULL) {
        return false;
    }
    at += 8;
    for (i = 0; i < e->n_motions; i++) {
        size_t len = strlen(e->motion[i]);
        if (len > 0 && e->motion[i][len - 1] == '*') {
            if (strncmp(at, e->motion[i], len - 1) == 0) {
                return true; /* a family: "SpecialS*" */
            }
        } else if (strncmp(at, e->motion[i], len) == 0 && strcmp(at + len, "_figatree") == 0) {
            return true;
        }
    }
    return false;
}

void ftCustomVfx_Draw(Fighter_GObj* gobj, int pass)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const int p = ftCustom_Pack(fp->kind, fp->costume_id);
    HSD_CObj* cobj;
    float f, sc;
    int i;
    bool begun = false;
    if (pass != 2 || p < 0 || p >= PC_FIGHTER_PACKS_MAX) {
        return;
    }
    if (!s_loaded[p]) {
        load(p);
    }
    if (s_count[p] == 0 || (cobj = HSD_CObjGetCurrent()) == NULL) {
        return;
    }
    f = fp->cur_anim_frame;
    sc = fp->x34_scale.y * pc_fighter_pack(p)->size;
    for (i = 0; i < s_count[p]; i++) {
        const Vfx* e = &s_vfx[p][i];
        if ((fp->invisible && !e->vis) || !motion_is(fp, e)) {
            if (e->kind == VFX_TRAIL) {
                s_trail[fp->player_idx & 7][i].n = 0;
            }
            continue;
        }
        if (e->to >= 0 && (f < (float) e->from || f > (float) e->to + (e->kind == VFX_STREAM ? e->life : 0))) {
            if (e->kind == VFX_TRAIL) {
                s_trail[fp->player_idx & 7][i].n = 0;
            }
            continue;
        }
        if (e->kind == VFX_SHOT) {
            continue;
        }
        if (!begun) {
            begun = true;
            gx_begin(cobj);
        }
        draw_one(fp, e, i, f, sc);
    }
    if (begun) {
        HSD_StateInvalidate(-1);
        HSD_StateInitTev();
    }
}
