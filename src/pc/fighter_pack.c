/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Custom fighter packs: find them, read fighter.cfg, and put their costume
 * files on the virtual disc (aurora's overlay files), so the game loads
 * Pl<id><suffix>.dat exactly as it loads a fighter file from the disc --
 * preload, size and read all go through DVDConvertPathToEntrynum. See
 * fighter_pack.h for the pack layout. */
#include "pc/fighter_pack.h"

#include "pc/pc.h"

#include <SDL3/SDL.h>
#include <aurora/dvd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PcFighterPack s_packs[PC_FIGHTER_PACKS_MAX];
static int s_count;
static uint32_t s_hash;
static char s_texture_dirs[PC_FIGHTER_PACKS_MAX][540];

/* ---- where packs live ---------------------------------------------------- */

/* MELEE_FIGHTERS_DIR, else fighters/ beside the game (shipped with a
 * release), else fighters/ in the user folder. */
static bool fighters_dir(char* out, size_t n) {
    const char* env = getenv("MELEE_FIGHTERS_DIR");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, n, "%s", env);
        return true;
    }
    const char* base = SDL_GetBasePath();
    if (base != NULL) {
        snprintf(out, n, "%sfighters", base);
        SDL_PathInfo info;
        if (SDL_GetPathInfo(out, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            return true;
        }
    }
    char* user = pc_user_dir();
    if (user == NULL) {
        return false;
    }
    snprintf(out, n, "%sfighters", user);
    SDL_free(user);
    return true;
}

/* ---- fighter.cfg ------------------------------------------------------- */

/* Next whitespace-separated token, honouring "double quotes"; empty at the
 * end of the line or at a # comment. */
static const char* token(const char* p, char* out, size_t n) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    size_t k = 0;
    if (*p == '#') {
        p += strlen(p);
    } else if (*p == '"') {
        p++;
        while (*p != '\0' && *p != '"' && *p != '\n') {
            if (k + 1 < n) {
                out[k++] = *p;
            }
            p++;
        }
        if (*p == '"') {
            p++;
        }
    } else {
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            if (k + 1 < n) {
                out[k++] = *p;
            }
            p++;
        }
    }
    out[k] = '\0';
    return p;
}

