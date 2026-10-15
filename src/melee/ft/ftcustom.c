#include "ftcustom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftdata.h"
#include "fighter.h"
#include "ftparts.h"
#include "inlines.h"
#include "types.h"

#include "ft_081B.h"
#include "ft_084E.h"
#include "ft_0892.h"
#include "ftanim.h"
#include "ftcommon.h"
#include "kinds/ftCommon/ftCo_Fall.h"
#include "kinds/ftCommon/ftCo_FallSpecial.h"
#include "kinds/ftCommon/ftCo_Landing.h"
#include "kinds/ftCommon/inlines.h"

#include "pc/fighter_pack.h"
#include "pc/pc.h"

#include <melee/ef/efsync.h>
#include <melee/ft/ftcolanim.h>
#include <melee/it/kinds/itlgunray.h>
#include <melee/it/kinds/itstarrodstar.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_0219.h>
#include <melee/lb/lbarchive.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/archive.h>

/* What ftCustom_Init made for each pack: the base's costume list and
 * costume-string table extended by the pack's costumes. */
/* ftdata.c: each kind's data file and symbol ("PlMr.dat", "ftDataMario"),
 * and its costume file names. */
struct StringPair {
    char* a;
    char* b;
};
extern struct StringPair ftData_803C1F40[Ft_Kind_Max];
extern Fighter_CostumeStrings* ftData_803C2360[Ft_Kind_Max];

static struct {
    FighterKind base;
    int first; /* the pack's first costume number on the base */
    int count;
    const FtCustomMoves* moves;
} s_custom[PC_FIGHTER_PACKS_MAX];

/* Every custom fighter's compiled moves, by the name fighter.cfg uses. */
extern const FtCustomMoves ftGe_Moves;
extern const FtCustomMoves ftSh_Moves;
extern const FtCustomMoves ftWa_Moves;
extern const FtCustomMoves ftTh_Moves;
extern const FtCustomMoves ftFr_Moves;
extern const FtCustomMoves ftOp_Moves;
extern const FtCustomMoves ftYo_Moves;
extern const FtCustomMoves ftKr_Moves;
extern const FtCustomMoves ftKg_Moves;
extern const FtCustomMoves ftCr_Moves;
static const FtCustomMoves* const k_moves[] = {
    &ftGe_Moves, &ftSh_Moves, &ftWa_Moves, &ftTh_Moves, &ftFr_Moves,
    &ftOp_Moves, &ftYo_Moves, &ftKr_Moves, &ftKg_Moves, &ftCr_Moves,
};

/* The base fighter's own special entries, for the fighters not wearing a
 * custom costume with moves: the tables below are shared by all of them. */
enum { SP_N, SP_S, SP_LW, SP_HI, SP_AIR_N, SP_AIR_S, SP_AIR_LW, SP_AIR_HI, SP_COUNT };
static HSD_GObjEvent s_base_special[SP_COUNT][Ft_Kind_Max];

static void special(Fighter_GObj* gobj, int which)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomMoves* m = ftCustom_Moves(fp);
    void (*own)(Fighter_GObj*) = NULL;
    if (m != NULL) {
        switch (which) {
        case SP_N: own = m->special_n; break;
        case SP_S: own = m->special_s; break;
        case SP_LW: own = m->special_lw; break;
        case SP_HI: own = m->special_hi; break;
        case SP_AIR_N: own = m->special_air_n; break;
        case SP_AIR_S: own = m->special_air_s; break;
        case SP_AIR_LW: own = m->special_air_lw; break;
        case SP_AIR_HI: own = m->special_air_hi; break;
        }
    }
    if (own != NULL) {
        own(gobj);
    } else if (s_base_special[which][fp->kind] != NULL) {
        s_base_special[which][fp->kind](gobj);
    }
}

