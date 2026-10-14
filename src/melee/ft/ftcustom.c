#include "ftcustom.h"

#include <stdio.h>
#include <string.h>

#include "ftdata.h"
#include "inlines.h"
#include "types.h"

#include "pc/fighter_pack.h"
#include "pc/pc.h"

#include <melee/lb/lbarchive.h>
#include <melee/pl/player.h>
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
static const FtCustomMoves* const k_moves[] = { &ftGe_Moves };

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

const FtCustomMoves* ftCustom_Moves(Fighter* fp)
{
    const int p = ftCustom_Pack(fp->kind, fp->costume_id);
    return p >= 0 ? s_custom[p].moves : NULL;
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

void ftCustom_Frame(Fighter_GObj* gobj)
{
    const FtCustomMoves* m = ftCustom_Moves(GET_FIGHTER(gobj));
    if (m != NULL && m->on_frame != NULL) {
        m->on_frame(gobj);
    }
}

void ftCustom_Reset(Fighter* fp)
{
    const FtCustomMoves* m = ftCustom_Moves(fp);
    if (m != NULL && m->on_reset != NULL) {
        m->on_reset(fp);
    }
}
