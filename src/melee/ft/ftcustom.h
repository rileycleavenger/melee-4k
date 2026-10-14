#ifndef MELEE_FT_FTCUSTOM_H
#define MELEE_FT_FTCUSTOM_H

#include <Runtime/platform.h>

#include "melee/ft/forward.h"

/* Custom fighters (PC). A custom fighter is an existing fighter -- its base
 * -- wearing costumes past the base's own: Geno is Mario with costume 5 and
 * up. The fighter kind stays the base's, so every table and every check the
 * game makes on that kind applies unchanged; only the costume files differ
 * (src/pc/fighter_pack.c puts them on the virtual disc). Menus and screens
 * tell the two apart with ftCustom_Pack. */

/* Gives each pack's base fighter the pack's costumes, and its special moves
 * (FtCustomMoves) to the fighters wearing them. Once at boot, after
 * pc_fighter_packs_init. */
void ftCustom_Init(void);

/* A custom fighter's own special moves, compiled into the game like any
 * fighter's code (src/melee/ft/kinds/<name>/) and named by a pack's
 * fighter.cfg ("moves Geno"). Each is entered in place of the base
 * fighter's; NULL keeps the base's. on_frame runs every frame
 * (Fighter_procCollPos: after the frame's motion changes, before the
 * accessory callbacks they install) and on_reset at every (re)spawn, for
 * state the moves keep. */
typedef struct FtCustomMoves {
    const char* name;
    void (*special_n)(Fighter_GObj*);
    void (*special_s)(Fighter_GObj*);
    void (*special_lw)(Fighter_GObj*);
    void (*special_hi)(Fighter_GObj*);
    void (*special_air_n)(Fighter_GObj*);
    void (*special_air_s)(Fighter_GObj*);
    void (*special_air_lw)(Fighter_GObj*);
    void (*special_air_hi)(Fighter_GObj*);
    void (*on_frame)(Fighter_GObj*);
    void (*on_reset)(Fighter*);
} FtCustomMoves;

/* The custom fighter's moves, or NULL (a retail fighter, or no "moves"). */
const FtCustomMoves* ftCustom_Moves(Fighter* fp);
/* fighter.cfg's attribute multipliers, on the attributes just copied from
 * the data file (ftchangeparam.c). */
void ftCustom_ApplyAttrs(Fighter* fp);
void ftCustom_Frame(Fighter_GObj* gobj);
void ftCustom_Reset(Fighter* fp);
/* How many costumes the base fighter has on the disc. */
int ftCustom_RetailCostumes(FighterKind kind);
/* The pack a (kind, costume) belongs to, or -1 for a retail costume. */
int ftCustom_Pack(FighterKind kind, int costume);
/* The costume to read the base fighter's on-disc per-costume tables with:
 * a custom costume uses the base's first. */
int ftCustom_DataCostume(FighterKind kind, int costume);
/* The first costume of pack `pack`, and how many it has. */
int ftCustom_FirstCostume(int pack);
int ftCustom_CostumeCount(int pack);
FighterKind ftCustom_BaseKind(int pack);
/* The pack with two-letter id `id`, or -1; and the character select
 * character its base fighter is (CKind_*), or -1. */
int ftCustom_PackById(const char* id);
int ftCustom_BaseCharacter(int pack);

/* Menu and HUD images from the pack's Pl<id>Ui.dat (build_ui.py), loaded
 * into the current scene's heap on first use. ftCustom_UiReset forgets them
 * (a new scene's heap); call it where a screen is built. */
void ftCustom_UiReset(void);
struct HSD_ImageDesc* ftCustom_UiImage(int pack, const char* symbol);
/* The stock icon of the fighter in player slot `slot`, or NULL for a
 * retail one. */
struct HSD_ImageDesc* ftCustom_StockImage(int slot);
/* Image `symbol` of the custom fighter in player slot `slot`, or NULL. */
struct HSD_ImageDesc* ftCustom_SlotImage(int slot, const char* symbol);
/* The name of the custom fighter in player slot `slot`, or NULL. */
const char* ftCustom_SlotName(int slot);

#endif
