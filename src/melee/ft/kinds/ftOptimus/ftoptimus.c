/* Optimus Prime's special moves: a custom fighter on Samus (fighters/Op,
 * "moves Optimus"). His animations and hitboxes are the pack's
 * (motions.bin); the moves run on the generic custom specials
 * (ftCustom_SpecialEnter, with FtCustomFx): nothing of Samus's charge
 * shot, missiles, screw attack or bombs.
 *
 *   Ion Blaster          (B)       a bolt from his right hand
 *   Roll Out             (side B)  he becomes the truck (Samus's morph-ball
 *                                  model state: the pack's truck mesh) and
 *                                  drives, left or right as the stick says,
 *                                  ramming what is in the way
 *   Rising Axe           (up B)    a short leap, the Energon Axe swung up
 *                                  through it; a helpless fall
 *   Matrix of Leadership (down B)  the chest blazes: a burst of light all
 *                                  around, armoured through it */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ft/ftcustom.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftSamus/forward.h>
#include <melee/ft/types.h>

#define OP_MATRIX_COLANIM 53 /* the glow of a charged shot */
/* Roll Out: the truck from frame TRUCK_FROM to TRUCK_TO of the move
 * (moves.py), at TRUCK_SPEED, turned round by the stick held back. */
#define OP_TRUCK_FROM 5
#define OP_TRUCK_TO 48
#define OP_TRUCK_SPEED 2.3F
#define OP_TRUCK_TURN 0.6F    /* stick, back: turn round */
#define OP_TRUCK_PART 0       /* Samus's body model part: state 1 is the ball */
#define OP_TRUCK_STATE 1

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
    ftSs_MS_SpecialS, ftSs_MS_SpecialAirS, false, false, 0, 0, 1.0F, 0, 0, &k_roll_fx,
};
static const FtCustomSpecial k_axe = {
    ftSs_MS_SpecialHi, ftSs_MS_SpecialAirHi, true, false, 0.5F, 2.3F, 1.0F, 1.0F, 24.0F, &k_axe_fx,
};
static const FtCustomSpecial k_matrix = {
    ftSs_MS_SpecialLw, ftSs_MS_SpecialAirLw, false, false, 0, 0, 0.2F, 0, 0, &k_matrix_fx,
};

/* Roll Out, every frame: the truck shown while he drives (the next motion
 * change puts the robot back, however the move ends), its speed held, and
 * the stick held back turns it round. */
static void ftOp_Frame(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    bool truck;
    if (ftCustom_SpecialActive(fp) != &k_roll) {
        return;
    }
    truck = fp->cur_anim_frame >= OP_TRUCK_FROM && fp->cur_anim_frame < OP_TRUCK_TO;
    ftParts_80074B0C(gobj, OP_TRUCK_PART, truck ? OP_TRUCK_STATE : 0);
    if (!truck) {
        return;
    }
    if (fp->input.lstick[0].x * fp->facing_dir <= -OP_TRUCK_TURN) {
        fp->facing_dir = -fp->facing_dir;
        ftPartSetRotY(fp, 0, M_PI_2 * fp->facing_dir);
    }
    if (fp->ground_or_air == GA_Ground) {
        fp->gr_vel = OP_TRUCK_SPEED * fp->facing_dir;
    } else {
        fp->self_vel.x = 0.8F * OP_TRUCK_SPEED * fp->facing_dir;
    }
}

static void ftOp_SpecialN(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_blaster); }
static void ftOp_SpecialS(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_roll); }
static void ftOp_SpecialHi(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_axe); }
static void ftOp_SpecialLw(Fighter_GObj* g) { ftCustom_SpecialEnter(g, &k_matrix); }

const FtCustomMoves ftOp_Moves = {
    "Optimus",
    ftOp_SpecialN, ftOp_SpecialS, ftOp_SpecialLw, ftOp_SpecialHi,
    ftOp_SpecialN, ftOp_SpecialS, ftOp_SpecialLw, ftOp_SpecialHi,
    ftOp_Frame, NULL,
};