static void special_n(Fighter_GObj* g) { special(g, SP_N); }
static void special_s(Fighter_GObj* g) { special(g, SP_S); }
static void special_lw(Fighter_GObj* g) { special(g, SP_LW); }
static void special_hi(Fighter_GObj* g) { special(g, SP_HI); }
static void special_air_n(Fighter_GObj* g) { special(g, SP_AIR_N); }
static void special_air_s(Fighter_GObj* g) { special(g, SP_AIR_S); }
static void special_air_lw(Fighter_GObj* g) { special(g, SP_AIR_LW); }
static void special_air_hi(Fighter_GObj* g) { special(g, SP_AIR_HI); }

/* Route a base fighter's special entries through special(), once. */
static void take_specials(FighterKind k)
{
    HSD_GObjEvent* const tables[SP_COUNT] = {
        ftData_SpecialN, ftData_SpecialS, ftData_SpecialLw, ftData_SpecialHi,
        ftData_SpecialAirN, ftData_SpecialAirS, ftData_SpecialAirLw, ftData_SpecialAirHi,
    };
    static void (*const shims[SP_COUNT])(Fighter_GObj*) = {
        special_n, special_s, special_lw, special_hi,
        special_air_n, special_air_s, special_air_lw, special_air_hi,
    };
    int i;
    if (tables[0][k] == (HSD_GObjEvent) special_n) {
        return;
    }
    for (i = 0; i < SP_COUNT; i++) {
        s_base_special[i][k] = tables[i][k];
        if (tables[i][k] != NULL) {
            tables[i][k] = (HSD_GObjEvent) shims[i];
        }
    }
}
static int s_n_custom;
static int s_retail[Ft_Kind_Max];

#define MAX_COSTUMES 16

static UnkCostumeStruct s_costume_list[Ft_Kind_Max][MAX_COSTUMES];
static Fighter_CostumeStrings s_costume_strings[Ft_Kind_Max][MAX_COSTUMES];
static char s_names[PC_FIGHTER_PACKS_MAX][PC_FIGHTER_COSTUMES_MAX][3][64];

static FighterKind kind_of_code(const char* code)
{
    int k;
    for (k = 0; k < Ft_Kind_Max; k++) {
        const char* dat = ftData_803C1F40[k].a; /* "PlMr.dat" */
        if (dat != NULL && strlen(dat) >= 4 && dat[2] == code[0] && dat[3] == code[1]) {
            return k;
        }
    }
    return Ft_Kind_None;
}

void ftCustom_Init(void)
{
    static bool done;
    int k, p;
    if (done) {
        return;
    }
    done = true;
    for (k = 0; k < Ft_Kind_Max; k++) {
        s_retail[k] = CostumeListsForeachCharacter[k].numCostumes;
    }
    for (p = 0; p < pc_fighter_pack_count(); p++) {
        const PcFighterPack* pk = pc_fighter_pack(p);
        FighterKind base = kind_of_code(pk->base);
        int have, c;
        if (base == Ft_Kind_None) {
            pc_log_line("fighters: %s: no fighter %s to base it on", pk->id, pk->base);
            continue;
        }
        have = CostumeListsForeachCharacter[base].numCostumes;
        if (have + pk->n_costumes > MAX_COSTUMES) {
            pc_log_line("fighters: %s: too many costumes on %s", pk->id, pk->base);
            continue;
        }
        /* Move the base's tables to ours on its first pack. */
        if (CostumeListsForeachCharacter[base].costume_list != s_costume_list[base]) {
            memcpy(s_costume_list[base], CostumeListsForeachCharacter[base].costume_list,
                   sizeof(UnkCostumeStruct) * have);
            memcpy(s_costume_strings[base], ftData_803C2360[base],
                   sizeof(Fighter_CostumeStrings) * have);
            CostumeListsForeachCharacter[base].costume_list = s_costume_list[base];
            ftData_803C2360[base] = s_costume_strings[base];
        }
        for (c = 0; c < pk->n_costumes; c++) {
            const char* sfx = strcmp(pk->costume[c], "Nr") == 0 ? "" : pk->costume[c];
            char(*n)[64] = s_names[s_n_custom][c];
            snprintf(n[0], 64, "Pl%s%s.dat", pk->id, pk->costume[c]);
            snprintf(n[1], 64, "%s%s_Share_joint", pk->symbol, sfx);
            snprintf(n[2], 64, "%s%s_Share_matanim_joint", pk->symbol, sfx);
            memset(&s_costume_list[base][have + c], 0, sizeof(UnkCostumeStruct));
            s_costume_strings[base][have + c].dat_filename = n[0];
            s_costume_strings[base][have + c].joint_name = n[1];
            s_costume_strings[base][have + c].matanim_joint_name = n[2];
        }
        CostumeListsForeachCharacter[base].numCostumes = (u8) (have + pk->n_costumes);
        s_custom[s_n_custom].base = base;
        s_custom[s_n_custom].first = have;
        s_custom[s_n_custom].count = pk->n_costumes;
        s_custom[s_n_custom].moves = NULL;
        for (c = 0; pk->moves[0] != '\0' && c < (int) (sizeof k_moves / sizeof k_moves[0]); c++) {
            if (strcmp(k_moves[c]->name, pk->moves) == 0) {
                s_custom[s_n_custom].moves = k_moves[c];
                take_specials(base);
            }
        }
        if (pk->moves[0] != '\0' && s_custom[s_n_custom].moves == NULL) {
            pc_log_line("fighters: %s: no moves called %s in this build", pk->id, pk->moves);
        }
        pc_log_line("fighters: %s is %s costumes %d-%d", pk->id, pk->base, have,
                    have + pk->n_costumes - 1);
        s_n_custom++;
    }
}

