#include "gmboot.h"

#include "gm_unsplit.h"
#include "gmmain_lib.h"
#include "types.h"
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lblanguage.h>
#include <melee/ty/toy.h>
#ifdef TARGET_PC
#include "gmonlinemode.h"
#include <stdlib.h>
#include <string.h>
#endif

/* 1BF948 */ static void bootOnLoad(GameModeState*);
/* 1BF9A8 */ static void bootOnLeave(GameModeState*);
/* 1BFA3C */ static void memcardOnLoad(GameModeState*);

/// @todo Move to toy header
enum {
    TROPHY_PIKMIN = 275,
};

struct loadData {
    u32 x0;
    u8 x4;
    u8 mode_id; ///< Copied to ::leaveData::mode_id to set next mode
};

struct leaveData {
    u32 x0;
    u8 mode_id;
};

static struct loadData load_data;
static struct leaveData leave_data;

#ifdef TARGET_PC
/* MELEE_BOOT_SCENE=<name|mode:N>: skip the whole menu walk and boot into one
 * mode. Menu navigation here can only be driven by synthetic input, which
 * misses keypresses often enough that an automated run cannot rely on it
 * (see tools/smoke_test.py).
 *
 * vs maps to GM_DEBUG_VS because that mode already *is* a fixed direct start
 * (onEnterDebugVs in gmvsmode.c fills the StartMeleeData itself). classic and
 * training open on a character-select state, so their on_load hooks seed the
 * pick and jump past it. Every other mode starts on its own first screen
 * (usually its character select), which is what screen and texture sweeps
 * need; tools/soak.py picks a character through MELEE_KEY_FIFO. vs-sss seeds
 * a two-player VS and opens on its stage select (gmvsmelee.c). */
static const struct {
    const char* name;
    u8 mode;
} boot_scenes[] = {
    {"title", GM_TITLE},
    {"menu", GM_MENU},
    {"vs", GM_DEBUG_VS},
    {"vs-css", GM_VS},
    {"vs-sss", GM_VS},
    {"classic", GM_CLASSIC},
    {"adventure", GM_ADVENTURE},
    {"allstar", GM_ALLSTAR},
    {"training", GM_TRAINING},
    {"event", GM_EVENT},
    {"target", GM_TARGET_TEST},
    {"homerun", GM_HOME_RUN_CONTEST},
    {"multiman10", GM_10MAN_VS},
    {"multiman100", GM_100MAN_VS},
    {"multiman3min", GM_3MIN_VS},
    {"multiman15min", GM_15MIN_VS},
    {"endless", GM_ENDLESS_VS},
    {"cruel", GM_CRUEL_VS},
    {"tournament", GM_TOURNAMENT},
    {"trophies", GM_TOY_GALLERY},
    {"lottery", GM_TOY_LOTTERY},
    {"collection", GM_TOY_COLLECTION},
    {"camera", GM_CAMERA_MODE},
    {"special-camera", GM_CAMERA_VS},
    {"special-stamina", GM_STAMINA_VS},
    {"special-super-sudden-death", GM_SUPER_SUDDEN_DEATH_VS},
    {"special-giant", GM_GIANT_VS},
    {"special-tiny", GM_TINY_VS},
    {"special-invisible", GM_INVISIBLE_VS},
    {"special-single-button", GM_SINGLE_BUTTON_VS},
    {"special-lightning", GM_LIGHTNING_VS},
    {"special-slomo", GM_SLOMO_VS},
    {"opening", GM_OPENING_MV},
    {"host", GM_ONLINE},
    {"join", GM_ONLINE},
    {"unranked", GM_ONLINE},
    {"direct", GM_ONLINE},
    {"ranked", GM_ONLINE},
};

static const char* boot_scene_name;

/* The MELEE_BOOT_SCENE name, for modes with more than one entry point. */
const char* pc_boot_scene_name(void)
{
    pc_boot_scene();
    return boot_scene_name != NULL ? boot_scene_name : "";
}

