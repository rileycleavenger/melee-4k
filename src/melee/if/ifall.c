#include "ifall.h"

#include "if_2F6E.h"
#include "if_2F72.h"
#include "if_2FD9.h"
#include "ifcoget.h"
#include "ifhazard.h"
#include "ifmagnify.h"
#include "ifnametag.h"
#include "ifstatus.h"
#include "ifstock.h"
#include "iftime.h"
#ifdef TARGET_PC
#include "ifnet.h"
#endif
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbspdisplay.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>

#include "pc/pc.h"
#include "pc/widescreen.h"

static struct ifAll_804A0FD8_t {
    /* 0x00 */ HSD_GObj* gobj;
    /* 0x04 */ HSD_GObj* gobj_2;
    /* 0x08 */ HSD_JObj* x8;
    /* 0x0C */ Vec3 xC;
    /* 0x18 */ Vec3 x18[6];
    /* 0x60 */ Vec3 x60[3];
    /* 0x84 */ Vec3 x84[2];
} ifAll_804A0FD8;

static u8 hidden; // HUD hidden bool

void ifAll_802F3394(void)
{
    ifStatus_802F6898();
    ifTime_HideTimers();
    un_802FD450();
    ifMagnify_802FC8E8();
    un_802FD668();
    un_802FD910();
    un_802FF570();
}

void ifAll_802F33CC(void)
{
    ifStatus_802F68F0();
    ifTime_ShowTimers();
    un_802FD45C();
    ifMagnify_802FC940();
    un_802FD674();
    un_802FD91C();
    un_802FF620();
}

HSD_GObj* ifAll_GetHUDGObj(void)
{
    return ifAll_804A0FD8.gobj;
}

Vec3* ifAll_GetTimerPosition(void)
{
    return &ifAll_804A0FD8.xC;
}

Vec3* ifAll_GetPlayerHUDPosition(int idx)
{
    return &ifAll_804A0FD8.x18[idx];
}

#ifdef TARGET_PC
/* PORT: the wide HUD spreads the timer and player positions toward the
 * screen edges (pc_widescreen_hud_*). Each HUD element copies its position
 * once, when it is made, so a change of aspect or HUD mode mid-match moves
 * the elements already on screen as well as the positions
 * (ifAll_PcRelayoutHud). */
static struct {
    bool valid;
    bool timer;  /* the timer position is widened too */
    int count;   /* players spread out: 2..4, else none */
    float offset; /* the pc_widescreen_hud_offset() applied */
} pc_hud;

static void pc_hud_widen(int count, bool timer)
{
    int i;
    const float offset = pc_widescreen_hud_offset();
    if (timer) {
        ifAll_804A0FD8.xC.x += pc_widescreen_hud_dx(-1, 0, offset);
    }
    if (count >= 2 && count <= 4) {
        for (i = 0; i < count; i++) {
            ifAll_804A0FD8.x18[i].x += pc_widescreen_hud_dx(i, count, offset);
        }
    }
    pc_hud.valid = true;
    pc_hud.timer = pc_hud.timer || timer;
    pc_hud.count = count;
    pc_hud.offset = offset;
}

/* Move a HUD element placed at `pos` by dx. Within a unit, for a damage
 * counter shaking from a hit; the slots are 13+ units apart. */
static void pc_hud_move(const Vec3* pos, float dx)
{
    HSD_GObj* gobj;
    if (dx == 0.0f) {
        return;
    }
    for (gobj = HSD_GObjGXLinkHead[11]; gobj != NULL; gobj = gobj->next_gx) {
        HSD_JObj* jobj;
        if (gobj->obj_kind != HSD_GObj_JObjKind || gobj->hsd_obj == NULL) {
            continue;
        }
        jobj = gobj->hsd_obj;
        if (fabsf(HSD_JObjGetTranslationX(jobj) - pos->x) < 1.0f &&
            fabsf(HSD_JObjGetTranslationY(jobj) - pos->y) < 1.0f)
        {
            HSD_JObjSetTranslateX(jobj, HSD_JObjGetTranslationX(jobj) + dx);
        }
    }
}

