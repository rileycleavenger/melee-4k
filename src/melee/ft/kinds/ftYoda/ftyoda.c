/* Yoda's special moves: a custom fighter on Young Link (fighters/Yo, "moves
 * Yoda"). His animations and hitboxes are the pack's (motions.bin); the
 * moves run on the generic custom specials (ftCustom_SpecialEnter, with
 * FtCustomFx): no bow, boomerang or bombs.
 *
 *   Force Push     (B)       the open hand: a wave that pushes all in front
 *                            away, hardly hurting
 *   Ataru Leap     (side B)  a forward somersault, cutting all the way
 *   Force Jump     (up B)    a huge spinning leap; a helpless fall
 *   Force Barrier  (down B)  braced and armoured, then the gathered Force let
 *                            go in a burst all around */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftLink/forward.h>
#include <melee/ft/types.h>

#define YO_FORCE_COLANIM 53

static const FtCustomFx k_push_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_RHandN, 12, 18, 3, 0, 12, 0,
};
static const FtCustomFx k_ataru_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_LHandN, 4, 24, 5,
};
static const FtCustomFx k_jump_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_LHandN, 3, 28, 5,
};
static const FtCustomFx k_barrier_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_RHandN, 8, 30, 4, 0, 33, YO_FORCE_COLANIM, 25.0F,
};

static const FtCustomSpecial k_push = {
    ftLk_MS_SpecialNStart, ftLk_MS_SpecialAirNStart, false, false, 0, 0, 0.4F, 0, 0, &k_push_fx,
};
static const FtCustomSpecial k_ataru = {
    ftLk_MS_SpecialS1, ftLk_MS_SpecialAirS1, false, false, 2.2F, 1.4F, 0.6F, 0, 0, &k_ataru_fx,
};
static const FtCustomSpecial k_jump = {
    ftLk_MS_SpecialHi, ftLk_MS_SpecialAirHi, true, false, 0.7F, 3.9F, 0.45F, 1.2F, 20.0F, &k_jump_fx,
};
static const FtCustomSpecial k_barrier = {
    ftLk_MS_SpecialLw, ftLk_MS_SpecialAirLw, false, false, 0, 0, 0.2F, 0, 0, &k_barrier_fx,
};

static void ftYo_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_push); }
static void ftYo_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_ataru); }
static void ftYo_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_jump); }
static void ftYo_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_barrier); }

const FtCustomMoves ftYo_Moves = {
    "Yoda",
    ftYo_SpecialN, ftYo_SpecialS, ftYo_SpecialLw, ftYo_SpecialHi,
    ftYo_SpecialN, ftYo_SpecialS, ftYo_SpecialLw, ftYo_SpecialHi,
    NULL, NULL,
};
