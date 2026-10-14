# Fighter review checklist

Go through every line with images in front of you (renders, `preview.py`
sheets, in-game screenshots). Note anything approximate in the report.

## Model
- [ ] Silhouette matches the references from front, side, back (blender_snap, 4 views side by side with the references).
- [ ] Head/body ratio and the character's signature parts (hat, cape, hair, weapon, tail...) read at Melee's camera distance (check a `preview.py` frame at full size, not just the sheet).
- [ ] Nothing hangs inside out (one-sided faces seen from behind): sheets solidified (capes, collars, ribbons).
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
- [ ] Every costume loads in a match (`preview.py --costumes 0,1,...`): a wrong symbol crashes on load.
- [ ] Team colours: red, blue, green costumes exist and `teams` in fighter.cfg names them if the blue one is not "Bu".
- [ ] Costumes are distinguishable at a glance in a 4-player match.

## Menus and screens
- [ ] Character select: the icon shows (to the right of the bottom row), picking it shows the portrait and the name; cycling costumes (X/Y) changes the portrait; random never picks it by accident.
- [ ] Icon: the face, as big and crisp as the Melee HD icons beside it (HD replacement in textures/), the name on the bar.
- [ ] Portrait: big in the door like Melee's (upper body, the character's own pose), every costume.
- [ ] CUSTOM switch (left of the top row): OFF hides the icon, ON shows it.
- [ ] HUD stock icon: the head, readable at 24x24, every costume.
- [ ] Results screen: winner banner and name plate say the name; the KO table's stock icon beside "P1" is the fighter's; the panel shows the 3D model.
- [ ] Pause and announcer: no announcer call for the custom fighter (expected), no crash.

## Moves and attributes (if any)
- [ ] Every special works grounded and aerial, facing both ways, and hits (scripted test, tools.md §9).
- [ ] Specials fall back to the base's for other players of the same base fighter (a retail Mario next to the custom one still has Mario's moves).
- [ ] State resets on death/respawn (on_reset).
- [ ] Two of the same custom fighter in one match keep separate state.
- [ ] Attributes feel distinct but sane (0.5-2x).
- [ ] It does not feel like its base: its voice (`voice none` if it should be silent), projectiles and hit effects are its own.

## Gates
- [ ] `validate.py --base-dir --disc`: OK.
- [ ] `interp_determinism.py` with the fighter: every row PASS.
- [ ] Online test with the fighter picked: PASS; and with a peer without the pack: base fighters only.
- [ ] The pack and art are not in git (`git status`).
