/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "textures.h"
#include "fighter_pack.h"
#include "pc.h"

#include <dolphin/gx.h>
#include <aurora/texture.hpp>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

/*
 * Texture directory layout.
 *
 *   textures/packs/<NN-name>/   installed packs; the lower NN, the higher the
 *                               priority (Setup installs 10-community above
 *                               90-ai-gapfill, so hand-made art wins).
 *   textures/stocks/<style>/    optional stock-icon overlays; the one named by
 *                               the stock_style preference goes above all.
 *   textures/<anything else>    a plain Dolphin pack dropped in loose, used
 *                               as-is when there is no packs/ folder.
 *
 * Aurora scans a directory recursively, so in the structured layout each pack
 * is loaded on its own (never the root, which would pull every pack and
 * every stock style in at one priority).
 */

namespace {

std::vector<aurora::texture::ReplacementGroup> s_groups;
std::filesystem::path s_textures_dir;
std::string s_textures_dir_str;
bool s_initialized = false;

constexpr int32_t kStockPriority = 1000000;

std::filesystem::path resolve_texture_directory() {
    // 0. MELEE_TEXTURE_DIR wins: lets a pack be tested without installing it,
    //    and gives consoles (no SDL pref path) a fixed location.
    if (const char* env = SDL_getenv("MELEE_TEXTURE_DIR"); env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }

    // 1. The user data dir (pc_user_dir: MELEE_USER_DIR, else SDL's pref path).
    //    Use a "textures" subdirectory there: <prefPath>/textures
    std::filesystem::path user_dir;
    char* pref = pc_user_dir();
    if (pref != nullptr) {
        user_dir = std::filesystem::path(pref) / "textures";
        SDL_free(pref);
    }

    // 2. Create the user textures directory if it does not exist.
    std::error_code ec;
    if (!user_dir.empty() && !std::filesystem::exists(user_dir, ec)) {
        std::filesystem::create_directories(user_dir, ec);
    }

    // 3. Also check if a local "textures" folder exists beside the executable.
    const char* base = SDL_GetBasePath();
    std::filesystem::path local_dir;
    if (base != nullptr) {
        local_dir = std::filesystem::path(base) / "textures";
    } else {
        local_dir = std::filesystem::path("textures");
    }

    if (std::filesystem::exists(local_dir, ec) && std::filesystem::is_directory(local_dir, ec)) {
        return local_dir;
    }

    return user_dir;
}

std::vector<std::filesystem::path> sorted_subdirectories(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_directory(ec) && entry.path().filename().string().rfind('.', 0) != 0) {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

size_t load_group(const std::filesystem::path& dir, int32_t priority) {
    s_groups.push_back(aurora::texture::load_replacement_directory(dir, {.priority = priority}));
    const size_t n = s_groups.back().registrations.size();
    SDL_Log("Custom textures: %zu replacement(s) from %s (priority %d)", n, dir.string().c_str(),
        static_cast<int>(priority));
    return n;
}

void unload_all() {
    for (auto& group : s_groups) {
        aurora::texture::unregister_replacements(group);
    }
    s_groups.clear();
}

void load_all() {
    s_textures_dir = resolve_texture_directory();
    s_textures_dir_str = s_textures_dir.string();
    if (s_textures_dir.empty()) {
        return;
    }

    std::error_code ec;
    const auto packs = s_textures_dir / "packs";
    if (!std::filesystem::is_directory(packs, ec)) {
        load_group(s_textures_dir, 0);
        return;
    }

    const auto dirs = sorted_subdirectories(packs);
    for (size_t i = 0; i < dirs.size(); i++) {
        // First in sort order = highest priority.
        load_group(dirs[i], static_cast<int32_t>((dirs.size() - i) * 100));
    }

    const char* style = pc_get_stock_style();
    if (style != nullptr && std::string(style) != "default") {
        const auto stocks = s_textures_dir / "stocks" / style;
        if (std::filesystem::is_directory(stocks, ec)) {
            load_group(stocks, kStockPriority);
        } else {
            SDL_Log("Custom textures: stock style '%s' is not installed", style);
        }
    }
}

/* Custom fighters' own HD textures (fighter_pack.h). They are part of the
 * fighter rather than a texture pack, so they load with Custom Textures off
 * too, above every pack. */
void load_fighters() {
    const char* dirs[PC_FIGHTER_PACKS_MAX];
    const int n = pc_fighter_pack_texture_dirs(dirs, PC_FIGHTER_PACKS_MAX);
    std::error_code ec;
    for (int i = 0; i < n; i++) {
        if (std::filesystem::is_directory(dirs[i], ec)) {
            load_group(dirs[i], kStockPriority * 2);
        }
    }
}

}  // namespace

extern "C" void pc_textures_init(void) {
    if (s_initialized) {
        pc_textures_reload();
        return;
    }
    s_initialized = true;

    if (pc_is_custom_textures_enabled()) {
        load_all();
    }
    load_fighters();
}

extern "C" void pc_textures_reload(void) {
    unload_all();
    if (pc_is_custom_textures_enabled()) {
        load_all();
    }
    load_fighters();
}

extern "C" void pc_textures_shutdown(void) {
    unload_all();
    s_initialized = false;
}

extern "C" const char* pc_textures_get_path(void) {
    return s_textures_dir_str.c_str();
}

extern "C" int pc_textures_stock_styles(char names[][64], int max) {
    std::error_code ec;
    const auto dir = resolve_texture_directory() / "stocks";
    int n = 0;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& path : sorted_subdirectories(dir)) {
            if (n == max) {
                break;
            }
            SDL_strlcpy(names[n++], path.filename().string().c_str(), 64);
        }
    }
    return n;
}