int ftCustom_RetailCostumes(FighterKind kind)
{
    if (kind < 0 || kind >= Ft_Kind_Max) {
        return 0;
    }
    return s_retail[kind] != 0 ? s_retail[kind]
                               : CostumeListsForeachCharacter[kind].numCostumes;
}

int ftCustom_Pack(FighterKind kind, int costume)
{
    int p;
    for (p = 0; p < s_n_custom; p++) {
        if (s_custom[p].base == kind && costume >= s_custom[p].first &&
            costume < s_custom[p].first + s_custom[p].count)
        {
            return p;
        }
    }
    return -1;
}

int ftCustom_DataCostume(FighterKind kind, int costume)
{
    return ftCustom_Pack(kind, costume) >= 0 ? 0 : costume;
}

int ftCustom_FirstCostume(int pack)
{
    return pack >= 0 && pack < s_n_custom ? s_custom[pack].first : -1;
}

int ftCustom_CostumeCount(int pack)
{
    return pack >= 0 && pack < s_n_custom ? s_custom[pack].count : 0;
}

FighterKind ftCustom_BaseKind(int pack)
{
    return pack >= 0 && pack < s_n_custom ? s_custom[pack].base : Ft_Kind_None;
}

int ftCustom_PackById(const char* id)
{
    int i;
    for (i = 0; i < s_n_custom; i++) {
        if (strncmp(pc_fighter_pack(i)->id, id, 2) == 0) {
            return i;
        }
    }
    return -1;
}

int ftCustom_BaseCharacter(int pack)
{
    const FighterKind kind = ftCustom_BaseKind(pack);
    int c;
    if (kind == Ft_Kind_None) {
        return -1;
    }
    for (c = 0; c < CKind_Playable_Count; c++) {
        if (Player_800325C8((CharacterKind) c, false) == kind) {
            return c;
        }
    }
    return -1;
}

static HSD_Archive* s_ui[PC_FIGHTER_PACKS_MAX];
static bool s_ui_tried[PC_FIGHTER_PACKS_MAX];

void ftCustom_UiReset(void)
{
    memset(s_ui, 0, sizeof s_ui);
    memset(s_ui_tried, 0, sizeof s_ui_tried);
}

struct HSD_ImageDesc* ftCustom_UiImage(int pack, const char* symbol)
{
    if (pack < 0 || pack >= s_n_custom) {
        return NULL;
    }
    if (!s_ui_tried[pack]) {
        char name[24];
        void* first = NULL;
        s_ui_tried[pack] = true;
        snprintf(name, sizeof name, "Pl%sUi.dat", pc_fighter_pack(pack)->id);
        s_ui[pack] = lbArchive_80016DBC(name, &first, "ftUiIcon", NULL);
    }
    return s_ui[pack] != NULL ? HSD_ArchiveGetPublicAddress(s_ui[pack], symbol) : NULL;
}

