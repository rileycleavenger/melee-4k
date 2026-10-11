#ifndef MELEE_MN_MNM4K_H
#define MELEE_MN_MNM4K_H

#include "forward.h"

#include <sysdolphin/baselib/forward.h>

/* PC only: Options > Melee 4K (MENU_KIND_M4K), the port's settings in
 * Melee's own menu. Like mnonline.h, its entries have no label textures, so
 * mnmain.c draws the text these return. The settings themselves live in
 * src/pc/launcher.cpp (pc_m4k_*), shared with the F1 overlay. */

/* 22D104-style think proc, see mn_803EB6B0[MENU_KIND_M4K]. */
void mnM4K_Think(HSD_GObj*);

/* Label / bottom-bar text for the Melee 4K entry and page; NULL = not ours.
 * A label shows the setting's current value, so it changes as it is set. */
const char* mnM4K_Label(MenuKind, int selection);
const char* mnM4K_Description(MenuKind, int selection);

#endif
