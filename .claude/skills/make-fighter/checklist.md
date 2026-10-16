# Fighter review checklist

The three requirements first:
- [ ] **One costume**: Nr, the provided model with its own textures; nothing restyled.
- [ ] **Original moveset**: notes.md names a source for every slot; validate.py reports no motion left the base's; nothing of the base's moves shows in play.py sheets.
- [ ] **Character select icon** indistinguishable in format from its neighbours (font, warm grey, size, spacing, bar, frame), checked in the game with the HD pack.

Go through every line with images in front of you (renders, `preview.py`
sheets, in-game screenshots). Note anything approximate in the report.

## Model
- [ ] Silhouette matches the references from front, side, back (blender_snap, 4 views side by side with the references).
- [ ] Head/body ratio and the character's signature parts (hat, cape, hair, weapon, tail...) read at Melee's camera distance (check a `preview.py` frame at full size, not just the sheet).
- [ ] In the game, not only in Blender's renders: hair cards and thin sheets (ears, robe edges) are there (back faces are culled, alpha under half is cut), and every texture is the right way up and from the right file (the face on the face).
- [ ] Size against the base roster: a character as big as another fighter in its games is that big in a match (fighter.cfg `size`, from the base's model scale; `PlXx.dat` attributes +0x8C).
- [ ] Nothing hangs inside out or is see-through (one-sided faces seen from behind): sheets solidified (capes, collars, ribbons); textures sample the right places in the game, not only in Blender (UVs in the 0-1 tile).
- [ ] Limbs are separate (arms not fused to the body) and every part keeps its shape in the base's poses (game pose dumps): no spikes from joints the game never animates.
- [ ] Triangles 5-20k (`validate.py` prints the count; Melee's own are 0.8-6k).
- [ ] Up close in the game (`closeup.py`): face, hands, joints and every part from the front, sides, back and above.
- [ ] Every vertex weighted, weights sum to 1 (`validate.py`).
- [ ] Deformation: shoulders, elbows, hips, knees in run, jump, smash attacks, crouch, KO tumble (in-game frames or posed Blender renders). Capes/skirts follow the torso, not the arms only.
- [ ] Scale against the base: feet on the ground (shadow under them), not floating or sunk.
- [ ] Eyes: the base's eye material slots are kept by build_dat.py; if the base blinks by texture animation, the new face texture does not need to.

## Textures
- [ ] Every material has a texture; HD ones in `textures/` (validate.py).
- [ ] Shading: baked occlusion gives depth (under the hat, inside the cape) with no dark blotches that move with the limbs (arms in AO_NO_CAST, ball joints and eyes with AO_FLOORS 1.0).
- [ ] Faces/emblems are at the front (u = 0.5) and the right height; not covering other features (compare with references).
- [ ] No visible seams at u = 0/1 on the back of round parts.
- [ ] Colours sampled from the references, not guessed.

## Costumes
- [ ] Only the costumes asked for. Every costume loads in a match (`preview.py --costumes 0,1,...`): a wrong symbol crashes on load.
- [ ] Character select: cycling costumes shows no other fighter's emblem or picture behind the door.
- [ ] Team colours, if there are team costumes: `teams` in fighter.cfg names them if the blue one is not "Bu".
- [ ] Costumes are distinguishable at a glance in a 4-player match.

## Menus and screens
- [ ] Character select: the icon shows (to the right of the bottom row), picking it shows the portrait and the name (title case, like "Mario"); cycling costumes (X/Y) changes the portrait; random never picks it by accident.
- [ ] Icon: the face, as big and crisp as the Melee HD icons beside it (HD replacement in textures/), the name on the bar exactly like its neighbours' (Roy, Link: same font, warm grey, size, spacing, dark red bar) - check on character select with the HD pack loaded.
- [ ] Portrait: big in the door like Melee's (upper body, the character's own pose), every costume.
- [ ] CUSTOM switch (left of the top row): OFF hides the icon, ON shows it.
- [ ] HUD stock icon: the head render through `stock_icon.py`, the face readable at 24x24 and in HD, every costume.
- [ ] Metal box (items on): the metal fighter is the custom model, not the base's.
- [ ] Results screen: winner banner and name plate say the name; the KO table's stock icon beside "P1" is the fighter's; the panel shows the 3D model.
- [ ] Pause and announcer: no announcer call for the custom fighter (expected), no crash.

## Moves and attributes (if any)
- [ ] No crash in any special or attack, grounded and aerial, facing both ways (`play.py`).
- [ ] Every attack (jabs, tilts, dash attack, smashes, aerials) is the fighter's own animation and hitboxes, none of the base's (motions.bin, `play.py` sheets), and hits (opponent's damage climbs).
- [ ] Every special works grounded and aerial, facing both ways, and hits; effects that should last (a move wrapped in lightning) last the whole move.
- [ ] Specials fall back to the base's for other players of the same base fighter (a retail Mario next to the custom one still has Mario's moves).
- [ ] State resets on death/respawn (on_reset).
- [ ] Two of the same custom fighter in one match keep separate state.
- [ ] Attributes feel distinct but sane (0.5-2x).
- [ ] It does not feel like its base: its voice (`voice none` if it should be silent), projectiles and hit effects are its own.

## Hits (every fighter, every rebuild)

- [ ] Step test: `play.py DISC PACKS <Id> OUT "Right 140, wait 80, X 60,
      wait 900, Right 140, wait 80, Right+X 60, wait 900, ..."` (a short step,
      then a different attack, again and again) against the idle Mario:
      his damage climbs through the run (crop the HUD of a few frames).
- [ ] No crash with a CPU of the fighter (`MELEE_DEBUG_VS_CHARS="<Id>:0,8:0"`):
      it uses every move, the specials included, within a minute.
- [ ] `validate.py` passes (it refuses scripts with goto/call/return).

## Look (every fighter)

- [ ] Every special and signature attack shows its own effect in the game (close, `MELEE_TEST_CAM_ZOOM=3`): colours and shapes from the character, timed with the hit.
- [ ] No common spark effect, no electricity it does not have (hit elements chosen per move; the base's carried over nowhere).
- [ ] Projectiles drawn as its own (`shot` lines), not the Ray Gun's ray or the Star Rod's star.
- [ ] Nothing of the base's model or gear shows in any move (shield, scabbard, sword, hat) - watch idle, run, shield, item throws.
- [ ] Its body: idle, walk, run, turn, jump, fall, crouch and shield are its own when its silhouette differs from the base's.

## Gates
- [ ] `validate.py --base-dir --disc`: OK.
- [ ] `interp_determinism.py` with the fighter: every row PASS.
- [ ] Online test with the fighter picked: PASS; and with a peer without the pack: base fighters only.
- [ ] The pack and art are not in git (`git status`).

## In character beyond the attacks (moves.md "The parts of a fighter people notice")
- [ ] Grab and throws are the character's (no hookshot or grapple beam on Link/Samus bases).
- [ ] Jumps: the count (fighter.cfg `jumps`) and the animations are the character's, not Kirby's or Jigglypuff's.
- [ ] Up B rises 30-40 units, not off the top of the screen.
- [ ] Results screen (`MELEE_DEBUG_VS_STOCKS=1 MELEE_INSTANT_WIN=1`): Win and Lose poses are the character's own; nothing of the base's pose (clapping) shows; framed whole.
- [ ] Cloth, hair and tails move in every move; the jaw opens for bites and howls.
- [ ] Parts on hidden joints (tongue, helpers, another form) never show outside their moves, even when the move is interrupted by a hit; none in the menu images (`RENDER_HIDE`).
- [ ] Character select: picking it, and putting the coin back down on it, says nothing (the announcer has no name for it).