struct HSD_ImageDesc* ftCustom_StockImage(int slot)
{
    const int ckind = Player_GetPlayerCharacter(slot);
    const int costume = Player_GetCostumeId(slot);
    FighterKind kind;
    int pack;
    char sym[16];
    if (ckind < 0 || ckind >= CKind_Playable_Count) {
        return NULL;
    }
    kind = Player_800325C8(ckind, false);
    pack = ftCustom_Pack(kind, costume);
    if (pack < 0) {
        return NULL;
    }
    snprintf(sym, sizeof sym, "ftUiStock%d", costume - s_custom[pack].first);
    return ftCustom_UiImage(pack, sym);
}

struct HSD_ImageDesc* ftCustom_SlotImage(int slot, const char* symbol)
{
    const int ckind = Player_GetPlayerCharacter(slot);
    if (ckind < 0 || ckind >= CKind_Playable_Count) {
        return NULL;
    }
    {
        const int pack = ftCustom_Pack(Player_800325C8(ckind, false), Player_GetCostumeId(slot));
        return pack >= 0 ? ftCustom_UiImage(pack, symbol) : NULL;
    }
}

const char* ftCustom_SlotName(int slot)
{
    const int ckind = Player_GetPlayerCharacter(slot);
    int pack;
    if (ckind < 0 || ckind >= CKind_Playable_Count) {
        return NULL;
    }
    pack = ftCustom_Pack(Player_800325C8(ckind, false), Player_GetCostumeId(slot));
    return pack >= 0 ? pc_fighter_pack(pack)->name : NULL;
}

bool ftCustom_Mute(Fighter* fp)
{
    const int p = ftCustom_Pack(fp->kind, fp->costume_id);
    return p >= 0 && pc_fighter_pack(p)->mute;
}

const FtCustomMoves* ftCustom_Moves(Fighter* fp)
{
    const int p = ftCustom_Pack(fp->kind, fp->costume_id);
    return p >= 0 ? s_custom[p].moves : NULL;
}

/* motions.bin (tools/fighter/build_moves.py), big-endian: "M4MV", version
 * 1, the row count, then per row the offsets (from the file's start) of the
 * base motion's name ("Attack11": every motion-table row whose animation is
 * PlyXxx_Share_ACTION_Attack11_figatree), of the animation archive and its
 * size, of the action script (0: the base's), and the row's flags
 * (0xFFFFFFFF: the base's). */
