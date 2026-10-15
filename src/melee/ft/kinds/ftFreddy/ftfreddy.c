/* Freddy Fazbear's special moves: a custom fighter on Ganondorf
 * (fighters/Fr, "moves Freddy"). His animations and hitboxes are the
 * pack's (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): nothing of Ganondorf's dark
 * fire or grabs.
 *
 *   Mic Toss        (B)       the microphone thrown spinning (the Star
 *                             Rod's star)
 *   Jumpscare       (side B)  gone while unseen, then lunging in your face
 *   Lights Out      (up B)    vanishes and reappears high above, claws out;
 *                             a helpless fall
 *   Toreador March  (down B)  the lights go out, the music box plays:
 *                             anyone close falls asleep */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftCaptain/forward.h>
#include <melee/ft/types.h>

#define FR_SCARE_COLANIM 15

static const FtCustomFx k_mic_fx = {
    FT_CUSTOM_SHOT_STAR, 18, FtPart_RHandN,
};
static const FtCustomFx k_scare_fx = {
    0, 0, 0, 6, 20, 2.4F, 0.0F, FtPart_HeadN, 20, 20, 1, 0, 20, FR_SCARE_COLANIM,
};
static const FtCustomFx k_lights_fx = {
    0, 0, 0, 6, 22, 0.3F, 2.6F, FtPart_HeadN, 22, 22, 1, 0, 6, 0,
};
static const FtCustomFx k_march_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HeadN, 10, 58, 12, 0, 8, 0,
};

static const FtCustomSpecial k_mic = {
    ftCa_MS_SpecialN, ftCa_MS_SpecialAirN, false, false, 0, 0, 0.6F, 0, 0, &k_mic_fx,
};
static const FtCustomSpecial k_scare = {
    ftCa_MS_SpecialSStart, ftCa_MS_SpecialAirSStart, false, false, 0, 0, 0.4F, 0, 0, &k_scare_fx,
};
static const FtCustomSpecial k_lights = {
    ftCa_MS_SpecialHi, ftCa_MS_SpecialAirHi, true, false, 0, 1.0F, 0.5F, 0.9F, 30.0F, &k_lights_fx,
};
static const FtCustomSpecial k_march = {
    ftCa_MS_SpecialLw, ftCa_MS_SpecialAirLw, false, false, 0, 0, 0.3F, 0, 0, &k_march_fx,
};

static void ftFr_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_mic); }
static void ftFr_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_scare); }
static void ftFr_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_lights); }
static void ftFr_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_march); }

const FtCustomMoves ftFr_Moves = {
    "Freddy",
    ftFr_SpecialN, ftFr_SpecialS, ftFr_SpecialLw, ftFr_SpecialHi,
    ftFr_SpecialN, ftFr_SpecialS, ftFr_SpecialLw, ftFr_SpecialHi,
    NULL, NULL,
};
