/* Geno's special moves: a custom fighter on Mario (fighters/Ge, "moves
 * Geno"), entered in place of Mario's for the fighters wearing his costumes
 * (src/melee/ft/ftcustom.h). They run on Mario's motions -- the skeleton is
 * Mario's -- firing common items' shots.
 *
 *   Geno Beam   (B)        the Ray Gun's ray from his hand
 *   Geno Whirl  (side B)   the Star Rod's big spinning star
 *   Geno Boost  (down B)   ten seconds of armour: hits push him less
 *   up B                   Mario's Super Jump Punch, flashing (below)
 *   Geno Flash             his fire and coin hits are electric instead
 *
 * All state here is the fighter's own (ftGe_State by player slot) and moves
 * only with the simulation, so it is the same on every machine online. */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/db/db.h>
#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcustom.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftMario/forward.h>
#include <melee/ft/kinds/ftMario/ftmariospecialn.h>
#include <melee/ft/types.h>
#include <melee/it/inlines.h>
#include <melee/it/it_2725.h>
#include <melee/it/itcoll.h>
#include <melee/it/item.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/kinds/inlines.h>
#include <melee/it/kinds/itlgunray.h>
#include <melee/it/kinds/itstarrodstar.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/jobj.h>

#define GE_BOOST_FRAMES 600
#define GE_BOOST_ARMOR 40.0F
#define GE_BOOST_COLANIM 53 /* the glow of a charged shot */

typedef enum GeShot {
    GE_SHOT_NONE,
    GE_SHOT_BEAM,
    GE_SHOT_WHIRL,
    GE_SHOT_BOOST,
} GeShot;

static struct {
    GeShot shot; /* what the motion's throw frame does */
    int boost;   /* frames of Geno Boost left */
} ftGe_State[6];

static inline int slot_of(Fighter* fp)
{
    return fp->player_idx < 6 ? fp->player_idx : 0;
}

/* Geno Beam is the Ray Gun's ray and Geno Whirl the Star Rod's spinning
 * star (its strong one), fired from his hand: common items' shots, so their
 * models load in every match, and simulation like any item. */
static void spawn_shot(Fighter_GObj* gobj, GeShot shot)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, FtPart_L1stNb)].joint, NULL, &pos);
    if (shot == GE_SHOT_BEAM) {
        it_80298168(gobj, &pos, fp->facing_dir);
    } else {
        it_802988E4(gobj, &pos, 1, fp->facing_dir);
    }
}

/* His hits flash with electricity where Mario's burn or shed coins (the
 * forward smash, the up B): Geno Flash. Set on the hitboxes the motion
 * made this frame, before they are checked. */
static void flash_hits(Fighter* fp)
{
    int i;
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        if (hit->state != HitCapsule_Disabled &&
            (hit->element == HitElement_Fire || hit->element == HitElement_Coin))
        {
            hit->element = HitElement_Electric;
        }
    }
}

/* The motion's throw frame (Mario's fireball frame). */
static void ftGe_ThrowFrame(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!fp->throw_flags_b0) {
        return;
    }
    fp->throw_flags_b0 = false;
    switch (ftGe_State[slot_of(fp)].shot) {
    case GE_SHOT_BEAM:
    case GE_SHOT_WHIRL:
        spawn_shot(gobj, ftGe_State[slot_of(fp)].shot);
        break;
    case GE_SHOT_BOOST:
        ftGe_State[slot_of(fp)].boost = GE_BOOST_FRAMES;
        ftCo_800BFFD0(fp, GE_BOOST_COLANIM, false);
        break;
    default:
        break;
    }
}

static void enter(Fighter_GObj* gobj, GeShot shot, bool air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    fp->cmd_vars[0] = 0;
    fp->throw_flags = 0;
    ftGe_State[slot_of(fp)].shot = shot;
    Fighter_ChangeMotionState(gobj, air ? ftMr_MS_SpecialAirN : ftMr_MS_SpecialN, Ft_MF_None, 0, 1,
                              0, NULL);
    ftAnim_8006EBA4(gobj);
    fp->accessory4_cb = ftGe_ThrowFrame;
}

static void ftGe_SpecialN(Fighter_GObj* g) { enter(g, GE_SHOT_BEAM, false); }
static void ftGe_SpecialAirN(Fighter_GObj* g) { enter(g, GE_SHOT_BEAM, true); }
static void ftGe_SpecialS(Fighter_GObj* g) { enter(g, GE_SHOT_WHIRL, false); }
static void ftGe_SpecialAirS(Fighter_GObj* g) { enter(g, GE_SHOT_WHIRL, true); }
static void ftGe_SpecialLw(Fighter_GObj* g) { enter(g, GE_SHOT_BOOST, false); }
static void ftGe_SpecialAirLw(Fighter_GObj* g) { enter(g, GE_SHOT_BOOST, true); }

static void ftGe_Frame(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int* boost = &ftGe_State[slot_of(fp)].boost;
    /* Mario's landing and leaving-the-ground changes put his own fireball
     * frame back. */
    if (fp->accessory4_cb == ftMr_SpecialN_ItemFireSpawn) {
        fp->accessory4_cb = ftGe_ThrowFrame;
    }
    flash_hits(fp);
    if (*boost > 0) {
        (*boost)--;
        fp->dmg.armor0 = *boost > 0 ? GE_BOOST_ARMOR : 0.0F;
    }
}

static void ftGe_Reset(Fighter* fp)
{
    ftGe_State[slot_of(fp)].shot = GE_SHOT_NONE;
    ftGe_State[slot_of(fp)].boost = 0;
}

const FtCustomMoves ftGe_Moves = {
    "Geno",
    ftGe_SpecialN,
    ftGe_SpecialS,
    ftGe_SpecialLw,
    NULL, /* up B: Mario's */
    ftGe_SpecialAirN,
    ftGe_SpecialAirS,
    ftGe_SpecialAirLw,
    NULL,
    ftGe_Frame,
    ftGe_Reset,
};
