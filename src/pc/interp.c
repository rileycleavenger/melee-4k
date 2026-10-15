/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Transform interpolation for high-refresh presentation (see interp.h).
 *
 * Joints: every JObj tree hanging off a GObj is walked, its world matrices
 * are brought up to date for the current tick (exactly what the render would
 * do lazily), and recorded in a pointer-keyed table. For the in-between frame
 * each matrix is replaced by a blend with the previous tick's: translation
 * and scale lerped, rotation slerped. Matrices are blended as final world
 * matrices and left clean, so no child is ever rebuilt from a blended parent,
 * and skinned meshes (which read the bone matrices) follow for free.
 *
 * Cameras: eye, interest, roll and field of view are blended the same way.
 * The in-game camera is rebuilt from its CameraTransformState on every draw
 * (Camera_8002A4AC), so that state is blended rather than its CObj.
 *
 * Particles (HSD_Particle, outside the JObj tree): their world position is
 * recorded per tick and blended the same way.
 *
 * Sword trails: each entry of a fighter's 3-entry trail ring is blended
 * with the entry of the same age a tick earlier, so the trail follows the
 * blended blade instead of lagging it by up to a tick.
 *
 * A joint whose position jumped further than a fighter can move in one tick
 * (respawn, warp, a recycled JObj pointer) is drawn where it is now rather
 * than swept across the stage.
 */
#include "pc/compat.h"
#include "pc/interp.h"

#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/psstructs.h>
#include <sysdolphin/baselib/wobj.h>
#include <melee/cm/types.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SNAP_DISTANCE 40.0F /* world units; Melee's fastest launches stay well under */

typedef struct {
    HSD_JObj* jobj; /* NULL = empty slot */
    Mtx mtx;
} JEntry;

typedef struct {
    JEntry* slots;
    unsigned cap; /* power of two */
    unsigned count;
} JTable;

typedef struct {
    HSD_JObj* jobj;
    Mtx real;
    u32 flags;
} Restore;

typedef struct {
    HSD_CObj* cobj;
    Vec3 eye, interest;
    float up[3];
    float fov;
} CamState;

#define MAX_CAMS 16

static JTable s_tables[2];
static int s_cur; /* s_tables[s_cur] is this tick, the other the previous */
static Restore* s_restore;
static unsigned s_restore_count, s_restore_cap;
/* JObjs the capture brought up to date that were stale (dirty) before it. */
static Restore* s_pre;
static unsigned s_pre_count, s_pre_cap;
static CamState s_cams[2][MAX_CAMS];
static int s_cam_count[2];
static CamState s_cam_restore[MAX_CAMS];
static int s_cam_restore_count;

/* Particles: position per tick, plus what identifies the particle so a
 * recycled HSD_Particle is not blended from the one it replaced. */
typedef struct {
    HSD_Particle* pp;
    Vec3 pos;
    u32 kind;
    u16 life;
    u8 bank;
} PState;

typedef struct {
    PState* items;
    unsigned count, cap;
    unsigned* index; /* open addressing into items, UINT32_MAX = empty */
    unsigned index_cap;
} PTable;

static PTable s_ptcl[2];
static PState* s_ptcl_restore;
static unsigned s_ptcl_restore_count, s_ptcl_restore_cap;

/* Sword trails (src/melee/ft/ftafterimage.c): a 3-entry ring of blade
 * position/direction per fighter, written once per tick and drawn as a
 * curve through the entries. */
typedef struct {
    Fighter* fp;
    Fighter_x20B0_t ring[3];
    u8 next; /* x2101_bits_0to6: slot the next entry is written to */
    s8 count;
} TrailState;

#define MAX_TRAILS 12 /* 6 player slots x 2 entities (Ice Climbers, Zelda/Sheik) */
static TrailState s_trail[2][MAX_TRAILS];
static int s_trail_count[2];
static TrailState s_trail_restore[MAX_TRAILS];
static int s_trail_restore_count;

/* The game camera's transform states (src/melee/cm/camera.c), which its
 * render callback turns into the CObj on every draw. */
