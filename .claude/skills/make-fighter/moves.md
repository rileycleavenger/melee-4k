# A custom fighter's own moves

Without this, a custom fighter plays exactly like its base. Two levels:

1. **Attributes only** (no code): multipliers in fighter.cfg - `weight`,
   `walk`, `dash`, `jump` (jump and hop speed), `gravity`, `fall` (terminal
   and fast-fall speed), `air` (air drift). Applied where the game copies
   the base's attributes (`ftCustom_ApplyAttrs`, src/melee/ft/ftchangeparam.c).
2. **Special moves** (C, compiled in): neutral, side, down, up B, grounded
   and aerial, replaced individually; any left NULL stay the base's.

## How the game calls them

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

## Adding a fighter's moves

1. `src/melee/ft/kinds/ft<Name>/ft<name>.c` (copy
   `src/melee/ft/kinds/ftGeno/ftgeno.c` as the worked example), exporting
   `const FtCustomMoves ft<Xx>_Moves = { "<Name>", ... };`.
2. Add it to `cmake/game_sources.manifest` (sorted; the build refuses
   drift) and to `k_moves[]` in `src/melee/ft/ftcustom.c` (with an
   `extern const FtCustomMoves ft<Xx>_Moves;`).
3. `moves <Name>` in the pack's fighter.cfg. `validate.py` checks the name
   is compiled in.

## Writing a special

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
- **Buffs and armour:** `fp->dmg.armor0` (knockback resistance) held from
  on_frame; colour flash `ftCo_800BFFD0(fp, colanim, false)`.
- **State:** a static array indexed by `fp->player_idx` (0..5); reset in
  on_reset. Never per-kind state: two of the same fighter can be in a match.

Not yet supported: new animations or hitbox scripts (they would need new
figatree/subaction data in the pack). Moves are built from the base's.

## Determinism rules (online play replays inputs)

Moves are **simulation**: both machines must compute identical results.

- No `rand()`, time, or floating-point library calls not routed through the
  game's own (`src/pc/libm/pc_trig.h` macros; plain + - * / are fine).
- No state outside the fighter/item that is not reset deterministically.
- Read nothing from files at match time; everything comes from the pack's
  hashed bytes or the code.
- No dependency on render/presentation (camera, interpolation).

## Verification

1. **Scripted test** (`tools.md` §9): each special grounded and in the
   air, both directions; opponent idle (`8:1h` = a human slot with no
   input). Screenshots show the effect; damage climbing proves hits.
   A temporary `fprintf(stderr, ...)` in the event callback counts uses;
   remove it before committing.
2. **CPU match**: level 9 CPUs use specials constantly: run
   `preview.py` and a 60 s match for crashes.
3. **Determinism**: `interp_determinism.py` with two of the fighter and two
   others (`MELEE_DEBUG_VS_CHARS="Xx:0,8:0,Xx:1,2:0"`): every row PASS.
4. **Online**: `tools/online_test.py` with the fighter picked on both
   sides (Mac-Mac; Mac vs the Windows build under Wine, docs/testing.md):
   PASS.
