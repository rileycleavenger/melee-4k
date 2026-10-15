# A custom fighter's own moveset

The user's rule: **the complete moveset is original**, based on the
character's appearances in other games and media. Nothing of the base's
attacks may show: every jab, tilt, dash attack, smash, aerial, special and
the taunt gets the character's own animation and hitboxes. What may stay is
the base's *timing* (frame counts, hit frames, smash charge, IASA, landing,
auto-cancel), which keeps the moves balanced and the base's script commands
(jab chains, loops, throw frames) working.

Three layers, all needed:

1. **Attacks and the taunt** (no C): new animations and hitboxes in the
   pack's `motions.bin`, built by `tools/fighter/build_moves.py` from the
   character's `moves.py` (template: `templates/model/moves.py`).
2. **Special moves**: their animations in `motions.bin` too (the
   `Special*` rows), their behaviour (projectiles, buffs, effects,
   recovery) in C, `src/melee/ft/kinds/ft<Name>/`.
3. **Attributes** (no code): fighter.cfg multipliers - `weight`, `walk`,
   `dash`, `jump` (jump and hop speed), `gravity`, `fall` (terminal and
   fast-fall speed), `air` (air drift) - from how the character moves in
   its games (a heavy knight: weight up, jump down; a floaty mage:
   gravity down). Applied where the game copies the base's attributes
   (`ftCustom_ApplyAttrs`). Keep each in 0.7-1.3 unless the character is
   extreme.

## 1. Research (before any posing)

List the character's appearances (games, anime, films, comics) and from
them its signature actions: attacks, spells, weapons and items it uses,
transformations, poses, catchphrases, how it moves (stiff, bouncy,
floaty, heavy), its element (electric, fire, ice, darkness...). Search the
web when the character is unfamiliar (WebSearch: "<name> moves",
"<name> attacks <game>", its wiki's abilities page). Then assign one idea
per slot in `notes.md` "Moveset research": the most iconic ideas go to
the specials and smashes; quick, small ideas to jabs and tilts; how the
character fights in the air to the aerials; its signature pose or line to
the taunt. Each slot names its source. The Geno example: Geno Beam (B),
Geno Whirl (side B), Geno Boost (down B), a doll's stiff straight-arm
punches (jabs), a butt with his hat's point (forward tilt), Geno Cannon /
"arm cannon" blasts (forward smash, up smash), electric hits (Geno
Flash), his finger-gun pose (taunt).

Slots (Melee's motion names; `frame_data.py` lists the base's):

| Slot | Motions |
|---|---|
| Jabs | Attack11, Attack12, Attack13 (Attack100 rapid jab, if the base has one) |
| Dash attack | AttackDash (travels: root motion, below) |
| Forward tilt | AttackS3S, AttackS3Hi, AttackS3Lw (angled versions: a move each) |
| Up / down tilt | AttackHi3, AttackLw3 (crouched) |
| Forward smash | AttackS4S, AttackS4Hi, AttackS4Lw |
| Up / down smash | AttackHi4, AttackLw4 |
| Aerials | AttackAirN, AttackAirF, AttackAirB, AttackAirHi, AttackAirLw |
| Taunt | Appeal |
| Specials (animations) | SpecialN, SpecialAirN, SpecialS, SpecialSAir, SpecialHi, SpecialAirHi, SpecialLw, SpecialAirLw (names vary by base: frame_data.py) |
| Grab game | CatchAttack (pummel), ThrowF, ThrowB, ThrowHi, ThrowLw |
| Get-up, ledge | DownAttackU, DownAttackD, CliffAttackQuick, CliffAttackSlow |

The grab game and get-ups complete the set; do them when the rest is done
(throws hold the opponent at the base's throw bone: keep the base's timing
and test them with an opponent grabbed).

## 2. The base's frame data

`tools/fighter/frame_data.py BASE_DIR [MOTION...]` prints per motion its
length and script events: hitboxes (bone, damage, size, angle, kbg, bkb,
element), timers, IASA, auto-cancel, smash charge, throw frames (0x60 on
specials: where a projectile spawns), effects (0x28), sounds (0x44), model
swaps (0xA0). Time each new move on it: its hit pose on the base's first
hit frame, held to the last, recovered by the end.

## 3. Posing (`moves.py`)

`ctx.pose("Wait1", 0)` is a pose at a base animation's frame; Poser
operations change it in model space (Y up, +Z front, +X the fighter's
left), parents first:

| Operation | Use |
|---|---|
| `aim(joint, child, dir)` | point a bone (upper arm to elbow, thigh to knee) along a direction; `blend` < 1 part way |
| `turn(joint, axis, deg)` | turn a joint about a model axis through it (waist yaw, hips pitch, head roll) |
| `spin(joint, axis_index, deg)` | add to a joint's own angle: whole-body spins (YRotN, index 1) and flips (XRotN, index 0); keys every <= 90 deg |
| `move(joint, offset)` | move a joint: the body up/down (XRotN), root motion (TransN, `translate=[1]`) |
| `stretch(joint, child, f)` | lengthen a limb for a pose (the next animation resets it) |

`Move(motion, frames, keys=[(frame, pose)], script, translate=[], also=[],
root=False)`: keys are splined (Catmull-Rom); start and end on the stance
(ground) or the fall pose (air) so transitions are smooth. `root=True`
keeps the base animation's root motion for moves that travel by it (row
flags 0x80000000: dash attack, up B, ledge attacks) - without it an up B
rises nowhere.

