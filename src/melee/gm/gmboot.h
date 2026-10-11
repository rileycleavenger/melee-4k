#ifndef MELEE_GM_1BF9_H
#define MELEE_GM_1BF9_H

#ifdef TARGET_PC
#include <dolphin/types.h>

/// MELEE_BOOT_SCENE=<name|mode:N> (gmboot.c lists the names): the
/// GameModeKind to boot straight into, or GM_COUNT when unset.
u8 pc_boot_scene(void);
const char* pc_boot_scene_name(void);
#endif

#endif
