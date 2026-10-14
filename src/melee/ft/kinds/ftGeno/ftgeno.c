/* Geno's special moves: a custom fighter on Mario (fighters/Ge, "moves
 * Geno"), entered in place of Mario's for the fighters wearing his costumes
 * (src/melee/ft/ftcustom.h). They run on Mario's motions -- the skeleton is
 * Mario's -- and his fireball article, flown differently.
 *
 *   Geno Beam   (B)        a fast straight shot from the finger, 6%
 *   Geno Whirl  (side B)   a slower, larger shot that flies twice as far, 10%
 *   Geno Boost  (down B)   ten seconds of armour: hits push him less
 *   up B                   Mario's Super Jump Punch
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
#include <melee/it/kinds/itmariofireball.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/jobj.h>

#define GE_BEAM_SPEED 3.6F
#define GE_BEAM_LIFE 40
#define GE_BEAM_SCALE 1.2F
#define GE_BEAM_DAMAGE 6
#define GE_WHIRL_SPEED 1.7F
#define GE_WHIRL_LIFE 110
#define GE_WHIRL_SCALE 1.8F
#define GE_WHIRL_DAMAGE 10
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

/* Straight flight: no gravity, the speed it was given. The article's
 * script makes its hitbox after the spawn, so the shot's own damage and
 * size go on it here, once per hitbox (it_80272460 records the damage in
 * unk_count). */
static void shot_hitbox(Item_GObj* gobj, u32 damage, f32 scale)
{
    Item* it = GET_ITEM(gobj);
    int i;
    for (i = 0; i < 4; i++) {
        HitCapsule* hit = &it->x5D4_hitboxes[i].hit;
        if (hit->state != HitCapsule_Disabled && hit->unk_count != damage) {
            it_80272460(hit, damage, gobj);
            hit->scale *= scale;
        }
    }
}

static void ftGe_BeamPhys(Item_GObj* gobj)
{
    shot_hitbox(gobj, GE_BEAM_DAMAGE, GE_BEAM_SCALE);
}

static void ftGe_WhirlPhys(Item_GObj* gobj)
{
    shot_hitbox(gobj, GE_WHIRL_DAMAGE, GE_WHIRL_SCALE);
}

/* Gone at the first wall or floor it touches, or when its life runs out
 * (the article's own animation callback counts that down). */
static bool ftGe_ShotColl(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    return it_8027781C(gobj) ? true : false;
}

static void spawn_shot(Fighter_GObj* gobj, f32 speed, int life, f32 scale,
                       void (*phys)(Item_GObj*))
{
    Fighter* fp = GET_FIGHTER(gobj);
    SpawnItem spawn;
    Vec3 pos;
    Item_GObj* item_gobj;
    Item* it;
    lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, FtPart_L1stNb)].joint, NULL, &pos);
    spawn.kind = It_Kind_Mario_Fire;
    Item_InitSpawnPositionFromParent(&spawn, gobj, &pos);
    Item_InitSpawnCommonFields(&spawn, gobj, fp->facing_dir, true);
    item_gobj = Item_80268B18(&spawn);
    if (item_gobj == NULL) {
        return;
    }
    it_8029B7C0(item_gobj);
    db_80225DD8(item_gobj, gobj);
    it_802750F8(item_gobj);
    it = GET_ITEM(item_gobj);
    it->x40_vel.x = fp->facing_dir * speed;
    it->x40_vel.y = 0.0F;
    it->xD44_lifeTimer = life;
    it->physics_updated = phys;
    it->collided = ftGe_ShotColl;
    {
        Vec3 s = { scale, scale, scale };
        HSD_JObjSetScale(GET_JOBJ(item_gobj), &s);
    }
    efSync_Spawn(1146, gobj, fp->parts[ftParts_GetBoneIndex(fp, FtPart_L1stNb)].joint,
                 &fp->facing_dir);
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
        spawn_shot(gobj, GE_BEAM_SPEED, GE_BEAM_LIFE, GE_BEAM_SCALE, ftGe_BeamPhys);
        break;
    case GE_SHOT_WHIRL:
        spawn_shot(gobj, GE_WHIRL_SPEED, GE_WHIRL_LIFE, GE_WHIRL_SCALE, ftGe_WhirlPhys);
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
