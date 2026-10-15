/* Geno's special moves: a custom fighter on Mario (fighters/Ge, "moves
 * Geno"), entered in place of Mario's for the fighters wearing his costumes
 * (src/melee/ft/ftcustom.h). They run on Mario's motions -- the skeleton is
 * Mario's -- firing common items' shots.
 *
 *   Geno Beam   (B)        the Ray Gun's ray from his hand
 *   Geno Whirl  (side B)   the Star Rod's big spinning star
 *   Geno Boost  (down B)   a burst of lightning, then ten seconds of
 *                          armour and harder hits, sparking all along
 *   up B                   Mario's Super Jump Punch, wrapped in lightning
 *                          from start to end
 *   Geno Flash             his fire and coin hits are electric instead
 *
 * His normal, smash and aerial attacks are his own animations and
 * hitboxes, from the pack (motions.bin, ftCustom_Motions); here they only
 * get their sparks.
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
#include <melee/ft/kinds/ftCommon/ftCo_AppealS.h>
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
#include <melee/lb/lb_0219.h>
#include <sysdolphin/baselib/jobj.h>

#define GE_BOOST_FRAMES 600
#define GE_BOOST_ARMOR 40.0F
#define GE_BOOST_COLANIM 53 /* the glow of a charged shot */
#define GE_BOOST_DAMAGE 1.25F
#define GE_ELEC_COLANIM 15 /* the flash of an electric hit */

typedef enum GeShot {
    GE_SHOT_NONE,
    GE_SHOT_BEAM,
    GE_SHOT_WHIRL,
    GE_SHOT_BOOST,
} GeShot;

static struct {
    GeShot shot; /* what the motion's throw frame does */
    int boost;   /* frames of Geno Boost left */
    u8 boosted;  /* hitboxes (bits) whose damage Geno Boost raised */
} ftGe_State[8];

static inline int slot_of(Fighter* fp)
{
    return fp->player_idx < 8 ? fp->player_idx : 0;
}

/* Lightning over his body: an electric spark on one of these bones every
 * other frame, in turn, and the electric hit's flash. Effects only. */
static const Fighter_Part k_spark_bones[] = {
    FtPart_HeadN, FtPart_LHandN, FtPart_BustN, FtPart_RKneeJ,  FtPart_RHandN,
    FtPart_HipN,  FtPart_LKneeJ, FtPart_NeckN, FtPart_LFootJ, FtPart_RFootJ,
};

/* Where a part is on the stage; false if the base has no bone for it. */
static bool part_pos(Fighter* fp, Fighter_Part part, Vec3* pos)
{
    FighterBone* bone = ftParts_GetBone(fp, part);
    if (bone == NULL || bone->joint == NULL) {
        return false;
    }
    lb_8000B1CC(bone->joint, NULL, pos);
    return true;
}

static void spark_at(Fighter* fp, Fighter_Part part)
{
    Vec3 pos;
    if (part_pos(fp, part, &pos)) {
        efSync_Spawn(Ef_Id_Unk1001, NULL, &pos);
    }
}

static void sparks(Fighter* fp, int every)
{
    int n = (int) fp->cur_anim_frame;
    if (every <= 0 || n % every != 0) {
        return;
    }
    n /= every;
    spark_at(fp, k_spark_bones[n % ARRAY_SIZE(k_spark_bones)]);
}

static void burst(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    int i;
    for (i = 0; i < (int) ARRAY_SIZE(k_spark_bones); i++) {
        spark_at(fp, k_spark_bones[i]);
    }
    lbBgFlash_80021C48(4, 0);
    ftCo_800BFFD0(fp, GE_ELEC_COLANIM + 2, false);
}

static void boost_start(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ftGe_State[slot_of(fp)].boost = GE_BOOST_FRAMES;
    burst(gobj);
}

/* Geno Beam is the Ray Gun's ray and Geno Whirl the Star Rod's spinning
 * star (its strong one), fired from his hand: common items' shots, so their
 * models load in every match, and simulation like any item. */
static void spawn_shot(Fighter_GObj* gobj, GeShot shot)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    if (!part_pos(fp, FtPart_L1stNb, &pos)) {
        pos = fp->cur_pos;
    }
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
        boost_start(gobj);
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
/* On the ground Geno Boost is a pose (the taunt's), the burst at once. */
static void ftGe_SpecialLw(Fighter_GObj* g)
{
    ftGe_State[slot_of(GET_FIGHTER(g))].shot = GE_SHOT_NONE;
    ftCo_800DEBD0(g);
    boost_start(g);
}
static void ftGe_SpecialAirLw(Fighter_GObj* g) { enter(g, GE_SHOT_BOOST, true); }

/* Electricity where his electric hits are (the cannon blasts of his smash
 * attacks, the floor shock, the shove): sparks every other frame. */
static void electric_sparks(Fighter* fp)
{
    int i;
    if ((int) fp->cur_anim_frame % 2 != 0) {
        return;
    }
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &fp->x914[i];
        /* (A hitbox's position is set after its first frame.) */
        if (hit->state != HitCapsule_Disabled && hit->element == HitElement_Electric &&
            (hit->x4C.x != 0.0F || hit->x4C.y != 0.0F))
        {
            efSync_Spawn(Ef_Id_Unk1001, NULL, &hit->x4C);
        }
    }
}

/* The taunt (his pack animation): three finger-gun shots, a spark and an
 * electric flash at his right hand on each. */
static void taunt_shots(Fighter* fp)
{
    const int f = (int) fp->cur_anim_frame;
    if ((fp->motion_id == ftCo_MS_AppealSR || fp->motion_id == ftCo_MS_AppealSL) &&
        (f == 50 || f == 62 || f == 74))
    {
        spark_at(fp, FtPart_RHandN);
        ftCo_800BFFD0(fp, GE_ELEC_COLANIM, false);
    }
}

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
    electric_sparks(fp);
    taunt_shots(fp);
    if (fp->motion_id == ftMr_MS_SpecialHi || fp->motion_id == ftMr_MS_SpecialAirHi) {
        sparks(fp, 2);
        if ((int) fp->cur_anim_frame % 12 == 0) {
            ftCo_800BFFD0(fp, GE_ELEC_COLANIM, false);
        }
    }
    if (*boost > 0) {
        u8* boosted = &ftGe_State[slot_of(fp)].boosted;
        int i;
        (*boost)--;
        fp->dmg.armor0 = *boost > 0 ? GE_BOOST_ARMOR : 0.0F;
        /* Each new hitbox hits harder, once. */
        for (i = 0; i < 4; i++) {
            HitCapsule* hit = &fp->x914[i];
            if (hit->state == HitCapsule_Disabled) {
                *boosted &= ~(1 << i);
            } else if (!(*boosted & (1 << i))) {
                *boosted |= 1 << i;
                hit->damage *= GE_BOOST_DAMAGE;
            }
        }
        if (*boost % 30 == 0) {
            sparks(fp, 1);
            ftCo_800BFFD0(fp, GE_BOOST_COLANIM, false);
        }
    }
}

static void ftGe_Reset(Fighter* fp)
{
    ftGe_State[slot_of(fp)].shot = GE_SHOT_NONE;
    ftGe_State[slot_of(fp)].boost = 0;
    ftGe_State[slot_of(fp)].boosted = 0;
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
