#ifndef MELEE_FT_FTCUSTOM_H
#define MELEE_FT_FTCUSTOM_H

#include <Runtime/platform.h>

#include "melee/ft/forward.h"

/* Custom fighters (PC). A custom fighter is an existing fighter -- its base
 * -- wearing costumes past the base's own: Geno is Mario with costume 5 and
 * up. The fighter kind stays the base's, so every table and every check the
 * game makes on that kind applies unchanged; only the costume files differ
 * (src/pc/fighter_pack.c puts them on the virtual disc). Menus and screens
 * tell the two apart with ftCustom_Pack. */

/* Gives each pack's base fighter the pack's costumes, and its special moves
 * (FtCustomMoves) to the fighters wearing them. Once at boot, after
 * pc_fighter_packs_init. */
void ftCustom_Init(void);

/* A custom fighter's own special moves, compiled into the game like any
 * fighter's code (src/melee/ft/kinds/<name>/) and named by a pack's
 * fighter.cfg ("moves Geno"). Each is entered in place of the base
 * fighter's; NULL keeps the base's. on_frame runs every frame
 * (Fighter_procCollPos: after the frame's motion changes, before the
 * accessory callbacks they install) and on_reset at every (re)spawn, for
 * state the moves keep. */
typedef struct FtCustomMoves {
    const char* name;
    void (*special_n)(Fighter_GObj*);
    void (*special_s)(Fighter_GObj*);
    void (*special_lw)(Fighter_GObj*);
    void (*special_hi)(Fighter_GObj*);
    void (*special_air_n)(Fighter_GObj*);
    void (*special_air_s)(Fighter_GObj*);
    void (*special_air_lw)(Fighter_GObj*);
    void (*special_air_hi)(Fighter_GObj*);
    void (*on_frame)(Fighter_GObj*);
    void (*on_reset)(Fighter*);
} FtCustomMoves;

/* A custom fighter's special move on the base's own motion rows (their
 * animations and scripts are the pack's, motions.bin) with generic
 * behaviour instead of the base's special code (which would bring the
 * base's guns, reflectors and flames along): it plays the move, follows
 * the fighter between ground and air, and ends standing/falling or, for a
 * recovery, in a helpless fall. The character's own code adds what is
 * special (projectiles, effects) from its on_frame, on the move's script
 * event (ftCustom_SpecialEvent). */
/* What a special does besides moving, read off its frame counter while it
 * runs (all optional, 0 = none): a shot from a body part, a vanish (the
 * fighter invisible and travelling between two frames), sparks along the
 * way, a flash, armour for the whole move. */
enum { FT_CUSTOM_SHOT_NONE, FT_CUSTOM_SHOT_RAY, FT_CUSTOM_SHOT_STAR };
typedef struct FtCustomFx {
    int shot, shot_frame, shot_part;          /* FT_CUSTOM_SHOT_*, frame, FtPart_* */
    int vanish_start, vanish_end;             /* invisible from..to (frames) */
    float vanish_vx, vanish_vy;               /* velocity meanwhile (x toward the front) */
    int spark_part, spark_from, spark_to, spark_every; /* sparks at a part */
    int spark_ef;                             /* effect id (0: none); only common effects
                                               * every match loads. A fighter's own look
                                               * is its pack's vfx.cfg (ftcustomvfx.h) */
    int flash_frame, flash_colanim;           /* screen flash and the fighter's glow */
    float armor;                              /* damage armour while the move runs */
} FtCustomFx;

typedef struct FtCustomSpecial {
    int msid_ground, msid_air; /* the base's motion states whose rows hold the move */
    bool helpless;             /* a recovery: a helpless fall after it in the air */
    bool root_motion;          /* the animation moves the fighter (rows with root motion) */
    float vel_x, vel_y;        /* set on entry (x toward the front; on the ground a
                                * slide); both 0: kept */
    float gravity;             /* in the air: x the fighter's gravity (0: hover; 1: normal) */
    float mobility, landing_lag; /* the helpless fall's */
    const FtCustomFx* fx;      /* run by ftCustom_Frame while the move is on */
} FtCustomSpecial;
void ftCustom_SpecialEnter(Fighter_GObj* gobj, const FtCustomSpecial* d);
/* The move a fighter is in (ftCustom_SpecialEnter's), or NULL. */
const FtCustomSpecial* ftCustom_SpecialActive(Fighter* fp);
/* True once each time the move's script fires its event (the base's
 * "throw" flag: where a projectile leaves the hand). */
bool ftCustom_SpecialEvent(Fighter* fp);

/* The custom fighter's moves, or NULL (a retail fighter, or no "moves"). */
const FtCustomMoves* ftCustom_Moves(Fighter* fp);
/* The pack says "voice none": the base's voice clips stay silent (as for
 * a metal fighter, fp->x2225_b5). */
bool ftCustom_Mute(Fighter* fp);
/* fighter.cfg's attribute multipliers, on the attributes just copied from
 * the data file (ftchangeparam.c). */
void ftCustom_ApplyAttrs(Fighter* fp);
/* The fighter's kind as the grab code sees it: a custom fighter on Link,
 * Young Link or Samus grabs at close range (its own Catch motions) with no
 * hookshot or grapple beam, as an ordinary grabber; everyone else is their
 * kind. */
FighterKind ftCustom_GrabKind(Fighter* fp);
void ftCustom_Frame(Fighter_GObj* gobj);
/* The pack's own animations and action scripts (motions.bin): the fighter's
 * motion table becomes a copy with those rows replaced. After its
 * animations are loaded (ftData_80085B10). */
void ftCustom_Motions(Fighter* fp);
void ftCustom_Reset(Fighter* fp);
/* How many costumes the base fighter has on the disc. */
int ftCustom_RetailCostumes(FighterKind kind);
/* The pack a (kind, costume) belongs to, or -1 for a retail costume. */
int ftCustom_Pack(FighterKind kind, int costume);
/* The costume to read the base fighter's on-disc per-costume tables with:
 * a custom costume uses the base's first. */
int ftCustom_DataCostume(FighterKind kind, int costume);
/* The first costume of pack `pack`, and how many it has. */
int ftCustom_FirstCostume(int pack);
int ftCustom_CostumeCount(int pack);
FighterKind ftCustom_BaseKind(int pack);
/* The pack with two-letter id `id`, or -1; and the character select
 * character its base fighter is (CKind_*), or -1. */
int ftCustom_PackById(const char* id);
int ftCustom_BaseCharacter(int pack);

/* Menu and HUD images from the pack's Pl<id>Ui.dat (build_ui.py), loaded
 * into the current scene's heap on first use. ftCustom_UiReset forgets them
 * (a new scene's heap); call it where a screen is built. */
void ftCustom_UiReset(void);
struct HSD_ImageDesc* ftCustom_UiImage(int pack, const char* symbol);
/* The stock icon of the fighter in player slot `slot`, or NULL for a
 * retail one. */
struct HSD_ImageDesc* ftCustom_StockImage(int slot);
/* Image `symbol` of the custom fighter in player slot `slot`, or NULL. */
struct HSD_ImageDesc* ftCustom_SlotImage(int slot, const char* symbol);
/* The name of the custom fighter in player slot `slot`, or NULL. */
const char* ftCustom_SlotName(int slot);

#endif
