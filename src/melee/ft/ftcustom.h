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

/* Gives each pack's base fighter the pack's costumes. Once at boot, after
 * pc_fighter_packs_init. */
void ftCustom_Init(void);
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

#endif
