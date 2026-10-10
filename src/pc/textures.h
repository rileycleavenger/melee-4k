/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_TEXTURES_H
#define PC_TEXTURES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize texture replacements if custom textures are enabled. The
 * textures directory is MELEE_TEXTURE_DIR, else a textures/ folder beside
 * the executable, else <prefPath>/textures (created if missing). Installed
 * packs live in packs/<NN-name>/ (lower NN wins) with an optional stock-icon
 * overlay from stocks/<stock_style>/; a loose Dolphin pack is loaded as-is.
 * See src/pc/textures.cpp. */
void pc_textures_init(void);

/* Reload texture replacements from disk. */
void pc_textures_reload(void);

/* Unregister and unload active texture replacements. */
void pc_textures_shutdown(void);

/* Returns the resolved textures directory path (or empty string if none). */
const char* pc_textures_get_path(void);
/* Fills names with the installed stock-icon styles (stocks/ subfolders, sorted)
 * and returns how many; at most max. */
int pc_textures_stock_styles(char names[][64], int max);

#ifdef __cplusplus
}
#endif

#endif /* PC_TEXTURES_H */
