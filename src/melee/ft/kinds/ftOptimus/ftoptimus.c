/* Optimus Prime's special moves: a custom fighter on Samus (fighters/Op,
 * "moves Optimus"). His animations and hitboxes are the pack's
 * (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): nothing of Samus's charge
 * shot, missiles, screw attack or bombs.
 *
 *   Ion Blaster          (B)       a bolt from his right hand
 *   Roll Out             (side B)  the truck-mode ram along the ground
 *   Rising Axe           (up B)    a leap, the Energon Axe swung up through
 *                                  it; a helpless fall
 *   Matrix of Leadership (down B)  the chest blazes: a burst of light all
 *                                  around, armoured through it */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftSamus/forward.h>
#include <melee/ft/types.h>

#define OP_MATRIX_COLANIM 53 /* the glow of a charged shot */

static const FtCustomFx k_blaster_fx = {
    FT_CUSTOM_SHOT_RAY, 14, FtPart_RHandN, 0, 0, 0, 0, FtPart_RHandN, 14, 14, 1,
};
static const FtCustomFx k_roll_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 6, 30, 4, 0,
};
static const FtCustomFx k_axe_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_RHandN, 6, 24, 3,
};
static const FtCustomFx k_matrix_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_BustN, 12, 30, 3, 0, 18, OP_MATRIX_COLANIM, 30.0F,
};

static const FtCustomSpecial k_blaster = {
    ftSs_MS_SpecialN, ftSs_MS_SpecialAirN, false, false, 0, 0, 0.5F, 0, 0, &k_blaster_fx,
};
static const FtCustomSpecial k_roll = {
    ftSs_MS_SpecialS, ftSs_MS_SpecialAirS, false, false, 2.8F, 0, 0.3F, 0, 0, &k_roll_fx,
};
static const FtCustomSpecial k_axe = {
    ftSs_MS_SpecialHi, ftSs_MS_SpecialAirHi, true, false, 0.6F, 3.2F, 0.4F, 1.0F, 24.0F, &k_axe_fx,
};
static const FtCustomSpecial k_matrix = {
    ftSs_MS_SpecialLw, ftSs_MS_SpecialAirLw, false, false, 0, 0, 0.2F, 0, 0, &k_matrix_fx,
};

static void ftOp_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_blaster); }
static void ftOp_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_roll); }
static void ftOp_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_axe); }
static void ftOp_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_matrix); }

const FtCustomMoves ftOp_Moves = {
    "Optimus",
    ftOp_SpecialN, ftOp_SpecialS, ftOp_SpecialLw, ftOp_SpecialHi,
    ftOp_SpecialN, ftOp_SpecialS, ftOp_SpecialLw, ftOp_SpecialHi,
    NULL, NULL,
};