CameraTransformState* pc_camera_transform(int which);
Vec2* pc_camera_translation(void);
static CameraTransformState s_gcam[2][2];
static Vec2 s_gcam_shift[2];
static Vec2 s_gcam_shift_restore;
static bool s_gcam_valid[2];
static CameraTransformState s_gcam_restore[2];
static bool s_gcam_restored;

/* Live particle lists, one per link (src/sysdolphin/baselib/particle.c). */
extern HSD_Particle* hsd_804D0908[16];
extern bool pc_particles_live; /* particle.c */

static unsigned hash_ptr(const void* p) {
    uintptr_t v = (uintptr_t)p;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    return (unsigned)v;
}

static void table_clear(JTable* t) {
    if (t->slots != NULL) {
        memset(t->slots, 0, sizeof(JEntry) * t->cap);
    }
    t->count = 0;
}

static void table_put(JTable* t, HSD_JObj* jobj, const Mtx m);

static void table_grow(JTable* t) {
    JTable old = *t;
    t->cap = old.cap ? old.cap * 2 : 4096;
    t->slots = calloc(t->cap, sizeof(JEntry));
    t->count = 0;
    for (unsigned i = 0; i < old.cap; i++) {
        if (old.slots[i].jobj != NULL) {
            table_put(t, old.slots[i].jobj, old.slots[i].mtx);
        }
    }
    free(old.slots);
}

static void table_put(JTable* t, HSD_JObj* jobj, const Mtx m) {
    if ((t->count + 1) * 2 > t->cap) {
        table_grow(t);
    }
    unsigned i = hash_ptr(jobj) & (t->cap - 1);
    while (t->slots[i].jobj != NULL && t->slots[i].jobj != jobj) {
        i = (i + 1) & (t->cap - 1);
    }
    if (t->slots[i].jobj == NULL) {
        t->count++;
    }
    t->slots[i].jobj = jobj;
    memcpy(t->slots[i].mtx, m, sizeof(Mtx));
}

