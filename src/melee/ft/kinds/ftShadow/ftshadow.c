/* Shadow the Hedgehog's special moves: a custom fighter on Fox (fighters/Sh,
 * "moves Shadow"). His animations and hitboxes are the pack's (motions.bin);
 * the moves run on the generic custom specials (ftCustom_SpecialEnter), not
 * Fox's own code, so nothing of Fox's blaster, illusion, fire or reflector
 * shows.
 *
 *   Chaos Spear    (B)       a spear of Chaos energy from his hand (the Ray
 *                            Gun's ray), sparking
 *   Chaos Control  (side B)  he vanishes and reappears a few lengths on,
 *                            kicking
 *   Air shoes      (up B)    a jet boost straight up, then a helpless fall
 *   Chaos Blast    (down B)  he hangs in the air, curled, and bursts in red
 *
 * All state is the fighter's own and moves with the simulation only. */
#include <Runtime/platform.h>

#include <melee/ft/forward.h>

#include <melee/ef/efsync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcustom.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftFox/forward.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/itlgunray.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lb_0219.h>

#define SH_SPEAR_FRAME 9
#define SH_CC_START 4  /* Chaos Control: invisible from here... */
#define SH_CC_END 12   /* ...to here, travelling */
#define SH_CC_SPEED 3.6F
#define SH_BLAST_FRAME 16
#define SH_FLASH_COLANIM 15

static const FtCustomSpecial k_spear = {
    ftFx_MS_SpecialNStart, ftFx_MS_SpecialAirNStart, false, false, 0, 0, 0.5F, 0, 0,
};
static const FtCustomSpecial k_control = {
    ftFx_MS_SpecialSStart, ftFx_MS_SpecialAirSStart, false, false, 0, 0, 0.0F, 0, 0,
};
static const FtCustomSpecial k_jets = {
    ftFx_MS_SpecialHi, ftFx_MS_SpecialAirHi, true, false, 0.9F, 3.4F, 0.35F, 1.0F, 22.0F,
};
static const FtCustomSpecial k_blast = {
    ftFx_MS_SpecialLwStart, ftFx_MS_SpecialAirLwStart, false, false, 0, 0, 0.15F, 0, 0,
};

static bool part_pos(Fighter* fp, Fighter_Part part, Vec3* pos)
{
    FighterBone* bone = ftParts_GetBone(fp, part);
    if (bone == NULL || bone->joint == NULL) {
        return false;
    }
    lb_8000B1CC(bone->joint, NULL, pos);
    return true;
}

static void enter(Fighter_GObj* g, const FtCustomSpecial* d)
{
    ftCustom_SpecialEnter(g, d);
}
static void ftSh_SpecialN(Fighter_GObj* g) { enter(g, &k_spear); }
static void ftSh_SpecialS(Fighter_GObj* g) { enter(g, &k_control); }
static void ftSh_SpecialHi(Fighter_GObj* g) { enter(g, &k_jets); }
static void ftSh_SpecialLw(Fighter_GObj* g) { enter(g, &k_blast); }

static void ftSh_Frame(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const FtCustomSpecial* d = ftCustom_SpecialActive(fp);
    const int f = (int) fp->cur_anim_frame;
    Vec3 pos;
    if (d == &k_spear) {
        if (f == SH_SPEAR_FRAME && part_pos(fp, FtPart_RHandN, &pos)) {
            it_80298168(gobj, &pos, fp->facing_dir);
        }
    } else if (d == &k_control) {
        /* Gone between the two frames, moving; back with the kick. */
        fp->invisible = f >= SH_CC_START && f < SH_CC_END;
        if (fp->invisible) {
            if (fp->ground_or_air == GA_Ground) {
                fp->gr_vel = SH_CC_SPEED * fp->facing_dir;
            } else {
                fp->self_vel.x = SH_CC_SPEED * fp->facing_dir;
                fp->self_vel.y = 0.0F;
            }
        }
        if (f == SH_CC_START || f == SH_CC_END) {
            ftCo_800BFFD0(fp, SH_FLASH_COLANIM, false);
        }
    } else if (d == &k_blast) {
        /* its look: the pack's vfx.cfg (the red sphere, the jets' flames) */
        if (f == SH_BLAST_FRAME) {
            lbBgFlash_80021C48(4, 0);
            ftCo_800BFFD0(fp, SH_FLASH_COLANIM + 2, false);
        }
    }
}

static void ftSh_Reset(Fighter* fp)
{
    fp->invisible = false;
}

const FtCustomMoves ftSh_Moves = {
    "Shadow",
    ftSh_SpecialN,
    ftSh_SpecialS,
    ftSh_SpecialLw,
    ftSh_SpecialHi,
    ftSh_SpecialN,
    ftSh_SpecialS,
    ftSh_SpecialLw,
    ftSh_SpecialHi,
    ftSh_Frame,
    ftSh_Reset,
};
