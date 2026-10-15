/* Krypto the Superdog's special moves: a custom fighter on Pikachu
 * (fighters/Kr, "moves Krypto"). His animations and hitboxes are the
 * pack's (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): no electricity.
 *
 *   Heat Vision         (B)       a bolt from his eyes (the Ray Gun's ray)
 *   Super-Dog Flight    (side B)  a straight flying dash, forepaw first
 *   Up, Up and Away     (up B)    flying straight up; a helpless fall
 *   Freeze Breath       (down B)  a cold blast ahead that freezes */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/kinds/ftPikachu/forward.h>
#include <melee/ft/types.h>

static const FtCustomFx k_heat_fx = {
    FT_CUSTOM_SHOT_RAY, 12, FtPart_HeadN, 0, 0, 0, 0, FtPart_HeadN, 10, 12, 2,
};
static const FtCustomFx k_flight_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 5, 26, 5, 0,
};
static const FtCustomFx k_away_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 4, 28, 6, 0,
};
static const FtCustomFx k_breath_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HeadN, 16, 36, 4,
};

static const FtCustomSpecial k_heat = {
    ftPk_MS_SpecialN, ftPk_MS_SpecialAirN, false, false, 0, 0, 0.5F, 0, 0, &k_heat_fx,
};
static const FtCustomSpecial k_flight = {
    ftPk_MS_SpecialSStart, ftPk_MS_SpecialAirSStart, false, false, 3.2F, 0, 0.0F, 0, 0, &k_flight_fx,
};
static const FtCustomSpecial k_away = {
    ftPk_MS_SpecialHiStart0, ftPk_MS_SpecialAirHiStart0, true, false, 0.3F, 3.8F, 0.3F, 1.1F, 18.0F, &k_away_fx,
};
static const FtCustomSpecial k_breath = {
    ftPk_MS_SpecialLwStart, ftPk_MS_SpecialAirLwStart, false, false, 0, 0, 0.3F, 0, 0, &k_breath_fx,
};

static void ftKr_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_heat); }
static void ftKr_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_flight); }
static void ftKr_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_away); }
static void ftKr_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_breath); }

const FtCustomMoves ftKr_Moves = {
    "Krypto",
    ftKr_SpecialN, ftKr_SpecialS, ftKr_SpecialLw, ftKr_SpecialHi,
    ftKr_SpecialN, ftKr_SpecialS, ftKr_SpecialLw, ftKr_SpecialHi,
    NULL, NULL,
};