static const JEntry* table_get(const JTable* t, const HSD_JObj* jobj) {
    if (t->count == 0) {
        return NULL;
    }
    unsigned i = hash_ptr(jobj) & (t->cap - 1);
    while (t->slots[i].jobj != NULL) {
        if (t->slots[i].jobj == jobj) {
            return &t->slots[i];
        }
        i = (i + 1) & (t->cap - 1);
    }
    return NULL;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Split a world matrix into scale, rotation quaternion and translation.
 * False when it is degenerate (a zero scale hides a part) or sheared (a
 * non-uniformly scaled parent), where only a plain lerp is safe. */
static bool decompose(const Mtx m, float scale[3], float q[4]) {
    float c[3][3];
    for (int j = 0; j < 3; j++) {
        float x = m[0][j], y = m[1][j], z = m[2][j];
        scale[j] = sqrtf(x * x + y * y + z * z);
        if (!(scale[j] > 1e-6F)) {
            return false;
        }
        c[j][0] = x / scale[j];
        c[j][1] = y / scale[j];
        c[j][2] = z / scale[j];
    }
    for (int a = 0; a < 3; a++) {
        int b = (a + 1) % 3;
        float d = c[a][0] * c[b][0] + c[a][1] * c[b][1] + c[a][2] * c[b][2];
        if (fabsf(d) > 0.01F) {
            return false;
        }
    }
    float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) -
                c[1][0] * (c[0][1] * c[2][2] - c[0][2] * c[2][1]) +
                c[2][0] * (c[0][1] * c[1][2] - c[0][2] * c[1][1]);
    if (det < 0) { /* mirrored: carry the flip in the x scale */
        scale[0] = -scale[0];
        c[0][0] = -c[0][0];
        c[0][1] = -c[0][1];
        c[0][2] = -c[0][2];
    }
    /* r[row][col] = c[col][row] */
    float r00 = c[0][0], r01 = c[1][0], r02 = c[2][0];
    float r10 = c[0][1], r11 = c[1][1], r12 = c[2][1];
    float r20 = c[0][2], r21 = c[1][2], r22 = c[2][2];
    float tr = r00 + r11 + r22;
    if (tr > 0) {
        float s = sqrtf(fmaxf(tr + 1.0F, 1e-12F)) * 2;
        q[3] = 0.25F * s;
        q[0] = (r21 - r12) / s;
        q[1] = (r02 - r20) / s;
        q[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        float s = sqrtf(fmaxf(1.0F + r00 - r11 - r22, 1e-12F)) * 2;
        q[3] = (r21 - r12) / s;
        q[0] = 0.25F * s;
        q[1] = (r01 + r10) / s;
        q[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        float s = sqrtf(fmaxf(1.0F + r11 - r00 - r22, 1e-12F)) * 2;
        q[3] = (r02 - r20) / s;
        q[0] = (r01 + r10) / s;
        q[1] = 0.25F * s;
        q[2] = (r12 + r21) / s;
    } else {
        float s = sqrtf(fmaxf(1.0F + r22 - r00 - r11, 1e-12F)) * 2;
        q[3] = (r10 - r01) / s;
        q[0] = (r02 + r20) / s;
        q[1] = (r12 + r21) / s;
        q[2] = 0.25F * s;
    }
    return true;
}

static void blend_mtx(Mtx out, const Mtx a, const Mtx b, float t) {
    float sa[3], sb[3], qa[4], qb[4];
    if (!decompose(a, sa, qa) || !decompose(b, sb, qb) || (sa[0] < 0) != (sb[0] < 0)) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                out[r][c] = lerpf(a[r][c], b[r][c], t);
            }
        }
        return;
    }
    float dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (dot < 0) {
        dot = -dot;
        for (int i = 0; i < 4; i++) {
            qb[i] = -qb[i];
        }
    }
    float q[4];
    if (dot > 0.9995F) {
        for (int i = 0; i < 4; i++) {
            q[i] = lerpf(qa[i], qb[i], t);
        }
    } else {
        float th = acosf(dot), s = sinf(th);
        float wa = sinf((1 - t) * th) / s, wb = sinf(t * th) / s;
        for (int i = 0; i < 4; i++) {
            q[i] = qa[i] * wa + qb[i] * wb;
        }
    }
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) {
        q[i] /= n;
    }
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float r[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
        {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
        {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)},
    };
    for (int c = 0; c < 3; c++) {
        float s = lerpf(sa[c], sb[c], t);
        for (int row = 0; row < 3; row++) {
            out[row][c] = r[row][c] * s;
        }
    }
    for (int row = 0; row < 3; row++) {
        out[row][3] = lerpf(a[row][3], b[row][3], t);
    }
    /* Never hand the renderer a non-finite matrix: whatever the
     * decomposition made of a near-degenerate input, a lerp is safe. */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            if (!isfinite(out[r][c])) {
                for (int rr = 0; rr < 3; rr++) {
                    for (int cc = 0; cc < 4; cc++) {
                        out[rr][cc] = lerpf(a[rr][cc], b[rr][cc], t);
                    }
                }
                return;
            }
        }
    }
}

static bool jumped(const Mtx a, const Mtx b) {
    float dx = a[0][3] - b[0][3], dy = a[1][3] - b[1][3], dz = a[2][3] - b[2][3];
    return !(dx * dx + dy * dy + dz * dz < SNAP_DISTANCE * SNAP_DISTANCE);
}

static void push_state(Restore** list, unsigned* count, unsigned* cap, HSD_JObj* jobj) {
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 4096;
        *list = realloc(*list, *cap * sizeof(Restore));
    }
    (*list)[*count].jobj = jobj;
    memcpy((*list)[*count].real, jobj->mtx, sizeof(Mtx));
    (*list)[*count].flags = jobj->flags;
    (*count)++;
}

static void push_restore(HSD_JObj* jobj) {
    push_state(&s_restore, &s_restore_count, &s_restore_cap, jobj);
}

static void walk(HSD_JObj* jobj, JTable* cur) {
    for (; jobj != NULL; jobj = jobj->next) {
        /* Parents first, so each child is rebuilt from an up-to-date parent
         * exactly as the render would. */
        if (jobj->flags & JOBJ_MTX_DIRTY) {
            push_state(&s_pre, &s_pre_count, &s_pre_cap, jobj);
        }
        HSD_JObjSetupMatrix(jobj);
        table_put(cur, jobj, jobj->mtx);
        /* An instance's child is another tree drawn through it, reached
         * from its own GObj. */
        if (!(jobj->flags & JOBJ_INSTANCE)) {
            walk(jobj->child, cur);
        }
    }
}