static bool read_cfg(const char* dir, PcFighterPack* pk) {
    char path[600];
    snprintf(path, sizeof path, "%s/fighter.cfg", dir);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    memset(pk, 0, sizeof *pk);
    snprintf(pk->dir, sizeof pk->dir, "%s", dir);
    pk->weight = pk->walk = pk->dash = pk->jump = pk->gravity = pk->fall = pk->air = pk->size = 1.0f;
    pk->order = 100;
    snprintf(pk->team[0], sizeof pk->team[0], "Re");
    snprintf(pk->team[1], sizeof pk->team[1], "Bu");
    snprintf(pk->team[2], sizeof pk->team[2], "Gr");
    char line[512];
    while (fgets(line, sizeof line, f) != NULL) {
        char key[32], val[64];
        const char* p = token(line, key, sizeof key);
        if (key[0] == '\0' || key[0] == '#') {
            continue;
        }
        if (strcmp(key, "teams") == 0) {
            for (int t = 0; t < 3; t++) {
                p = token(p, val, sizeof val);
                snprintf(pk->team[t], sizeof pk->team[t], "%s", val);
            }
            continue;
        }
        if (strcmp(key, "costumes") == 0) {
            while (pk->n_costumes < PC_FIGHTER_COSTUMES_MAX) {
                p = token(p, val, sizeof val);
                if (val[0] == '\0') {
                    break;
                }
                snprintf(pk->costume[pk->n_costumes++], 4, "%s", val);
            }
            continue;
        }
        token(p, val, sizeof val);
        if (strcmp(key, "id") == 0) {
            snprintf(pk->id, sizeof pk->id, "%s", val);
        } else if (strcmp(key, "base") == 0) {
            snprintf(pk->base, sizeof pk->base, "%s", val);
        } else if (strcmp(key, "name") == 0) {
            snprintf(pk->name, sizeof pk->name, "%s", val);
        } else if (strcmp(key, "symbol") == 0) {
            snprintf(pk->symbol, sizeof pk->symbol, "%s", val);
        } else if (strcmp(key, "moves") == 0) {
            snprintf(pk->moves, sizeof pk->moves, "%s", val);
        } else if (strcmp(key, "order") == 0) {
            pk->order = atoi(val);
        } else if (strcmp(key, "voice") == 0) {
            pk->mute = strcmp(val, "none") == 0;
        } else {
            /* Attribute multipliers. strtod in the C locale: the same bits on
             * every machine (they are simulation). */
            static const struct {
                const char* key;
                size_t at;
            } k_attrs[] = {
                {"weight", offsetof(PcFighterPack, weight)},
                {"walk", offsetof(PcFighterPack, walk)},
                {"dash", offsetof(PcFighterPack, dash)},
                {"jump", offsetof(PcFighterPack, jump)},
                {"gravity", offsetof(PcFighterPack, gravity)},
                {"fall", offsetof(PcFighterPack, fall)},
                {"air", offsetof(PcFighterPack, air)},
                {"size", offsetof(PcFighterPack, size)},
            };
            for (size_t i = 0; i < sizeof k_attrs / sizeof k_attrs[0]; i++) {
                if (strcmp(key, k_attrs[i].key) == 0) {
                    float v = (float)strtod(val, NULL);
                    *(float*)((char*)pk + k_attrs[i].at) = v > 0.1f && v < 10.0f ? v : 1.0f;
                }
            }
        }
    }
    fclose(f);
    if (strlen(pk->id) != 2 || strlen(pk->base) != 2 || pk->symbol[0] == '\0' ||
        pk->n_costumes == 0)
    {
        pc_log_line("fighters: %s/fighter.cfg needs id, base, symbol and costumes", dir);
        return false;
    }
    if (pk->name[0] == '\0') {
        snprintf(pk->name, sizeof pk->name, "%s", pk->id);
    }
    return true;
}

/* ---- virtual disc files ---------------------------------------------- */

static void* ov_open(void* userdata) {
    return fopen((const char*)userdata, "rb");
}

static void ov_close(void* handle) {
    fclose((FILE*)handle);
}

static int64_t ov_read(void* handle, uint8_t* buf, size_t len) {
    size_t n = fread(buf, 1, len, (FILE*)handle);
    return ferror((FILE*)handle) ? -1 : (int64_t)n;
}

static int64_t ov_seek(void* handle, int64_t offset, int32_t whence) {
    if (fseek((FILE*)handle, (long)offset, whence) != 0) {
        return -1;
    }
    return (int64_t)ftell((FILE*)handle);
}

static uint32_t fnv(uint32_t h, const void* p, size_t n) {
    const uint8_t* b = p;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

/* Adds the pack's costume files and its menu images (Pl<id>Ui.dat) to
 * `files`; hashes them as it goes. */
static void hash_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    uint8_t buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
        s_hash = fnv(s_hash, buf, got);
    }
    fclose(f);
}

/* The pack's motions.bin, whole, into memory (the motion table points into
 * it while matches play), hashed. */
static void load_motions(PcFighterPack* pk) {
    char path[600];
    snprintf(path, sizeof path, "%s/motions.bin", pk->dir);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = size > 0 ? malloc((size_t)size) : NULL;
    if (buf != NULL && fread(buf, 1, (size_t)size, f) == (size_t)size) {
        pk->motions = buf;
        pk->motions_size = (uint32_t)size;
        s_hash = fnv(s_hash, "motions.bin", 11);
        s_hash = fnv(s_hash, buf, (size_t)size);
    } else {
        free(buf);
    }
    fclose(f);
}