void ifAll_PcRelayoutHud(void)
{
    int i;
    float offset;
    if (!pc_hud.valid) {
        return;
    }
    offset = pc_widescreen_hud_offset();
    if (offset == pc_hud.offset) {
        return;
    }
    if (pc_hud.timer) {
        const float dx = pc_widescreen_hud_dx(-1, 0, offset) -
                         pc_widescreen_hud_dx(-1, 0, pc_hud.offset);
        pc_hud_move(&ifAll_804A0FD8.xC, dx);
        ifAll_804A0FD8.xC.x += dx;
    }
    if (pc_hud.count >= 2 && pc_hud.count <= 4) {
        for (i = 0; i < pc_hud.count; i++) {
            const float dx = pc_widescreen_hud_dx(i, pc_hud.count, offset) -
                             pc_widescreen_hud_dx(i, pc_hud.count, pc_hud.offset);
            pc_hud_move(&ifAll_804A0FD8.x18[i], dx);
            ifAll_804A0FD8.x18[i].x += dx;
        }
    }
    pc_hud.offset = offset;
}

void ifAll_PcForgetHud(void)
{
    pc_hud.valid = false;
    pc_hud.timer = false;
    pc_hud.count = 0;
}
#endif

static inline void ifAll_802F343C_inline(int i)
{
    ifAll_804A0FD8.x18[i].x *= 0.65F;
}

void ifAll_802F343C(int count)
{
    HSD_JObj* jobj;
    int i;
    HSD_JObj* child;

    jobj = ifAll_804A0FD8.x8;
    switch (count) {
    case 1:
        lb_80011E24(jobj, &child, 9, -1);
        lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[0]);
        break;
    case 2:
        for (i = 0; i < count; i++) {
            lb_80011E24(jobj, &child, i + 11, -1);
            lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[i]);
        }
        break;
    case 3:
        for (i = 0; i < count; i++) {
            lb_80011E24(jobj, &child, i + 8, -1);
            lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[i]);
        }
        break;
    case 4:
        for (i = 0; i < count; i++) {
            lb_80011E24(jobj, &child, i + 2, -1);
            lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[i]);
        }
        break;
    case 5:
    case 6:
        lb_80011E24(jobj, &child, 6, -1);
        lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[0]);
        ifAll_802F343C_inline(0);

        lb_80011E24(jobj, &child, 7, -1);
        lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[5]);
        ifAll_802F343C_inline(5);

        for (i = 1; i < 5; i++) {
            lb_80011E24(jobj, &child, i + 1, -1);
            lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x18[i]);
            ifAll_802F343C_inline(i);
        }
        break;
    }

#ifdef TARGET_PC
    pc_hud_widen(count, false);
#endif
}

HSD_Archive** ifAll_GetArchive(void)
{
    static HSD_Archive* archive;
    return &archive;
}

void ifAll_HideHUD(void)
{
    hidden = true;
}

void ifAll_ShowHUD(void)
{
    hidden = false;
}

bool ifAll_IsHUDHidden(void)
{
    return hidden;
}

static void fn_802F36B8(HSD_GObj* gobj, intptr_t unused)
{
    if (!ifAll_IsHUDHidden()) {
        if (HSD_CObjSetCurrent(GET_COBJ(gobj))) {
            HSD_GObj_80390ED0(gobj, 0x7);
            HSD_CObjEndCurrent();
        }
    }
}

