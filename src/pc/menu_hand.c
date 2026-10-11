/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The character select hand in the other menus (menu_hand.h). A scene that
 * wants the hand loads MnSlChr for its model, as character select does
 * (the archive's pointer slots need the game's own memory, and goes with
 * the scene's heap), and gets a screen-space camera that draws only the
 * hand, on top of everything. */
#include "pc/menu_hand.h"

#include "pc/mouse_menu.h"
#include "pc/pc.h"
#include <dolphin/vi.h>
#include <melee/if/forward.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lblanguage.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/mn/mncharsel.h>
#include <melee/sc/types.h>
#include <math.h>
#include <stdlib.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/wobj.h>

/* The hand's size on screen, in 640x480 canvas units per character select
 * world unit (that screen's camera shows about 64 units across 640). */
#define HAND_SCALE 10.0f
/* From the hand's origin to its fingertips, in the same world units: the
 * fingertips are where it points. */
#define TIP_X 3.0f
#define TIP_Y (-0.8f)

static StaticModelDesc* s_model; /* this scene's, once loaded */
static bool s_failed;            /* not in this scene: not retried */

static HSD_GObj* s_camera; /* this scene's; dropped with it */
static HSD_JObj* s_hand;
static u32 s_want_tick;
static bool s_shown;
static int s_step;
static u32 s_step_tick;

/* Well back from z = 0: the hand is a model with depth, scaled up. */
static HSD_WObjDesc s_eye = {0, {0, 0, 1000}, 0};
static HSD_WObjDesc s_interest = {0, {0, 0, 0}, 0};
/* SisLib's canvas camera (sislib.c): an orthographic 640x480, y down. */
static HSD_CameraDescPerspective s_camera_desc = {
    0,
    0,
    PROJ_PERSPECTIVE,
    {0, 640, 0, 480},
    {0, 640, 0, 480},
    0,
    0,
    0.0f,
    0,
    0.0f,
    0xFFFF,
    30.0f,
    1.3333f,
};

static bool load_model(void) {
    const char* name;
    if (s_model != NULL || s_failed) {
        return s_model != NULL;
    }
    name = lbLang_IsSavedLanguageJP() ? "MnSlChr.dat" : "MnSlChr.usd";
    s_model = mnCharSel_PcHandModel(lbArchive_LoadArchive(name));
    s_failed = s_model == NULL;
    if (s_failed) {
        pc_log_line("menu hand: no hand model in %s", name);
    }
    return !s_failed;
}

static void render(HSD_GObj* gobj, intptr_t pass) {
    int i;
    (void)pass;
    if (!s_shown || s_hand == NULL) {
        return;
    }
    if (HSD_CObjSetCurrent(gobj->hsd_obj)) {
        for (i = 0; i < 3; i++) {
            HSD_JObjDispAll(s_hand, NULL, HSD_GObj_80390EB8(i), 0);
        }
        HSD_CObjEndCurrent();
    }
}

/* Pose 2 (open, pointing) with the port 1 tag, as character select draws
 * the hand that holds no coin. */
static void set_pose(HSD_JObj* hand) {
    HSD_JObj* part;
    lb_80011E24(hand, &part, 2, -1);
    HSD_ForeachAnim(part, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF, 2.0f);
    HSD_JObjAnimAll(part);
    HSD_ForeachAnim(part, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL);
    lb_80011E24(hand, &part, 3, -1);
    HSD_ForeachAnim(part, JOBJ_TYPE, TOBJ_MASK, HSD_AObjReqAnim, AOBJ_ARG_AF, 0.0f);
    HSD_JObjAnimAll(part);
    HSD_ForeachAnim(part, JOBJ_TYPE, TOBJ_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL);
}

static bool build(void) {
    HSD_CObj* cobj;
    if (s_camera != NULL) {
        return true;
    }
    if (!load_model()) {
        return false;
    }
    s_camera = GObj_Create(HSD_GOBJ_CLASS_UI, 0, 0);
    if (s_camera == NULL) {
        return false;
    }
    DP_SET(s_camera_desc.eyepos, &s_eye);
    DP_SET(s_camera_desc.interest, &s_interest);
    cobj = HSD_CObjLoadDesc((HSD_CObjDesc*)&s_camera_desc);
    HSD_CObjSetOrtho(cobj, 0.0f, -480.0f, 0.0f, 640.0f);
    HSD_GObjObject_80390A70(s_camera, HSD_GObj_CameraKind, cobj);
    /* Last of the cameras, so the hand is over everything. */
    GObj_SetupGXLinkMax(s_camera, render, 255);
    s_camera->gxlink_prios = 0;

    s_hand = HSD_JObjLoadJoint(DP(HSD_Joint, s_model->joint));
    HSD_JObjAddAnimAll(s_hand, DP(HSD_AnimJoint, s_model->animjoint),
        DP(HSD_MatAnimJoint, s_model->matanim_joint),
        DP(HSD_ShapeAnimJoint, s_model->shapeanim_joint));
    HSD_JObjReqAnimAll(s_hand, 0.0f);
    HSD_JObjAnimAll(s_hand);
    HSD_ForeachAnim(s_hand, JOBJ_TYPE, ALL_TYPE_MASK, HSD_AObjStopAnim, AOBJ_ARG_AOV, NULL);
    set_pose(s_hand);
    HSD_JObjSetScaleX(s_hand, HAND_SCALE);
    HSD_JObjSetScaleY(s_hand, HAND_SCALE);
    HSD_JObjSetScaleZ(s_hand, HAND_SCALE);
    return true;
}

int pc_menu_hand_row(
    HSD_CObj* cobj, HSD_JObj* const* labels, int n, float left, float right, float half_row) {
    int i, best = -1;
    float best_d = half_row, mx, my;
    const bool debug = getenv("MELEE_HAND_DEBUG") != NULL;
    if (!s_shown || cobj == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        MtxPtr m;
        float d;
        if (labels[i] == NULL) {
            continue;
        }
        m = HSD_JObjGetMtxPtr(labels[i]);
        if (!pc_mouse_menu_world(cobj, m[2][3], &mx, &my)) {
            return -1;
        }
        if (debug) {
            pc_log_line("hand: row %d at %.2f %.2f %.2f, hand %.2f %.2f", i, m[0][3], m[1][3],
                m[2][3], mx, my);
        }
        d = fabsf(my - m[1][3]);
        if (mx > m[0][3] + left && mx < m[0][3] + right && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

int pc_menu_hand_take_wheel(void) {
    const int notches = pc_mouse_take_wheel();
    return s_shown ? notches : 0;
}

void pc_menu_hand_want(void) {
    s_want_tick = VIGetRetraceCount();
}

bool pc_menu_hand_shown(void) {
    return s_shown;
}

void pc_menu_hand_step(int dir) {
    s_step = dir;
}

int pc_menu_hand_take_step(void) {
    const u32 tick = VIGetRetraceCount();
    int step = s_step;
    if (!s_shown || tick == s_step_tick) {
        return 0;
    }
    s_step_tick = tick;
    s_step = 0;
    return step;
}

void pc_menu_hand_frame(void) {
    float x, y;
    const bool wanted = s_want_tick != 0 && VIGetRetraceCount() - s_want_tick <= 1;
    s_shown = false;
    if (!wanted || !build()) {
        pc_mouse_show_pointer(true);
        return;
    }
    if (!pc_mouse_menu_world(s_camera->hsd_obj, 0.0f, &x, &y)) {
        pc_mouse_show_pointer(true);
        return;
    }
    HSD_JObjSetTranslateX(s_hand, x - TIP_X * HAND_SCALE);
    HSD_JObjSetTranslateY(s_hand, y - TIP_Y * HAND_SCALE);
    s_shown = true;
    pc_mouse_show_pointer(false);
    if (getenv("MELEE_HAND_DEBUG") != NULL) {
        pc_log_line("hand: at %.1f %.1f", x, y);
    }
}

void pc_menu_hand_forget(void) {
    s_model = NULL;
    s_failed = false;
    s_camera = NULL;
    s_hand = NULL;
    s_shown = false;
    s_step = 0;
    s_want_tick = 0;
    pc_mouse_show_pointer(true);
}