static void cam_read(HSD_CObj* cobj, CamState* s) {
    s->cobj = cobj;
    memset(&s->eye, 0, sizeof s->eye);
    memset(&s->interest, 0, sizeof s->interest);
    if (cobj->eyepos != NULL) {
        s->eye = cobj->eyepos->pos;
    }
    if (cobj->interest != NULL) {
        s->interest = cobj->interest->pos;
    }
    memcpy(s->up, &cobj->u, sizeof s->up);
    s->fov = cobj->projection_param.perspective.fov;
}

static void cam_write(HSD_CObj* cobj, const CamState* s) {
    if (cobj->eyepos != NULL) {
        cobj->eyepos->pos = s->eye;
    }
    if (cobj->interest != NULL) {
        cobj->interest->pos = s->interest;
    }
    memcpy(&cobj->u, s->up, sizeof s->up);
    if (HSD_CObjGetProjectionType(cobj) == PROJ_PERSPECTIVE) {
        cobj->projection_param.perspective.fov = s->fov;
    }
    /* The view matrix is cached; dirty it (HSD_CObjMtxIsDirty) so the next
     * HSD_CObjSetCurrent rebuilds it from what was just written. */
    cobj->flags |= 1u << 30;
}

static void capture_camera(HSD_CObj* cobj) {
    int n = s_cam_count[s_cur];
    if (n == MAX_CAMS) {
        return;
    }
    /* WObj flag 1: the position is still relative to an animated JObj and
     * only becomes world space inside HSD_WObjGetPosition. Leave those. */
    if ((cobj->eyepos != NULL && (cobj->eyepos->flags & 1)) ||
        (cobj->interest != NULL && (cobj->interest->flags & 1)))
    {
        return;
    }
    cam_read(cobj, &s_cams[s_cur][n]);
    s_cam_count[s_cur] = n + 1;
}

static void apply_camera(const CamState* now, float t) {
    const CamState* before = NULL;
    for (int i = 0; i < s_cam_count[!s_cur]; i++) {
        if (s_cams[!s_cur][i].cobj == now->cobj) {
            before = &s_cams[!s_cur][i];
        }
    }
    if (before == NULL || s_cam_restore_count == MAX_CAMS) {
        return;
    }
    float dx = now->eye.x - before->eye.x, dy = now->eye.y - before->eye.y,
          dz = now->eye.z - before->eye.z;
    if (!(dx * dx + dy * dy + dz * dz < 4 * SNAP_DISTANCE * SNAP_DISTANCE)) {
        return; /* camera cut */
    }
    s_cam_restore[s_cam_restore_count++] = *now;
    CamState mix = *now;
    mix.eye.x = lerpf(before->eye.x, now->eye.x, t);
    mix.eye.y = lerpf(before->eye.y, now->eye.y, t);
    mix.eye.z = lerpf(before->eye.z, now->eye.z, t);
    mix.interest.x = lerpf(before->interest.x, now->interest.x, t);
    mix.interest.y = lerpf(before->interest.y, now->interest.y, t);
    mix.interest.z = lerpf(before->interest.z, now->interest.z, t);
    for (int i = 0; i < 3; i++) {
        mix.up[i] = lerpf(before->up[i], now->up[i], t);
    }
    mix.fov = lerpf(before->fov, now->fov, t);
    cam_write(now->cobj, &mix);
}

static void ptable_clear(PTable* t) {
    t->count = 0;
    if (t->index != NULL) {
        memset(t->index, 0xFF, sizeof(unsigned) * t->index_cap);
    }
}

