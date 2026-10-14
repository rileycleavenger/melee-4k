/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Custom fighter packs (src/pc/fighter_pack.c): fighter.cfg parsing and the
 * content hash online play compares.
 *
 *   ninja -C build unit_tests && ctest --test-dir build -R fighter_pack
 *
 * The hash must not depend on the order the directory lists packs in (two
 * machines' filesystems differ), must change with any byte of a pack, and
 * must be 0 with no packs. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pc/fighter_pack.h"

#include <aurora/dvd.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#define mkdir_(p) mkdir(p, 0755)
#endif

/* The game's logger and user folder, not linked here. */
void pc_log_line(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

char* pc_user_dir(void) {
    return NULL;
}

/* aurora's virtual disc: the files fighter_pack.c puts on it. */
static size_t s_overlay_files;
void aurora_dvd_overlay_callbacks(const AuroraOverlayCallbacks* callbacks) {
    (void)callbacks;
}
void aurora_dvd_overlay_files(const AuroraOverlayFile* files, size_t nFiles, s32* outEntryNums) {
    (void)files;
    (void)outEntryNums;
    s_overlay_files = nFiles;
}

static void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    assert(f != NULL);
    fputs(text, f);
    fclose(f);
}

/* A pack folder root/dir with fighter.cfg `cfg` and every costume file named
 * in it, plus Pl<id>Ui.dat, each holding `content`. */
static void make_pack(const char* root, const char* dir, const char* id, const char* cfg,
    const char* const* costumes, const char* content) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, dir);
    mkdir_(path);
    snprintf(path, sizeof path, "%s/%s/fighter.cfg", root, dir);
    write_file(path, cfg);
    for (int i = 0; costumes[i] != NULL; i++) {
        snprintf(path, sizeof path, "%s/%s/Pl%s%s.dat", root, dir, id, costumes[i]);
        write_file(path, content);
    }
    snprintf(path, sizeof path, "%s/%s/Pl%sUi.dat", root, dir, id);
    write_file(path, content);
}

static const char* const k_costumes_a[] = {"Nr", "Re", NULL};
static const char* const k_costumes_b[] = {"Nr", NULL};

static const char k_cfg_a[] = "# a test fighter\n"
                              "id Aa\n"
                              "name \"ALPHA ONE\"   # quoted, with a space\n"
                              "base Mr\n"
                              "symbol PlyAlpha5K\n"
                              "costumes Nr Re   # two\n"
                              "teams Re Nr Gr\n"
                              "moves Geno\n"
                              "weight 0.9\n"
                              "jump 1.25\n"
                              "gravity 50\n";
static const char k_cfg_b[] = "id Zz\nbase Fx\nsymbol PlyZed5K\ncostumes Nr\n";

static uint32_t load(const char* root) {
    s_overlay_files = 0;
    pc_fighter_packs_load(root);
    return pc_fighter_packs_hash();
}

int main(void) {
    /* Two roots with the same packs; in the second the folders list in the
     * other order (folder names sort opposite to the ids). */
    const char* r1 = "fighter_pack_test_1";
    const char* r2 = "fighter_pack_test_2";
    const char* r3 = "fighter_pack_test_3";
    mkdir_(r1);
    mkdir_(r2);
    mkdir_(r3);
    make_pack(r1, "a", "Aa", k_cfg_a, k_costumes_a, "costume bytes");
    make_pack(r1, "b", "Zz", k_cfg_b, k_costumes_b, "other bytes");
    make_pack(r2, "z", "Aa", k_cfg_a, k_costumes_a, "costume bytes");
    make_pack(r2, "y", "Zz", k_cfg_b, k_costumes_b, "other bytes");

    const uint32_t h1 = load(r1);
    assert(pc_fighter_pack_count() == 2);
    const PcFighterPack* a = pc_fighter_pack(0);
    assert(strcmp(a->id, "Aa") == 0); /* sorted by id */
    assert(strcmp(a->name, "ALPHA ONE") == 0);
    assert(strcmp(a->base, "Mr") == 0);
    assert(strcmp(a->symbol, "PlyAlpha5K") == 0);
    assert(a->n_costumes == 2); /* the trailing comment is not a costume */
    assert(strcmp(a->costume[0], "Nr") == 0 && strcmp(a->costume[1], "Re") == 0);
    assert(strcmp(a->team[0], "Re") == 0 && strcmp(a->team[1], "Nr") == 0 &&
           strcmp(a->team[2], "Gr") == 0);
    assert(strcmp(a->moves, "Geno") == 0);
    assert(a->weight == 0.9f && a->jump == 1.25f);
    assert(a->gravity == 1.0f); /* out of range: ignored */
    assert(a->walk == 1.0f && a->fall == 1.0f && a->air == 1.0f);
    const PcFighterPack* b = pc_fighter_pack(1);
    assert(strcmp(b->id, "Zz") == 0);
    assert(strcmp(b->name, "Zz") == 0); /* no name: the id */
    assert(strcmp(b->team[0], "Re") == 0 && strcmp(b->team[1], "Bu") == 0 &&
           strcmp(b->team[2], "Gr") == 0); /* default teams */
    assert(h1 != 0);
    assert(s_overlay_files == 5); /* Aa: Nr, Re, Ui; Zz: Nr, Ui */

    assert(load(r2) == h1); /* listing order does not matter */

    make_pack(r2, "y", "Zz", k_cfg_b, k_costumes_b, "other byteS");
    assert(load(r2) != h1); /* one byte of a costume file */
    make_pack(r2, "y", "Zz", "id Zz\nbase Fx\nsymbol PlyZed5K\ncostumes Nr\nweight 1.1\n",
        k_costumes_b, "other bytes");
    assert(load(r2) != h1); /* fighter.cfg (attributes are simulation) */

    assert(load(r3) == 0); /* no packs */
    assert(pc_fighter_pack_count() == 0);

    /* A pack missing a required key is skipped. */
    make_pack(r3, "bad", "Bb", "id Bb\ncostumes Nr\n", k_costumes_b, "x");
    load(r3);
    assert(pc_fighter_pack_count() == 0);

    puts("fighter_pack: ok");
    return 0;
}
