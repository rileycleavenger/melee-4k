/* The Crewmate's special moves: a custom fighter on Kirby (fighters/Cr,
 * "moves Crewmate"). Its animations and hitboxes are the pack's
 * (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): no inhale, hammer, cutter or
 * stone.
 *
 *   Emergency Meeting (B)       a slam on the button: everyone close is
 *                               stopped where they stand
 *   Impostor Kill     (side B)  a dash, knife out
 *   Ejected           (up B)    launched up, tumbling; a helpless fall
 *   Vent              (down B)  down into the floor, across, and out under
 *                               the foe */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftKirby/forward.h>
#include <melee/ft/types.h>

#define CR_ALARM_COLANIM 15

static const FtCustomFx k_meeting_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 16, 16, 1, 0, 16, CR_ALARM_COLANIM,
};
static const FtCustomFx k_kill_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 10, 10, 1,
};
static const FtCustomFx k_eject_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 3, 28, 6, 1000,
};
static const FtCustomFx k_vent_fx = {
    0, 0, 0, 8, 28, 1.6F, 0.0F, FtPart_HipN, 8, 28, 20, 1000,
};

static const FtCustomSpecial k_meeting = {
    ftKb_MS_SpecialN, ftKb_MS_SpecialAirN, false, false, 0, 0, 0.4F, 0, 0, &k_meeting_fx,
};
static const FtCustomSpecial k_kill = {
    ftKb_MS_SpecialS, ftKb_MS_SpecialAirS, false, false, 2.6F, 0, 0.3F, 0, 0, &k_kill_fx,
};
static const FtCustomSpecial k_eject = {
    ftKb_MS_SpecialHi1, ftKb_MS_SpecialAirHi1, true, false, 0.3F, 3.6F, 0.35F, 1.2F, 20.0F, &k_eject_fx,
};
static const FtCustomSpecial k_vent = {
    ftKb_MS_SpecialLw, ftKb_MS_SpecialAirLw, false, false, 0, 0, 0.5F, 0, 0, &k_vent_fx,
};

static void ftCr_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_meeting); }
static void ftCr_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_kill); }
static void ftCr_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_eject); }
static void ftCr_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_vent); }

const FtCustomMoves ftCr_Moves = {
    "Crewmate",
    ftCr_SpecialN, ftCr_SpecialS, ftCr_SpecialLw, ftCr_SpecialHi,
    ftCr_SpecialN, ftCr_SpecialS, ftCr_SpecialLw, ftCr_SpecialHi,
    NULL, NULL,
};