static void ptable_add(PTable* t, const PState* st) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1024;
        t->items = realloc(t->items, t->cap * sizeof(PState));
    }
    if ((t->count + 1) * 2 > t->index_cap) {
        free(t->index);
        t->index_cap = t->index_cap ? t->index_cap * 2 : 2048;
        t->index = malloc(sizeof(unsigned) * t->index_cap);
        memset(t->index, 0xFF, sizeof(unsigned) * t->index_cap);
        for (unsigned i = 0; i < t->count; i++) {
            unsigned h = hash_ptr(t->items[i].pp) & (t->index_cap - 1);
            while (t->index[h] != UINT32_MAX) {
                h = (h + 1) & (t->index_cap - 1);
            }
            t->index[h] = i;
        }
    }
    t->items[t->count] = *st;
    unsigned h = hash_ptr(st->pp) & (t->index_cap - 1);
    while (t->index[h] != UINT32_MAX) {
        h = (h + 1) & (t->index_cap - 1);
    }
    t->index[h] = t->count++;
}

static const PState* ptable_get(const PTable* t, const HSD_Particle* pp) {
    if (t->count == 0) {
        return NULL;
    }
    unsigned h = hash_ptr(pp) & (t->index_cap - 1);
    while (t->index[h] != UINT32_MAX) {
        if (t->items[t->index[h]].pp == pp) {
            return &t->items[t->index[h]];
        }
        h = (h + 1) & (t->index_cap - 1);
    }
    return NULL;
}

void pc_interp_reset(void) {
    table_clear(&s_tables[0]);
    table_clear(&s_tables[1]);
    ptable_clear(&s_ptcl[0]);
    ptable_clear(&s_ptcl[1]);
    s_cam_count[0] = s_cam_count[1] = 0;
    s_trail_count[0] = s_trail_count[1] = 0;
    s_gcam_valid[0] = s_gcam_valid[1] = false;
    s_gcam_restored = false;
    s_restore_count = 0;
    s_cam_restore_count = 0;
    s_ptcl_restore_count = 0;
    s_trail_restore_count = 0;
}

void pc_interp_capture(void) {
    s_cur = !s_cur;
    JTable* cur = &s_tables[s_cur];
    table_clear(cur);
    ptable_clear(&s_ptcl[s_cur]);
    s_cam_count[s_cur] = 0;
    s_pre_count = 0;

    /* Fighter GObjs alive right now: a player slot can still name one that
     * was freed when its match ended, so only these are dereferenced. */
    HSD_GObj* fighters[32];
    int fighter_count = 0;
    for (int p = 0; p <= HSD_GObjLibInitData.p_link_max; p++) {
        for (HSD_GObj* gobj = HSD_GObjPLinkHead[p]; gobj != NULL; gobj = gobj->next) {
            if (gobj->classifier == HSD_GOBJ_CLASS_FIGHTER && fighter_count < 32) {
                fighters[fighter_count++] = gobj;
            }
            if (gobj->hsd_obj == NULL) {
                continue;
            }
            if (gobj->obj_kind == HSD_GObj_JObjKind) {
                walk(gobj->hsd_obj, cur);
            } else if (gobj->obj_kind == HSD_GObj_CameraKind) {
                capture_camera(gobj->hsd_obj);
            }
        }
    }
    /* Only a scene that set up particles has live lists (particle.c). */
    /* Leave every JObj as the tick left it: one the game had not brought up
     * to date goes back to its old matrix, still dirty. Game code that reads
     * a bone's matrix without setting it up first (as on hardware, where only
     * the render does) must see the same stale value with smooth motion on
     * as off; a bone no mesh is skinned to is never set up by the render. */
    for (unsigned i = s_pre_count; i-- > 0;) {
        memcpy(s_pre[i].jobj->mtx, s_pre[i].real, sizeof(Mtx));
        s_pre[i].jobj->flags = s_pre[i].flags;
    }

    for (int link = 0; link < 16 && pc_particles_live; link++) {
        for (HSD_Particle* pp = hsd_804D0908[link]; pp != NULL; pp = pp->next) {
            PState st = {pp, pp->pos, pp->kind, pp->life, pp->bank};
            ptable_add(&s_ptcl[s_cur], &st);
        }
    }
    s_gcam[s_cur][0] = *pc_camera_transform(0);
    s_gcam[s_cur][1] = *pc_camera_transform(1);
    s_gcam_shift[s_cur] = *pc_camera_translation();
    s_gcam_valid[s_cur] = true;

    s_trail_count[s_cur] = 0;
    for (int slot = 0; slot < 6; slot++) {
        for (int index = 0; index < 2; index++) {
            HSD_GObj* gobj = Player_GetEntityAtIndex(slot, index);
            bool live = false;
            for (int f = 0; f < fighter_count && gobj != NULL; f++) {
                live |= fighters[f] == gobj;
            }
            Fighter* fp = live ? gobj->user_data : NULL;
            if (fp == NULL || fp->x2100 < 2 || s_trail_count[s_cur] == MAX_TRAILS) {
                continue;
            }
            TrailState* st = &s_trail[s_cur][s_trail_count[s_cur]++];
            st->fp = fp;
            memcpy(st->ring, fp->x20B0, sizeof st->ring);
            st->next = fp->x2101_bits_0to6;
            st->count = fp->x2100;
        }
    }
}