static u32 be32(const u8* p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

#define MOTION_ROWS_MAX 512
static struct Fighter_WaitAnimData s_motions[8][MOTION_ROWS_MAX];

void ftCustom_Motions(Fighter* fp)
{
    const int p = ftCustom_Pack(fp->kind, fp->costume_id);
    const PcFighterPack* pk;
    struct Fighter_WaitAnimData* rows;
    u32 n, i, k, count;
    if (p < 0 || (pk = pc_fighter_pack(p))->motions == NULL || fp->player_idx >= 8 ||
        fp->x58C > MOTION_ROWS_MAX || pk->motions_size < 12 ||
        memcmp(pk->motions, "M4MV", 4) != 0 || be32(pk->motions + 4) != 1)
    {
        return;
    }
    n = fp->x58C;
    rows = s_motions[fp->player_idx];
    memcpy(rows, fp->x24, n * sizeof(*rows));
    count = be32(pk->motions + 8);
    for (k = 0; k < count && 12 + (k + 1) * 24 <= pk->motions_size; k++) {
        const u8* e = pk->motions + 12 + k * 24;
        const char* name = (const char*) pk->motions + be32(e);
        const u32 anim = be32(e + 4), size = be32(e + 8), script = be32(e + 12), flags = be32(e + 20);
        const size_t len = strlen(name);
        for (i = 0; i < n; i++) {
            const char* sym = DP(char, rows[i].x0);
            const char* at = sym != NULL ? strstr(sym, "_ACTION_") : NULL;
            if (at == NULL || strncmp(at + 8, name, len) != 0 || strcmp(at + 8 + len, "_figatree") != 0) {
                continue;
            }
            if (size != 0) {
                /* The archive's one symbol is the row's name. */
                rows[i].x4 = 0;
                rows[i].x8 = size;
                rows[i].x14 = pc_encode_dp(pk->motions + anim);
            }
            if (script != 0) {
                DP_SET(rows[i].xC, pk->motions + script);
            }
            if (flags != 0xFFFFFFFFu) {
                rows[i].x10_animCurrFlags = flags;
            }
        }
    }
    fp->x24 = rows;
}

/* ---- generic special moves (ftcustom.h FtCustomSpecial) ---------------- */

static const FtCustomSpecial* s_special[8];

static const FtCustomSpecial* special_of(Fighter* fp)
{
    return fp->player_idx < 8 ? s_special[fp->player_idx] : NULL;
}

static void special_install(Fighter_GObj* gobj);

static void special_anim(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomSpecial* d = special_of(fp);
    if (ftAnim_IsFramesRemaining(gobj)) {
        return;
    }
    s_special[fp->player_idx] = NULL;
    if (fp->ground_or_air == GA_Ground) {
        ft_8008A2BC(gobj);
    } else if (d != NULL && d->helpless) {
        ftCo_80096900(gobj, 0, 1, 0, d->mobility, d->landing_lag);
    } else {
        ftCo_Fall_Enter(gobj);
    }
}

static void special_phys(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomSpecial* d = special_of(fp);
    if (fp->ground_or_air == GA_Ground) {
        if (d != NULL && d->root_motion) {
            ft_80084FA8(gobj);
        } else {
            ft_80084F3C(gobj);
        }
    } else if (d != NULL && d->root_motion) {
        ft_80085154(gobj);
    } else if (d == NULL || d->gravity == 1.0F) {
        ft_80084DB0(gobj);
    } else {
        ftCommon_Fall(fp, fp->co_attrs.gravity * d->gravity, fp->co_attrs.terminal_velocity);
        ftCommon_CalcSelfAccel_DeaccelQuickAir(fp);
    }
}

static void special_landing(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomSpecial* d = special_of(fp);
    s_special[fp->player_idx] = NULL;
    ftCo_LandingFallSpecial_Enter(gobj, false, d != NULL ? d->landing_lag : 10.0F);
}

#define SPECIAL_MF (Ft_MF_SkipColAnim | Ft_MF_UpdateCmd)

static void special_coll(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomSpecial* d = special_of(fp);
    if (d == NULL) {
        return;
    }
    if (fp->ground_or_air == GA_Ground) {
        if (!ft_80082708(gobj)) {
            ftCommon_GroundToAirStateChange(gobj, fp, d->msid_air, SPECIAL_MF);
            special_install(gobj);
        }
    } else if (d->helpless) {
        if (fp->self_vel.y >= 0.0F) {
            ft_80083B68(gobj);
        } else {
            ft_800831CC(gobj, &ftCo_80096CC8, &special_landing);
        }
    } else if (ft_80081D0C(gobj)) {
        ftCommon_AirToGroundStateChange(gobj, fp, d->msid_ground, SPECIAL_MF);
        special_install(gobj);
    }
}

static void special_install(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->anim_cb = special_anim;
    fp->input_cb = NULL;
    fp->phys_cb = special_phys;
    fp->coll_cb = special_coll;
}

void ftCustom_SpecialEnter(Fighter_GObj* gobj, const FtCustomSpecial* d)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const bool air = fp->ground_or_air == GA_Air || d->vel_y > 0.0F;
    if (fp->player_idx >= 8) {
        return;
    }
    fp->cmd_vars[0] = 0;
    fp->throw_flags = 0;
    if (air && fp->ground_or_air == GA_Ground) {
        ftCommon_8007D60C(fp);
    }
    Fighter_ChangeMotionState(gobj, air ? d->msid_air : d->msid_ground, Ft_MF_None, 0, 1, 0, NULL);
    ftAnim_8006EBA4(gobj);
    s_special[fp->player_idx] = d;
    special_install(gobj);
    if (d->vel_x != 0.0F || d->vel_y != 0.0F) {
        fp->self_vel.x = d->vel_x * fp->facing_dir;
        fp->self_vel.y = d->vel_y;
        if (!air) {
            fp->gr_vel = d->vel_x * fp->facing_dir; /* a slide: friction slows it */
        }
    }
}