**Proportions.** The base's limbs are the base's length: on a big-headed
model, arms raised straight up vanish into the head and forward punches
barely clear it. Aim raised arms out to the sides (a V), lean the body
into punches (waist yaw, hips pitch), and `stretch` an arm for a pose that
must read (a pointing pose). Check every move from the side and front.

**Preview** before the game: `build_moves.py ... --preview DIR` writes
pose dumps per frame; render every 4th on the model (`tools.md` §11).

## 4. Hitboxes (`ctx.rescript`)

`ctx.rescript(motion, [hit(...), ...])` keeps the base's script and
changes its hitbox commands in order: `bone` (a joint index: the hand,
foot, head, or 0 for a blast placed by `offset` = (forward, up, side)),
`damage`, `size` (radius, game units), `angle` (361 = Sakurai angle; 280
a meteor; 80-96 up; 45 diagonal), `kbg`, `bkb`, `wkb` (fixed knockback,
usually 0), `element` ("normal", "fire", "electric", "ice", "darkness",
"slash", "coin", "sleep"...). Fewer `hit`s than the base's hitboxes: the
last repeats; `None` removes one. Effects (0x28), model swaps (0xA0) and
sounds (0x44) of the base are dropped by default (`drop=`); keep the
base's damage totals near the base's unless the design says otherwise.
For blasts and projectiles-in-a-move, spawn effects at the hitbox in C
(below). On joint 0 the offset scales with the fighter (x1.1 Mario).

## 5. Special moves (C)
### How the game calls them

`src/melee/ft/ftcustom.h`:

```c
typedef struct FtCustomMoves {
    const char* name;                       /* fighter.cfg "moves <name>" */
    void (*special_n)(Fighter_GObj*);       /* entered instead of the base's */
    void (*special_s)(Fighter_GObj*);
    void (*special_lw)(Fighter_GObj*);
    void (*special_hi)(Fighter_GObj*);
    void (*special_air_n)(Fighter_GObj*);
    void (*special_air_s)(Fighter_GObj*);
    void (*special_air_lw)(Fighter_GObj*);
    void (*special_air_hi)(Fighter_GObj*);
    void (*on_frame)(Fighter_GObj*);        /* every frame (procCollPos) */
    void (*on_reset)(Fighter*);             /* every (re)spawn */
} FtCustomMoves;
```

ftCustom_Init replaces the base kind's 8 special entries with shims; each
shim calls the custom fighter's function when the fighter wears that pack's
costume, else the base's own. So the moves are per fighter, not per kind.

### Adding a fighter's specials

1. `src/melee/ft/kinds/ft<Name>/ft<name>.c` (copy
   `src/melee/ft/kinds/ftGeno/ftgeno.c` as the worked example), exporting
   `const FtCustomMoves ft<Xx>_Moves = { "<Name>", ... };`.
2. Add it to `cmake/game_sources.manifest` (sorted; the build refuses
   drift) and to `k_moves[]` in `src/melee/ft/ftcustom.c` (with an
   `extern const FtCustomMoves ft<Xx>_Moves;`).
3. `moves <Name>` in the pack's fighter.cfg. `validate.py` checks the name
   is compiled in.

### Writing a special

Reuse the base's **motion states** (its animations and hitbox scripts, by
`ftXx_MS_*` from `src/melee/ft/kinds/ft<Base>/forward.h`) and change what
happens on their events. The building blocks, all from the worked example:

- **Enter a motion:** `Fighter_ChangeMotionState(gobj, ftMr_MS_SpecialN,
  Ft_MF_None, 0, 1, 0, NULL); ftAnim_8006EBA4(gobj);` with
  `fp->cmd_vars[0] = 0; fp->throw_flags = 0;` first, as the base's enter
  functions do. Read the base's own `ft<base>special*.c` for what its enter
  sets.
- **Act on the animation's event frame:** set `fp->accessory4_cb` to your
  function; the motion's subaction sets `fp->throw_flags_b0` on its "throw"
  frame (where Mario's fireball spawns). Clear the flag and act.
  The base's ground<->air transitions reinstall *its* callback: in
  on_frame, put yours back (`if (fp->accessory4_cb == <base's>) ...`).
- **Projectiles:** spawn the base's own article (`Item_80268B18` with a
  `SpawnItem` for its kind, e.g. `It_Kind_Mario_Fire`), then override the
  item's `x40_vel`, `xD44_lifeTimer`, scale, and its `physics_updated` /
  `collided` callbacks for new flight. Its hitbox (damage, knockback) is
  the article's.