static void apply_trails(float t);
static void apply_game_camera(float t);

void pc_interp_apply(float t) {
    const JTable* cur = &s_tables[s_cur];
    const JTable* prev = &s_tables[!s_cur];
    s_restore_count = 0;
    s_cam_restore_count = 0;
    s_ptcl_restore_count = 0;

    for (unsigned i = 0; i < cur->cap; i++) {
        const JEntry* e = &cur->slots[i];
        if (e->jobj == NULL) {
            continue;
        }
        /* Every JObj is put back as it was after the draw (matrix and
         * dirty flag), blended or not: a draw may set up a stale one from
         * a blended parent. */
        push_restore(e->jobj);
        const JEntry* p = table_get(prev, e->jobj);
        Mtx out;
        if (p == NULL || jumped(p->mtx, e->mtx)) {
            memcpy(out, e->mtx, sizeof(Mtx));
        } else {
            blend_mtx(out, p->mtx, e->mtx, t);
        }
        memcpy(e->jobj->mtx, out, sizeof(Mtx));
        e->jobj->flags &= ~JOBJ_MTX_DIRTY;
    }

    for (int i = 0; i < s_cam_count[s_cur]; i++) {
        apply_camera(&s_cams[s_cur][i], t);
    }

    const PTable* pc = &s_ptcl[s_cur];
    for (unsigned i = 0; i < pc->count; i++) {
        const PState* now = &pc->items[i];
        const PState* before = ptable_get(&s_ptcl[!s_cur], now->pp);
        /* Same slot, same particle: same kind and bank, and its remaining
         * life did not go back up (a fresh particle in a recycled slot). */
        if (before == NULL || before->kind != now->kind || before->bank != now->bank ||
            now->life > before->life)
        {
            continue;
        }
        float dx = now->pos.x - before->pos.x, dy = now->pos.y - before->pos.y,
              dz = now->pos.z - before->pos.z;
        if (!(dx * dx + dy * dy + dz * dz < SNAP_DISTANCE * SNAP_DISTANCE)) {
            continue;
        }
        if (s_ptcl_restore_count == s_ptcl_restore_cap) {
            s_ptcl_restore_cap = s_ptcl_restore_cap ? s_ptcl_restore_cap * 2 : 1024;
            s_ptcl_restore = realloc(s_ptcl_restore, s_ptcl_restore_cap * sizeof(PState));
        }
        s_ptcl_restore[s_ptcl_restore_count++] = *now;
        now->pp->pos.x = lerpf(before->pos.x, now->pos.x, t);
        now->pp->pos.y = lerpf(before->pos.y, now->pos.y, t);
        now->pp->pos.z = lerpf(before->pos.z, now->pos.z, t);
    }

    apply_trails(t);
    apply_game_camera(t);
}