const FtCustomSpecial* ftCustom_SpecialActive(Fighter* fp)
{
    const FtCustomSpecial* d = special_of(fp);
    if (d == NULL) {
        return NULL;
    }
    /* Left by anything else (hit, grabbed...): not in the move. */
    if (fp->motion_id != d->msid_ground && fp->motion_id != d->msid_air) {
        s_special[fp->player_idx] = NULL;
        return NULL;
    }
    return d;
}

bool ftCustom_SpecialEvent(Fighter* fp)
{
    if (ftCustom_SpecialActive(fp) == NULL || !fp->throw_flags_b0) {
        return false;
    }
    fp->throw_flags_b0 = false;
    return true;
}

void ftCustom_ApplyAttrs(Fighter* fp)
{
    const int p = ftCustom_Pack(fp->kind, fp->costume_id);
    const PcFighterPack* pk;
    if (p < 0) {
        return;
    }
    pk = pc_fighter_pack(p);
    fp->co_attrs.weight *= pk->weight;
    fp->co_attrs.walk_max_vel *= pk->walk;
    fp->co_attrs.dash_max_velocity *= pk->dash;
    fp->co_attrs.jump_v_initial_velocity *= pk->jump;
    fp->co_attrs.hop_v_initial_velocity *= pk->jump;
    fp->co_attrs.gravity *= pk->gravity;
    fp->co_attrs.terminal_velocity *= pk->fall;
    fp->co_attrs.fast_fall_velocity *= pk->fall;
    fp->co_attrs.air_drift_max *= pk->air;
}

/* MELEE_DUMP_POSE=path:frame[,frame...] (a test knob): on those frames of
 * the first custom fighter, write every joint's world matrix (3x4, rows) as
 * JSON, for tools/fighter/blender_pose.py to pose the model like the game.
 * With several frames the path holds a %d for the frame number. */
static void dump_pose(Fighter* fp)
{
    static int frame = -1, n_at = -1, at[32];
    static char path[512];
    char name[600];
    FILE* f;
    int i, n, hit = 0;
    if (n_at < 0) {
        const char* env = getenv("MELEE_DUMP_POSE");
        const char* colon = env != NULL ? strrchr(env, ':') : NULL;
        n_at = 0;
        if (colon != NULL && (size_t) (colon - env) < sizeof(path)) {
            const char* c = colon + 1;
            memcpy(path, env, (size_t) (colon - env));
            path[colon - env] = '\0';
            while (*c != '\0' && n_at < (int) ARRAY_SIZE(at)) {
                at[n_at++] = atoi(c);
                while (*c != '\0' && *c != ',') {
                    c++;
                }
                if (*c == ',') {
                    c++;
                }
            }
        }
    }
    ++frame;
    for (i = 0; i < n_at; i++) {
        hit |= at[i] == frame;
    }
    if (!hit) {
        return;
    }
    snprintf(name, sizeof(name), path, frame);
    if ((f = fopen(name, "w")) == NULL) {
        return;
    }
    n = DP(FighterPartsTable, ftPartsTable[fp->kind].v)->parts_num;
    fprintf(f, "{\"motion\": %d, \"frame\": %f, \"joints\": [", fp->motion_id, fp->cur_anim_frame);
    for (i = 0; i < n; i++) {
        HSD_JObj* j = fp->parts[i].joint;
        int r, c;
        fprintf(f, "%s[", i ? ", " : "");
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 4; c++) {
                fprintf(f, "%s%.6f", r || c ? ", " : "", j != NULL ? j->mtx[r][c] : 0.0F);
            }
        }
        fprintf(f, "]");
    }
    fprintf(f, "]}\n");
    fclose(f);
}