- **Projectiles that are not the base's:** the common items' shots load in
  every match (ItCo), so any fighter can fire them, owner = the fighter:
  the Ray Gun's ray `it_80298168(gobj, &pos, fp->facing_dir)`
  (`itlgunray.h`, a laser), the Star Rod's star `it_802988E4(gobj, &pos,
  1, fp->facing_dir)` (`itstarrodstar.h`, a big spinning star; 0 the small
  one). Their look and hitbox are the item's; a projectile that looks like
  the base's (Mario's fireball) makes the fighter feel like the base.
- **Hit effects:** the base's hitboxes are its own scripts, but their
  element can change before they are checked: in on_frame, walk
  `fp->x914[0..3]` (`HitCapsule`) and set `element` (`HitElement_Fire`,
  `_Electric`, `_Coin`, ... in `lb/forward.h`). Electric adds Melee's
  extra hitlag: it is simulation, deterministic, and changes the feel.
- **Voice:** `voice none` in fighter.cfg silences the base's voice clips
  (`ftCustom_Mute`, the metal-fighter flag), no C needed.
- **Buffs and armour:** `fp->dmg.armor0` (knockback resistance) held from
  on_frame; colour flash `ftCo_800BFFD0(fp, colanim, false)`.
- **State:** a static array indexed by `fp->player_idx` (0..5); reset in
  on_reset. Never per-kind state: two of the same fighter can be in a match.

Specials run on the base's motion states; their animations are the rows
motions.bin replaced (`Special*` in `moves.py`), so the special looks like
the character while the base's state code (gravity, landing, ledge grab,
the throw frame) still runs it. Keep the base's script there (its throw
frame is when your C acts) and `root=True` on recoveries.

Toolbox (all used by `src/melee/ft/kinds/ftGeno/ftgeno.c`, the worked
example):

| Want | Use |
|---|---|
| a shot | common items' projectiles (Ray Gun ray, Star Rod star), owner = the fighter |
| sparks / effects on the body | `efSync_Spawn(Ef_Id_..., NULL, &pos)` at a part's position (`ftParts_GetBone` + `lb_8000B1CC`) |
| a screen flash | `lbBgFlash_80021C48(4, 0)` |
| a colour flash or glow | `ftCo_800BFFD0(fp, colanim, false)` (15 electric flash, 53 charge glow) |
| a buff | `fp->dmg.armor0` held from on_frame; raise new hitboxes' damage once |
| element on the base's hits | set `fp->x914[i].element` in on_frame |
| a pose with no attack | enter the taunt (`ftCo_800DEBD0`) and act on it |
| a move's lightning that lasts | sparks every few frames on the body's bones while the motion runs |

- **Parts lookups:** `ftParts_GetBone(fp, part)` returns NULL for a part the
  base has no bone for; never index `fp->parts` with
  `ftParts_GetBoneIndex` unchecked (it returns -1: a crash).
- **Effects at hitboxes:** `HitCapsule.x4C` is the hitbox's position from
  its second frame on (zero on its first): spawn effects there
  (`efSync_Spawn(Ef_Id_Unk1001, NULL, &hit->x4C)`, electric sparks) to make
  a pack attack's blast visible.

## Determinism rules (online play replays inputs)

Moves are **simulation**: both machines must compute identical results.

- No `rand()`, time, or floating-point library calls not routed through the
  game's own (`src/pc/libm/pc_trig.h` macros; plain + - * / are fine).
- No state outside the fighter/item that is not reset deterministically.
- Read nothing from files at match time; everything comes from the pack's
  hashed bytes or the code.
- No dependency on render/presentation (camera, interpolation).

## Verification

1. **Previews**: every move's hit pose rendered on the model (front and
   three-quarter): it reads as the designed move, nothing hidden in the
   head or cape, nothing of the base's pose.
2. **validate.py**: no "motions.bin leaves the base's" warning (every
   attack, the taunt and every special have their own animation).
3. **Scripted test** (`play.py`, `tools.md` §9): every attack, special and
   the taunt, grounded and in the air, both directions, an idle opponent
   in range (`8:0h`). Screenshots show the moves; the opponent's damage
   climbing proves the hitboxes; no crash (play.py prints the crash log).
   Tilts need the stick held before A (a separate key line), smashes the
   C-stick (I/J/K/L) or stick+A together.
4. **CPU match**: level 9 CPUs use everything: `preview.py` and a 60 s
   match, no crash.
5. **Determinism**: `interp_determinism.py` with two of the fighter and two
   others (`MELEE_DEBUG_VS_CHARS="Xx:0,8:0,Xx:1,2:0"`): every row PASS -
   on copies of the build and pack folder (rebuilding mid-run spoils it).
6. **Online**: `tools/online_test.py` with the fighter picked on both
   sides (Mac-Mac; Mac vs the Windows build under Wine, docs/testing.md):
   PASS. motions.bin is part of the content hash: both sides need the same.
