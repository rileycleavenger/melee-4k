/* King Boo's special moves: a custom fighter on Jigglypuff (fighters/Kg,
 * "moves KingBoo"). His animations and hitboxes are the pack's
 * (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): no rollout, song or rest.
 *
 *   Ghost Swarm   (B)       his ghosts (Goob and Greenie, the pack's model
 *                           on spare joints) circle him, then fly at the
 *                           foe: hitboxes on the ghosts, no projectile
 *   Boo Dash      (side B)  invisible across the ground, then a lash out of
 *                           nowhere
 *   Boo Teleport  (up B)    he fades out, slips a short way up unseen and
 *                           intangible, and reappears cackling; a helpless
 *                           fall
 *   Crown Slam    (down B)  up, then crashing down: darkness either side */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcustom.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftPurin/forward.h>
#include <melee/ft/types.h>

#define KG_SCARE_COLANIM 15
/* Boo Teleport: unseen from frame TP_FROM to TP_TO, rising TP_VY a frame
 * (about his height and a half in all). */
#define KG_TP_FROM 8
#define KG_TP_TO 17
#define KG_TP_VY 3.1F

static const FtCustomFx k_swarm_fx = {
    0,
};
static const FtCustomFx k_dash_fx = {
    0, 0, 0, 4, 16, 2.4F, 0.0F, FtPart_HipN, 16, 16, 1, 0, 18, KG_SCARE_COLANIM,
};
static const FtCustomFx k_rise_fx = {
    0, 0, 0, KG_TP_FROM, KG_TP_TO, 0.3F, KG_TP_VY, FtPart_HipN, 0, 0, 0, 0, KG_TP_TO, KG_SCARE_COLANIM,
};
static const FtCustomFx k_slam_fx = {
    0, 0, 0, 0, 0, 0, 0, FtPart_HipN, 14, 14, 1, 0, 14, 0,
};

static const FtCustomSpecial k_swarm = {
    ftPr_MS_SpecialNStartR, ftPr_MS_SpecialAirNStartR, false, false, 0, 0, 0.3F, 0, 0, &k_swarm_fx,
};
static const FtCustomSpecial k_dash = {
    ftPr_MS_SpecialS, ftPr_MS_SpecialAirS, false, false, 0, 0, 0.0F, 0, 0, &k_dash_fx,
};
static const FtCustomSpecial k_rise = {
    ftPr_MS_SpecialHiR, ftPr_MS_SpecialAirHiR, true, false, 0, 0.01F, 0.0F, 1.0F, 18.0F, &k_rise_fx,
};
static const FtCustomSpecial k_slam = {
    ftPr_MS_SpecialLwR, ftPr_MS_SpecialAirLwR, false, false, 0, 1.6F, 2.0F, 0, 0, &k_slam_fx,
};

static void ftKg_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_swarm); }
static void ftKg_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_dash); }
static void ftKg_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_rise); }
static void ftKg_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_slam); }

/* Boo Teleport: intangible while unseen; where he reappears he stops (the
 * vanish's rise does not carry on) and hangs a moment before the fall. */
static void ftKg_Frame(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (ftCustom_SpecialActive(fp) != &k_rise) {
        return;
    }
    if (fp->cur_anim_frame >= KG_TP_FROM && fp->cur_anim_frame < KG_TP_TO) {
        ftColl_8007B62C(gobj, 2);
    } else if (fp->cur_anim_frame >= KG_TP_TO && fp->cur_anim_frame < KG_TP_TO + 1) {
        ftColl_8007B62C(gobj, 0);
        fp->self_vel.x = 0.0F;
        fp->self_vel.y = 0.4F;
    }
}

const FtCustomMoves ftKg_Moves = {
    "KingBoo",
    ftKg_SpecialN, ftKg_SpecialS, ftKg_SpecialLw, ftKg_SpecialHi,
    ftKg_SpecialN, ftKg_SpecialS, ftKg_SpecialLw, ftKg_SpecialHi,
    ftKg_Frame, NULL,
};