static int add_files(PcFighterPack* pk, AuroraOverlayFile* files, char (*names)[32],
    char (*paths)[600], int n) {
    char cfg[600];
    snprintf(cfg, sizeof cfg, "%s/fighter.cfg", pk->dir);
    hash_file(cfg);
    load_motions(pk);
    for (int c = 0; c <= pk->n_costumes; c++) {
        const char* sfx = c < pk->n_costumes ? pk->costume[c] : "Ui";
        snprintf(names[n], 32, "/Pl%s%s.dat", pk->id, sfx);
        snprintf(paths[n], 600, "%s/Pl%s%s.dat", pk->dir, pk->id, sfx);
        FILE* f = fopen(paths[n], "rb");
        if (f == NULL) {
            pc_log_line("fighters: %s is missing %s", pk->id, names[n] + 1);
            continue;
        }
        s_hash = fnv(s_hash, names[n], strlen(names[n]));
        uint8_t buf[65536];
        size_t got, size = 0;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
            s_hash = fnv(s_hash, buf, got);
            size += got;
        }
        fclose(f);
        files[n] = (AuroraOverlayFile){names[n], paths[n], size};
        n++;
    }
    return n;
}

/* Pack order: fighter.cfg's "order", then the id. */
static int by_order(const void* a, const void* b) {
    const PcFighterPack* x = (const PcFighterPack*)a;
    const PcFighterPack* y = (const PcFighterPack*)b;
    if (x->order != y->order) {
        return x->order < y->order ? -1 : 1;
    }
    return strcmp(x->id, y->id);
}

void pc_fighter_packs_init(void) {
    static bool done;
    if (done) {
        return;
    }
    done = true;
    char root[512];
    if (fighters_dir(root, sizeof root)) {
        pc_fighter_packs_load(root);
    }
}

void pc_fighter_packs_load(const char* root) {
    for (int i = 0; i < s_count; i++) {
        free(s_packs[i].motions);
        s_packs[i].motions = NULL;
    }
    s_count = 0;
    s_hash = 0;
    int n_entries = 0;
    char** entries = SDL_GlobDirectory(root, NULL, 0, &n_entries);
    for (int i = 0; entries != NULL && i < n_entries && s_count < PC_FIGHTER_PACKS_MAX; i++) {
        char dir[600];
        snprintf(dir, sizeof dir, "%s/%s", root, entries[i]);
        if (entries[i][0] != '.' && read_cfg(dir, &s_packs[s_count])) {
            s_count++;
        }
    }
    SDL_free(entries);
    if (s_count == 0) {
        return;
    }
    qsort(s_packs, (size_t)s_count, sizeof s_packs[0], by_order);
    static AuroraOverlayFile files[PC_FIGHTER_PACKS_MAX * (PC_FIGHTER_COSTUMES_MAX + 1)];
    static char names[PC_FIGHTER_PACKS_MAX * (PC_FIGHTER_COSTUMES_MAX + 1)][32];
    static char paths[PC_FIGHTER_PACKS_MAX * (PC_FIGHTER_COSTUMES_MAX + 1)][600];
    int n = 0;
    s_hash = 2166136261u;
    for (int i = 0; i < s_count; i++) {
        n = add_files(&s_packs[i], files, names, paths, n);
        snprintf(s_texture_dirs[i], sizeof s_texture_dirs[i], "%s/textures", s_packs[i].dir);
        pc_log_line("fighters: %s \"%s\" on %s, %d costume(s)", s_packs[i].id, s_packs[i].name,
            s_packs[i].base, s_packs[i].n_costumes);
    }
    static const AuroraOverlayCallbacks cb = {ov_open, ov_close, ov_read, ov_seek};
    aurora_dvd_overlay_callbacks(&cb);
    aurora_dvd_overlay_files(files, (size_t)n, NULL);
    pc_log_line("fighters: %d pack(s) from %s, content %08x", s_count, root, s_hash);
}

int pc_fighter_pack_count(void) {
    return s_count;
}

const PcFighterPack* pc_fighter_pack(int i) {
    return i >= 0 && i < s_count ? &s_packs[i] : NULL;
}

uint32_t pc_fighter_packs_hash(void) {
    return s_count > 0 ? s_hash : 0;
}

int pc_fighter_pack_texture_dirs(const char** out, int max) {
    int n = 0;
    for (int i = 0; i < s_count && n < max; i++) {
        out[n++] = s_texture_dirs[i];
    }
    return n;
}