static void apply_trails(float t) {
    s_trail_restore_count = 0;
    for (int i = 0; i < s_trail_count[s_cur]; i++) {
        const TrailState* now = &s_trail[s_cur][i];
        const TrailState* before = NULL;
        for (int j = 0; j < s_trail_count[!s_cur]; j++) {
            if (s_trail[!s_cur][j].fp == now->fp) {
                before = &s_trail[!s_cur][j];
            }
        }
        /* Only a ring that advanced by exactly one entry this tick has an
         * entry of every age in both records. */
        if (before == NULL || (before->next + 1) % 3 != now->next) {
            continue;
        }
        int ages = now->count < before->count ? now->count : before->count;
        s_trail_restore[s_trail_restore_count++] = *now;
        for (int age = 0; age < ages; age++) {
            const Fighter_x20B0_t* a = &before->ring[(before->next + 2 - age) % 3];
            const Fighter_x20B0_t* b = &now->ring[(now->next + 2 - age) % 3];
            Fighter_x20B0_t* out = &now->fp->x20B0[(now->next + 2 - age) % 3];
            out->x0.x = lerpf(a->x0.x, b->x0.x, t);
            out->x0.y = lerpf(a->x0.y, b->x0.y, t);
            out->x0.z = lerpf(a->x0.z, b->x0.z, t);
            out->xC.x = lerpf(a->xC.x, b->xC.x, t);
            out->xC.y = lerpf(a->xC.y, b->xC.y, t);
            out->xC.z = lerpf(a->xC.z, b->xC.z, t);
        }
    }
}

static void lerp_vec(Vec3* out, const Vec3* a, const Vec3* b, float t) {
    out->x = lerpf(a->x, b->x, t);
    out->y = lerpf(a->y, b->y, t);
    out->z = lerpf(a->z, b->z, t);
}

static void apply_game_camera(float t) {
    s_gcam_restored = false;
    if (!s_gcam_valid[0] || !s_gcam_valid[1]) {
        return;
    }
    const CameraTransformState* before = s_gcam[!s_cur];
    const CameraTransformState* now = s_gcam[s_cur];
    float dx = now[0].position.x - before[0].position.x;
    float dy = now[0].position.y - before[0].position.y;
    float dz = now[0].position.z - before[0].position.z;
    if (!(dx * dx + dy * dy + dz * dz < 4 * SNAP_DISTANCE * SNAP_DISTANCE)) {
        return; /* camera cut */
    }
    for (int i = 0; i < 2; i++) {
        CameraTransformState* live = pc_camera_transform(i);
        s_gcam_restore[i] = *live;
        lerp_vec(&live->interest, &before[i].interest, &now[i].interest, t);
        lerp_vec(&live->position, &before[i].position, &now[i].position, t);
        live->fov = lerpf(before[i].fov, now[i].fov, t);
    }
    Vec2* shift = pc_camera_translation();
    s_gcam_shift_restore = *shift;
    shift->x = lerpf(s_gcam_shift[!s_cur].x, s_gcam_shift[s_cur].x, t);
    shift->y = lerpf(s_gcam_shift[!s_cur].y, s_gcam_shift[s_cur].y, t);
    s_gcam_restored = true;
}

void pc_interp_restore(void) {
    /* Game code reads bone matrices (hitboxes, camera boxes, hurtboxes), so
     * no blended or draw-built matrix, and no dirty flag a draw cleared, may
     * outlive the frame. */
    for (unsigned i = 0; i < s_restore_count; i++) {
        memcpy(s_restore[i].jobj->mtx, s_restore[i].real, sizeof(Mtx));
        s_restore[i].jobj->flags = s_restore[i].flags;
    }
    s_restore_count = 0;
    for (int i = 0; i < s_cam_restore_count; i++) {
        cam_write(s_cam_restore[i].cobj, &s_cam_restore[i]);
    }
    s_cam_restore_count = 0;
    for (unsigned i = 0; i < s_ptcl_restore_count; i++) {
        s_ptcl_restore[i].pp->pos = s_ptcl_restore[i].pos;
    }
    s_ptcl_restore_count = 0;
    for (int i = 0; i < s_trail_restore_count; i++) {
        memcpy(
            s_trail_restore[i].fp->x20B0, s_trail_restore[i].ring, sizeof s_trail_restore[i].ring);
    }
    s_trail_restore_count = 0;
    if (s_gcam_restored) {
        *pc_camera_transform(0) = s_gcam_restore[0];
        *pc_camera_transform(1) = s_gcam_restore[1];
        *pc_camera_translation() = s_gcam_shift_restore;
        s_gcam_restored = false;
    }
}
