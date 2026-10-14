#include "ftcustom.h"

#include <stdio.h>
#include <string.h>

#include "ftdata.h"
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
} s_custom[PC_FIGHTER_PACKS_MAX];
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