u8 pc_boot_scene(void)
{
    static int done;
    static u8 scene = GM_COUNT;

    if (!done) {
        const char* want = getenv("MELEE_BOOT_SCENE");
        size_t i;
        done = 1;
        boot_scene_name = want;
        if (want == NULL || want[0] == '\0') {
            return scene;
        }
        if (strncmp(want, "mode:", 5) == 0) {
            int mode = atoi(want + 5);
            if (mode >= 0 && mode < GM_COUNT) {
                scene = (u8) mode;
            }
        }
        for (i = 0; i < sizeof boot_scenes / sizeof boot_scenes[0]; i++) {
            if (strcmp(want, boot_scenes[i].name) == 0) {
                scene = boot_scenes[i].mode;
            }
        }
        if (strcmp(want, "host") == 0 || strcmp(want, "join") == 0) {
            gmOnline_SetKind(ONLINE_KIND_DIRECT);
            gmOnline_SetDirectRole(want[0] == 'h' ? ONLINE_DIRECT_HOST : ONLINE_DIRECT_JOIN);
        } else if (strcmp(want, "unranked") == 0) {
            gmOnline_SetKind(ONLINE_KIND_UNRANKED);
        } else if (strcmp(want, "direct") == 0) {
            gmOnline_SetKind(ONLINE_KIND_DIRECT);
        } else if (strcmp(want, "ranked") == 0) {
            gmOnline_SetKind(ONLINE_KIND_RANKED);
        }
        if (scene == GM_COUNT) {
            OSReport("MELEE_BOOT_SCENE: unknown scene '%s'; use mode:<0-%d> or one of:",
                     want, GM_COUNT - 1);
            for (i = 0; i < sizeof boot_scenes / sizeof boot_scenes[0]; i++) {
                OSReport(" %s", boot_scenes[i].name);
            }
            OSReport("\n");
        }
    }
    return scene;
}
#endif

GameModeState gm_Mode_Boot_States[] = {
    {
        0,
        1,
        0,
        bootOnLoad,
        bootOnLeave,
        GS_MEMCARD,
        &load_data,
        &leave_data,
    },
    {
        -1,
    },
};

void bootOnLoad(GameModeState* scene)
{
    struct loadData* scene_data = gm_GetGameModeStateEnterData(scene);
    scene_data->x4 = 0;
    scene_data->x0 = 0;
    if (gmMainLib_8046B0F0.skip_intro == true) {
        scene_data->mode_id = GM_TITLE;
    } else {
        gm_801BF708(0);
        scene_data->mode_id = GM_OPENING_MV;
    }
}

void bootOnLeave(GameModeState* data)
{
    struct leaveData* scene_data = gm_GetGameModeStateExitData(data);

    if (!Toy_803048C0(TROPHY_PIKMIN)) {
        if (!lb_8001C2D8(0, "01",
                         lbLang_GetLanguageSetting() == LANG_JP ? "GPIJ"
                                                                : "GPIE",
                         "Pikmin dataFile"))
        {
            Toy_803124BC();
            Toy_SetUnlockState(TROPHY_PIKMIN, true);
        }
    }

    gm_SetGameModeOverride(lbCardGame_DecideGameMode);

    // Enter mode
    // Gekko "boot to CSS" code changes scene_id to a hardcoded 2 (::GM_VS)
    gm_ChangeGameModeAfterCurrentScene(scene_data->mode_id);
}

GameModeState gm_Mode_MemCard_States[] = {
    {
        0,
        3,
        0,
        memcardOnLoad,
        NULL,
        {
            GS_MEMCARD,
            &load_data,
            &leave_data,
        },
    },
    { -1 },
};

void memcardOnLoad(GameModeState* scene)
{
    struct loadData* temp_r3 = gm_GetGameModeStateEnterData(scene);
    temp_r3->x4 = 0;
    temp_r3->x0 = 1;
}
