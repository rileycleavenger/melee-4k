/* King Boo's special moves: a custom fighter on Jigglypuff (fighters/Kg,
 * "moves KingBoo"). His animations and hitboxes are the pack's
 * (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): no rollout, song or rest.
 *
 *   Dark Orb      (B)       a ball of dark energy (the Star Rod's star)
 *   Boo Dash      (side B)  invisible across the ground, then a lash out of
 *                           nowhere
 *   Cackling Rise (up B)    spinning up, crown first; a helpless fall
 *   Crown Slam    (down B)  up, then crashing down: darkness either side */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftPurin/forward.h>
#include <melee/ft/types.h>

#define KG_SCARE_COLANIM 15

static const FtCustomFx k_orb_fx = {
    FT_CUSTOM_SHOT_STAR, 16, FtPart_HipN,
};
static const FtCustomFx k_dash_fx = {
    0, 0, 0, 4, 16, 2.4F, 0.0F, FtPart_HipN, 16, 16, 1, 0, 18, KG_SCARE_COLANIM,
};
static const FtCustomFx k_rise_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 3, 28, 6,
};
static const FtCustomFx k_slam_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 14, 14, 1, 0, 14, 0,
};

static const FtCustomSpecial k_orb = {
    ftPr_MS_SpecialNStartR, ftPr_MS_SpecialAirNStartR, false, false, 0, 0, 0.4F, 0, 0, &k_orb_fx,
};
static const FtCustomSpecial k_dash = {
    ftPr_MS_SpecialS, ftPr_MS_SpecialAirS, false, false, 0, 0, 0.0F, 0, 0, &k_dash_fx,
};
static const FtCustomSpecial k_rise = {
    ftPr_MS_SpecialHiR, ftPr_MS_SpecialAirHiR, true, false, 0.3F, 2.8F, 0.25F, 1.2F, 18.0F, &k_rise_fx,
};
static const FtCustomSpecial k_slam = {
    ftPr_MS_SpecialLwR, ftPr_MS_SpecialAirLwR, false, false, 0, 1.6F, 2.0F, 0, 0, &k_slam_fx,
};

static void ftKg_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_orb); }
static void ftKg_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_dash); }
static void ftKg_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_rise); }
static void ftKg_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_slam); }

const FtCustomMoves ftKg_Moves = {
    "KingBoo",
    ftKg_SpecialN, ftKg_SpecialS, ftKg_SpecialLw, ftKg_SpecialHi,
    ftKg_SpecialN, ftKg_SpecialS, ftKg_SpecialLw, ftKg_SpecialHi,
    NULL, NULL,
};
