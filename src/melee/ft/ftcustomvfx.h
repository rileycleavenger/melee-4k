#ifndef MELEE_FT_FTCUSTOMVFX_H
#define MELEE_FT_FTCUSTOMVFX_H

/* A custom fighter's own visual effects: beams, glows, rings, trails,
 * particle streams and glowing blades, drawn from its pack's vfx.cfg
 * (docs/fighters.md). Presentation only: drawn from the fighter's state
 * (its animation and frame, its joints) and never read back by the
 * simulation, so they cannot desync a match. */

#include <melee/ft/forward.h>
#include <sysdolphin/baselib/forward.h>
#include <stdbool.h>

/* The fighter's effects for this render pass (ftDrawCommon_800805C8). */
void ftCustomVfx_Draw(Fighter_GObj* gobj, int pass);
/* A custom fighter's projectile (the Ray Gun or Star Rod shot its specials
 * fire) in its own look (vfx.cfg "shot"): true when drawn here, and the
 * item's own model is then left out (it_8026EECC). */
bool ftCustomVfx_DrawItem(HSD_GObj* gobj, int pass);
/* Forget every pack's parsed vfx.cfg (packs reloaded). */
void ftCustomVfx_Reset(void);

#endif
