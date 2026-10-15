/* Waluigi's special moves: a custom fighter on Marth (fighters/Wa, "moves
 * Waluigi"). His animations and hitboxes are the pack's (motions.bin); the
 * moves run on the generic custom specials (ftCustom_SpecialEnter, with
 * FtCustomFx), so nothing of Marth's sword techniques shows.
 *
 *   Tennis Serve  (B)       a toss and a swing; the ball (the Star Rod's
 *                           star) flies from his hand
 *   Strikers Slide (side B) a long slide tackle along the ground
 *   Whirluigi     (up B)    spinning up like a top, hitting all the way,
 *                           then a helpless fall
 *   Wah Stomp     (down B)  a hop and a double stomp; the shock wave either
 *                           side buries (in the air he drops on the foe) */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftMars/forward.h>
#include <melee/ft/types.h>

static const FtCustomFx k_serve_fx = {
    FT_CUSTOM_SHOT_STAR, 16, FtPart_RHandN, 0, 0, 0, 0, FtPart_RHandN, 14, 16, 2,
};
static const FtCustomFx k_slide_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_RFootJ, 4, 22, 4, 0,
};
static const FtCustomFx k_whirl_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 2, 30, 6,
};
static const FtCustomFx k_stomp_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_LFootJ, 14, 14, 1, 0, 14, 0,
};

static const FtCustomSpecial k_serve = {
    ftMs_MS_SpecialNStart, ftMs_MS_SpecialAirNStart, false, false, 0, 0, 0.5F, 0, 0, &k_serve_fx,
};
static const FtCustomSpecial k_slide = {
    ftMs_MS_SpecialS1, ftMs_MS_SpecialAirS1, false, false, 2.6F, 0, 0.6F, 0, 0, &k_slide_fx,
};
static const FtCustomSpecial k_whirl = {
    ftMs_MS_SpecialHi, ftMs_MS_SpecialAirHi, true, false, 0.4F, 2.9F, 0.4F, 1.1F, 24.0F, &k_whirl_fx,
};
static const FtCustomSpecial k_stomp = {
    ftMs_MS_SpecialLw, ftMs_MS_SpecialAirLw, false, false, 0, 0, 2.2F, 0, 0, &k_stomp_fx,
};

static void ftWa_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_serve); }
static void ftWa_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_slide); }
static void ftWa_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_whirl); }
static void ftWa_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_stomp); }

const FtCustomMoves ftWa_Moves = {
    "Waluigi",
    ftWa_SpecialN, ftWa_SpecialS, ftWa_SpecialLw, ftWa_SpecialHi,
    ftWa_SpecialN, ftWa_SpecialS, ftWa_SpecialLw, ftWa_SpecialHi,
    NULL, NULL,
};
