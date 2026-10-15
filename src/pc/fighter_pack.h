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
 *   teams    Re Nr Gr            the red, blue and green team costumes
 *                                (default Re Bu Gr; one missing: the
 *                                player's own costume)
 *   moves    Geno                special moves compiled into the game
 *                                (ftcustom.h); absent: the base's
 *   weight   0.9                 multipliers on the base's attributes,
 *   walk     1.1                 one per line: weight, walk, dash, jump,
 *   ...                          gravity, fall, air
 *
 * motions.bin (optional, tools/fighter/build_moves.py): the fighter's own
 * animations and action scripts, replacing the base's motion by motion
 * (ftCustom_Motions).
 *
 * fighter.cfg and motions.bin are simulation (moves, attributes, hitboxes):
 * online they are part of the content hash, with every costume file.
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
    char team[3][4]; /* red, blue, green team costume suffixes */
    char moves[24];
    bool mute; /* "voice none": none of the base fighter's voice clips */
    /* Multipliers on the base fighter's attributes, 1 when not given. */
    float weight, walk, dash, jump, gravity, fall, air;
    uint8_t* motions; /* motions.bin, or NULL */
    uint32_t motions_size;
    char dir[512];
} PcFighterPack;

/* Finds the packs and puts their files on the virtual disc. Once, after the
 * disc is open (pc_platform_init). */
void pc_fighter_packs_init(void);
/* The packs in `root`, replacing any loaded: pc_fighter_packs_init's work,
 * callable again (tools/test_fighter_pack.c). */
void pc_fighter_packs_load(const char* root);
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
