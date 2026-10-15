/* Thragg's special moves: a custom fighter on Captain Falcon (fighters/Th,
 * "moves Thragg"). His animations and hitboxes are the pack's (motions.bin);
 * the moves run on the generic custom specials (ftCustom_SpecialEnter, with
 * FtCustomFx): nothing of Falcon's fire or grabs.
 *
 *   Viltrumite Haymaker (B)       one punch behind a lunge, armoured
 *                                 through the wind-up
 *   Flying Tackle       (side B)  flight straight ahead, fist first
 *   Flight              (up B)    straight up, fist first; a helpless fall
 *   Planet Cracker      (down B)  both fists into the ground: a burying
 *                                 shock wave; in the air, a diving meteor */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftCaptain/forward.h>
#include <melee/ft/types.h>

static const FtCustomFx k_haymaker_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_RHandN, 23, 23, 1, 0, 23, 0, 18.0F,
};
static const FtCustomFx k_tackle_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 6, 26, 5, 0,
};
static const FtCustomFx k_flight_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_LFootJ, 4, 28, 6, 0,
};
static const FtCustomFx k_crack_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_RHandN, 12, 12, 1, 0, 12, 0,
};

static const FtCustomSpecial k_haymaker = {
    ftCa_MS_SpecialN, ftCa_MS_SpecialAirN, false, false, 1.2F, 0, 0.3F, 0, 0, &k_haymaker_fx,
};
static const FtCustomSpecial k_tackle = {
    ftCa_MS_SpecialSStart, ftCa_MS_SpecialAirSStart, false, false, 3.0F, 0, 0.0F, 0, 0, &k_tackle_fx,
};
static const FtCustomSpecial k_flight = {
    ftCa_MS_SpecialHi, ftCa_MS_SpecialAirHi, true, false, 0.5F, 3.6F, 0.3F, 1.0F, 26.0F, &k_flight_fx,
};
static const FtCustomSpecial k_crack = {
    ftCa_MS_SpecialLw, ftCa_MS_SpecialAirLw, false, false, 0, -3.5F, 2.5F, 0, 0, &k_crack_fx,
};

static void ftTh_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_haymaker); }
static void ftTh_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_tackle); }
static void ftTh_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_flight); }
static void ftTh_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_crack); }

const FtCustomMoves ftTh_Moves = {
    "Thragg",
    ftTh_SpecialN, ftTh_SpecialS, ftTh_SpecialLw, ftTh_SpecialHi,
    ftTh_SpecialN, ftTh_SpecialS, ftTh_SpecialLw, ftTh_SpecialHi,
    NULL, NULL,
};