/* FtCustomFx: what the special does on this frame. The armour is set and
 * cleared here only for fighters whose moves asked for it. */
static bool s_fx_armor[8], s_fx_vanish[8];

static bool fx_part_pos(Fighter* fp, int part, Vec3* pos)
{
    FighterBone* bone = ftParts_GetBone(fp, (Fighter_Part) part);
    if (bone == NULL || bone->joint == NULL) {
        return false;
    }
    lb_8000B1CC(bone->joint, NULL, pos);
    return true;
}

static void fx_frame(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomSpecial* d = fp->player_idx < 8 ? ftCustom_SpecialActive(fp) : NULL;
    const FtCustomFx* fx = d != NULL ? d->fx : NULL;
    const int f = (int) fp->cur_anim_frame;
    Vec3 pos;
    if (fp->player_idx >= 8) {
        return;
    }
    if (s_fx_armor[fp->player_idx] && (fx == NULL || fx->armor <= 0.0F)) {
        fp->dmg.armor0 = 0.0F;
        s_fx_armor[fp->player_idx] = false;
    }
    if (s_fx_vanish[fp->player_idx] && (fx == NULL || fx->vanish_end <= fx->vanish_start)) {
        fp->invisible = false;
        s_fx_vanish[fp->player_idx] = false;
    }
    if (fx == NULL) {
        return;
    }
    if (fx->armor > 0.0F) {
        fp->dmg.armor0 = fx->armor;
        s_fx_armor[fp->player_idx] = true;
    }
    if (fx->shot != FT_CUSTOM_SHOT_NONE && f == fx->shot_frame && fx_part_pos(fp, fx->shot_part, &pos)) {
        if (fx->shot == FT_CUSTOM_SHOT_RAY) {
            it_80298168(gobj, &pos, fp->facing_dir);
        } else {
            it_802988E4(gobj, &pos, 1, fp->facing_dir);
        }
    }
    if (fx->vanish_end > fx->vanish_start) {
        fp->invisible = f >= fx->vanish_start && f < fx->vanish_end;
        s_fx_vanish[fp->player_idx] = true;
        if (fp->invisible) {
            if (fp->ground_or_air == GA_Ground) {
                fp->gr_vel = fx->vanish_vx * fp->facing_dir;
            } else {
                fp->self_vel.x = fx->vanish_vx * fp->facing_dir;
                fp->self_vel.y = fx->vanish_vy;
            }
        }
    }
    if (fx->spark_every > 0 && f >= fx->spark_from && f <= fx->spark_to &&
        (f - fx->spark_from) % fx->spark_every == 0 && fx_part_pos(fp, fx->spark_part, &pos))
    {
        efSync_Spawn(fx->spark_ef != 0 ? fx->spark_ef : Ef_Id_Unk1001, NULL, &pos);
    }
    if (fx->flash_frame > 0 && f == fx->flash_frame) {
        lbBgFlash_80021C48(4, 0);
        if (fx->flash_colanim > 0) {
            ftCo_800BFFD0(fp, fx->flash_colanim, false);
        }
    }
}

void ftCustom_Frame(Fighter_GObj* gobj)
{
    const FtCustomMoves* m = ftCustom_Moves(GET_FIGHTER(gobj));
    if (m != NULL) {
        fx_frame(gobj);
    }
    if (ftCustom_Pack(GET_FIGHTER(gobj)->kind, GET_FIGHTER(gobj)->costume_id) >= 0) {
        dump_pose(GET_FIGHTER(gobj));
    }
    if (m != NULL && m->on_frame != NULL) {
        m->on_frame(gobj);
    }
}

void ftCustom_Reset(Fighter* fp)
{
    if (fp->player_idx < 8 && (s_fx_armor[fp->player_idx] || s_fx_vanish[fp->player_idx])) {
        fp->dmg.armor0 = 0.0F;
        fp->invisible = false;
        s_fx_armor[fp->player_idx] = s_fx_vanish[fp->player_idx] = false;
    }
    const FtCustomMoves* m = ftCustom_Moves(fp);
    if (m != NULL && m->on_reset != NULL) {
        m->on_reset(fp);
    }
}
