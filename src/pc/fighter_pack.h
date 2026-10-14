/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_FIGHTER_PACK_H
#define PC_FIGHTER_PACK_H

/* Custom fighter packs (src/pc/fighter_pack.c). A pack is a folder
 * fighters/<id>/ holding fighter.cfg, the fighter's costume files
 * (Pl<id><suffix>.dat) and textures/ (HD replacements). It is played as an
 * existing fighter (its base) wearing its own costumes: the game gives the
 * base fighter extra costume numbers (src/melee/ft/ftcustom.c), and every
 * costume file becomes a file on the virtual disc. tools/fighter/ and the
 * make-fighter skill make packs; docs/fighters.md has the layout.
 *
 * fighter.cfg, one "key value" per line, # comments:
 *   id       Ge                  two letters, unused by the disc's Pl*.dat
 *   name     "GENO"              shown in menus
 *   base     Mr                  base fighter's two-letter code
 *   symbol   PlyGeno5K           costume joint symbol stem
 *   costumes Nr Re Gr Bk         costume file suffixes, first is the default
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_FIGHTER_PACKS_MAX 4
#define PC_FIGHTER_COSTUMES_MAX 8

typedef struct PcFighterPack {
    char id[3];
    char base[3];
    char name[32];
    char symbol[48];
    int n_costumes;
    char costume[PC_FIGHTER_COSTUMES_MAX][4];
    char dir[512];
} PcFighterPack;

/* Finds the packs and puts their files on the virtual disc. Once, after the
 * disc is open (pc_platform_init). */
void pc_fighter_packs_init(void);
int pc_fighter_pack_count(void);
const PcFighterPack* pc_fighter_pack(int i);
/* FNV-1a over every pack's files in id order: equal on two machines only
 * when they have the same custom fighters (online). 0 with none. */
uint32_t pc_fighter_packs_hash(void);
/* The packs' textures/ folders, for the texture loader. */
int pc_fighter_pack_texture_dirs(const char** out, int max);

#ifdef __cplusplus
}
#endif
#endif