static void ifAll_802F370C(SceneDesc* scene)
{
    HSD_JObj* child;
    HSD_GObj* gobj;
    HSD_JObj* jobj;
    int i;

    gobj = GObj_Create(0xE, 0xF, 0);
    jobj = HSD_JObjLoadJoint(
        DP(HSD_Joint, DP(DynamicModelDesc, DP(DiscU32, scene->models)[0].v)->joint));
    ifAll_804A0FD8.x8 = jobj;
    lb_80011E24(jobj, &child, 0xD, -1);
    lb_8000B1CC(child, NULL, ifAll_GetTimerPosition());

    lb_80011E24(jobj, &child, 2, -1);
    lb_8000B1CC(child, NULL, ifAll_GetPlayerHUDPosition(0));
    lb_80011E24(jobj, &child, 3, -1);
    lb_8000B1CC(child, NULL, ifAll_GetPlayerHUDPosition(1));
    lb_80011E24(jobj, &child, 4, -1);
    lb_8000B1CC(child, NULL, ifAll_GetPlayerHUDPosition(2));
    lb_80011E24(jobj, &child, 5, -1);
    lb_8000B1CC(child, NULL, ifAll_GetPlayerHUDPosition(3));
    lb_80011E24(jobj, &child, 6, -1);
    lb_8000B1CC(child, NULL, ifAll_GetPlayerHUDPosition(4));
    lb_80011E24(jobj, &child, 7, -1);
    lb_8000B1CC(child, NULL, ifAll_GetPlayerHUDPosition(5));

    for (i = 0; i < 3; i++) {
        lb_80011E24(jobj, &child, i + 8, -1);
        lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x60[i]);
    }
    for (i = 0; i < 2; i++) {
        lb_80011E24(jobj, &child, i + 11, -1);
        lb_8000B1CC(child, NULL, &ifAll_804A0FD8.x84[i]);
    }
    HSD_GObjFree(gobj);

#ifdef TARGET_PC
    pc_hud_widen(4, true);
#endif
}

static inline HSD_GObj* createCamera(HSD_CObjDesc* desc)
{
    HSD_GObj* gobj = GObj_Create(0x13, 0x14, 0);
    HSD_CObj* cobj = lb_80013B14(&desc->perspective);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_CameraKind, cobj);
    GObj_SetupGXLinkMax(gobj, fn_802F36B8, 8);
    gobj->gxlink_prios = 0xD00;
    return gobj;
}

static inline void createLight(SceneDesc* scene)
{
    HSD_LightDesc* desc =
        DP(HSD_LightDesc, DP(DiscU32, scene->lights)[0].v);
    HSD_GObj* gobj = GObj_Create(0xE, 3, 0);
    HSD_LObj* lobj = HSD_LObjLoadDesc(desc);
    HSD_GObjObject_80390A70(gobj, HSD_GObj_LightKind, lobj);
    GObj_SetupGXLink(gobj, HSD_GObj_LObjCallback, 0xA, 0);
    ifAll_804A0FD8.gobj_2 = gobj;
}

static inline void loadScene(SceneDesc** scene)
{
    HSD_Archive** archive = ifAll_GetArchive();
    lbArchive_80016F80(archive, "IfAll");
    lbArchive_LoadSections(*archive, scene, "ScInfDmg_scene_data", NULL);
}

void ifAll_802F390C(void)
{
    SceneDesc* scene;

    ifAll_ShowHUD();
    loadScene(&scene);
    ifAll_802F370C(scene);
    ifAll_804A0FD8.gobj = createCamera(DP(HSD_CObjDesc, DP(struct SceneCameraDesc, scene->cameras)[0].desc));
    createLight(scene);

    ifStatus_802F7134();
    ifStatus_802F66A4();
    ifStock_802FAEC4();
    ifTime_Reset();
    if_802F7E24();
    ifMagnify_802FC870();
    un_802FE260();
    un_802FD704();
    un_802FD4C8();
    un_802FF1B4();
    un_802FF498();
}

void ifAll_802F3A64(void)
{
    struct ifAll_804A0FD8_t* data;

    un_802FE390();
    ifStock_802FB390();
    ifStock_802FB41C();
    ifStock_802FB484();
    ifMagnify_802FC750();
    if_802F7E7C();
    ifTime_FreeTimers();
    ifStatus_802F6804();
    ifStatus_802F7220();

    data = &ifAll_804A0FD8;
    if (data->gobj != NULL) {
        HSD_GObjFree(data->gobj);
    }

    if (data->gobj_2 != NULL) {
        HSD_GObjFree(data->gobj_2);
    }

    un_802FD468();
    un_802FD90C();
    un_802FE390();
    un_802FF190();
    un_802FF4FC();
#ifdef TARGET_PC
    ifNet_Free();
#endif
}
