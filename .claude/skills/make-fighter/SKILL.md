---
name: make-fighter
description: Make a new custom fighter for Melee 4K (any character) from a 3D model of it (or reference images) - the model on an existing fighter's skeleton as two costumes (the model as given and a Melee-ified one), a completely original moveset researched from the character's games and media (new animations, hitboxes, specials, taunt), a character select icon in the same style as the others, a pixel-art stock icon - packaged as a fighter pack the game loads. Use when asked to add, build, model or improve a custom character.
---

# Make a custom fighter

A custom fighter is an existing fighter (its **base**) wearing new costumes:
the base's skeleton, physics and timing, with a new model, textures, menu
images, name, its **own animations and hitboxes for every attack**, its own
special moves (C) and attributes. The game side is done
(`src/pc/fighter_pack.c`, `src/melee/ft/ftcustom.c`, `docs/fighters.md`);
making a fighter is a pack folder (model, menus, `motions.bin`) plus a C
file for its specials.

The usual request: the user names a character and **provides a 3D model**
of it (a game rip or their own: .dae, .fbx, .obj, .glb with textures).
This file is the whole procedure from that model to a finished, verified
fighter. Without a model, see "No model" at the end.

## Three requirements (the user's, non-negotiable)

1. **Costumes: exactly two** - costume 1 (Nr, the default) is the
   **Melee-ified** model (fewer triangles, Melee-style shading painted into
   its textures); costume 2 is **the provided model as given** (its mesh
   and textures unchanged). No recolours unless asked.
2. **A completely original moveset**, based on the character's
   appearances in other games and media: every jab, tilt, dash attack,
   smash, aerial, special and the taunt is the character's own animation
   and hitboxes. Nothing of the base's moves may show (its timing may
   stay). Research first, then build (`moves.md`).
3. **The character select icon in the same format and style as the
   others**: the face filling the window, the name in the same font,
   warm grey colour, size and spacing on the same black-and-dark-red bar,
   the same frame (`build_ui.py` does this; compare with the neighbours
   on character select with the HD pack loaded, `checklist.md`).

Also standard (the user asked for each before): the name in title case
(`Geno`, like "Mario" on the door; the icon draws it upper-case); a
hand-drawn **pixel-art stock icon**; a silent fighter unless it has a
voice of its own (`voice none`); the model never see-through, limbs never
fused, nothing of the base showing (metal box, costume emblems).

## Companion files

- `tools.md` - every tool, exact commands, setup and debugging (Blender,
  assimp, the repo's `tools/fighter/*`, the game's test knobs, gates).
  **Check setup with §1 before starting.**
- `moves.md` - designing and building the moveset: research, the base's
  frame data, posing, hitboxes, specials in C, determinism.
- `checklist.md` - the review checklist; every line before calling it done.
- `templates/model/` - copied into the character's folder: `make_pack.sh`
  (one command: model, costumes, menus, moves, pack, validation; also
  `--inspect` and `--check`), `fit.py` (bone map), `moves.py` (the
  moveset), `make_stock.py` (stock icon), `notes.md` (design notes).
- `templates/no-model/` - the primitives route (no model).

## Where things live

Never put the character's art or model in the repo (someone's IP; ask
before publishing or uploading anything). Per character, outside the repo:

```
~/Melee4K-dev/fighters/
  base/<Base>/        export_base.py output: PlXx.dat, PlXxNr.dat, PlXxAJ.dat, skeleton.json
  <Id>/               the character's working folder
    model/            the user's model files (and model.glb, converted)
    notes.md fit.py moves.py make_stock.py make_pack.sh   (from templates/model/)
    poses/            game pose dumps (MELEE_DUMP_POSE): idle, run, jab, smashes, jump, aerial...
    tex/ ui/ mesh_*.json   generated
  packs/<Id>/         the pack the game loads (MELEE_FIGHTERS_DIR=.../packs)
    fighter.cfg  Pl<Id>Nr.dat  Pl<Id><C2>.dat  Pl<Id>Ui.dat  motions.bin  textures/
```

The repo holds only generic tools (`tools/fighter/`), engine code, and the
C for the character's specials (`src/melee/ft/kinds/ft<Name>/`, no art).

## Steps

1. **Setup** (`tools.md` §1): Blender 4.2+ (5.x fine), `brew install
   assimp`, the repo venv, the disc path, the game built
   (`cmake --build build/macos --target melee`).
2. **Inputs.** Name (title case) and a two-letter **id** (`tools.md` §8:
   free ids); the model files into `<Id>/model/`; read its readme
   (credits; keep them). Pick the **base** by body and how it moves
   (table below). Copy `templates/model/*` into `<Id>/` and fill the
   variables at the top of `make_pack.sh`.
3. **Export the base** once: `export_base.py DISC <Base> base/<Base>`
   (with its animations, PlXxAJ.dat). Print its joints
   (`base_joints.py`) and frame data (`frame_data.py`).
4. **Inspect the model**: `./make_pack.sh --inspect` lists objects,
   meshes, materials/textures and every bone with its position and how
   many vertices it skins. Render it (`tools.md` §6b) front/side/back: up
   axis, facing, spare parts parked away from the body (alternate hands,
   faces), accessories on loose bones. Write `notes.md` "Source model".
5. **Fit it** (`fit.py`, `tools.md` §6b): map its bones onto the base's
   joints (hips, chest, head, arms, fingers, legs), drop spare parts,
   pick SCALE. Dump the base's poses into `poses/` (`base_poses.py
   base/<Base> <Id>/poses --base <Base>`: idle, run, jab, smashes, jump,
   aerials, from its own animations, no game needed) and
   `./make_pack.sh --check`: look at every `ui/check_*.png`. No spikes, no
   parts through the face, feet on the ground, hands where the base's
   hands are. Iterate. A model with no skeleton: `blender_autorig.py`
   first (`tools.md` §6d). Non-humanoids (a dog, a ghost, a bean): see
   "Fitting any body" in `tools.md` §6b.
6. **Costumes** (requirement 1): `make_pack.sh` builds both: Nr = the
   mesh decimated (`LO_RATIO` = ~5000 / the model's triangles: a 27k-
   triangle model needs 0.18, a 7k one 0.6) with `melee_texture.py`
   textures (the model's AO and normal maps painted in, or AO baked when
   it has none); the second = the model's own mesh and textures.
7. **Menu images**: the portrait renders in an attack's game pose (`CSP`,
   the face visible), the icon from the face; `build_ui.py --name`
   formats both exactly like the HD pack's (requirement 3). Draw the
   stock icon (`make_stock.py`: 24x24 pixel art of the head, colours
   sampled from the textures).
8. **Moveset** (requirement 2, `moves.md`): research the character
   (games, anime, comics: its attacks, spells, weapons, signature
   poses), fill `notes.md` "Moveset research" for every slot, then pose
   each move in `moves.py` (`moveskit.py`: arms, legs, kick, punch,
   hit, attack, special, taunt, family) on the base's frame data, covering
   every Attack/Appeal/Special row the base has (`validate.py` lists the
   ones left), and write the specials' C (`src/melee/ft/kinds/ft<Name>/`:
   mostly data, `FtCustomSpecial` + `FtCustomFx`, `moves.md` §5).
   Set `MOVES` when NAME has spaces, `ORDER` for its place on character
   select, the attribute multipliers in the cfg block.
9. **Build the pack**: `./make_pack.sh` (model, both costumes, menus,
   motions.bin, stock, fighter.cfg, `validate.py`).
10. **Check in the game** (`tools.md` §9, `checklist.md`): `closeup.py`
    both costumes; `play.py` every attack, special and the taunt, grounded
    and aerial, against an idle opponent (its damage must climb; no
    crash); character select (icon beside its neighbours, door name,
    portrait, both costumes, no other fighter's emblem); HUD stock icon;
    metal box; results screen.
11. **Gates** (`tools.md` §10): determinism with the fighter (every row
    PASS), soak with the pack (N/N), online with it picked. Run them on
    copies of the build and the pack (a rebuild mid-run spoils them).
12. **Report** to the user with images: both costumes in the game, the
    character select row with its neighbours, a sheet of every move at
    its hit, the honest list of approximations. Commit the repo side
    (tools, C, skill), never the art.

| Body / feel | Base (code) |
|---|---|
| small, round, all-rounder | Mario (Mr), Dr. Mario (Dr), Luigi (Lg) |
| small, light, fast | Pikachu (Pk), Kirby (Kb), Jigglypuff (Pr), Ness (Ns) |
| medium humanoid, sword | Marth (Ms), Roy (Fe), Link (Lk), Young Link (Cl) |
| medium, fast, gunner | Fox (Fx), Falco (Fc) |
| tall, heavy | Ganondorf (Gn), Captain Falcon (Ca), Bowser (Kp), DK (Dk) |
| slender, floaty | Zelda (Zd), Peach (Pe), Samus (Ss), Mewtwo (Mt) |

The base's bones are fixed: the model is fitted to *its* proportions
(limb lengths, joint positions), so a model with a big head and short
arms on Mario looks like itself, while long-limbed poses need help
(`moves.md` "Proportions"). Hitboxes and hurtboxes stay the base's
(scaled by its attributes); a model drawn larger than the base looks
larger only.

## Lessons from earlier fighters (do not relearn them)

- Image-to-3D and primitive models were rejected by the user ("fucked
  up", see-through, fused arms); a real model fitted bone by bone is what
  worked. Use the provided model; keep its mesh, do not rebuild it.
- Blender 5 has no COLLADA import: convert with assimp. assimp's .dae
  output can leave meshes lying down relative to the bones
  (`SRC_MESH_ROT_X = 90`).
- UVs outside the 0-1 tile look right in Blender and wrong in the game
  (the game clamps): the tools wrap them; check textures in the game.
- One-key constant animation tracks collapse a joint to scale 0 in the
  game; the tools write two. Joints the game never animates (zero
  matrices in pose dumps) must not get weight.
- Anything indexing `fp->parts` by `ftParts_GetBoneIndex` crashes on a
  part the base lacks (-1): use `ftParts_GetBone`.
- The base's script commands that swap its model parts (0xA0) hide parts
  of the new model; the base's effects (0x28) and sounds (0x44) and odd
  ones (Mario's taunt grow, 0xB8) give the base away: drop them.
- Raised arms vanish into a big head on a short-armed base: aim them out
  to the sides (a V), or `stretch` the limb for that pose.
- A custom costume's metal form and character select emblems used to
  show the base: both fixed in the engine; check them anyway.
- Several meshes sharing one material used to overwrite each other in
  the retarget (only the last survived): it joins them first now.
- Parts skinned to a second root (eyes, teeth, collar on their own
  bones at the origin): `FOLLOW` them to the bone they sit on. Parts with
  no material at all (teeth): they get "Plain"; colour it in
  `TEX_COLORS`. A material with no image: `TEX_ASSIGN` it the right file
  or a `TEX_COLORS` flat colour (look at the render: a wrong guess paints
  a ghost maroon).
- Rigid shells (an animatronic's torso) mapped as segments get stretched
  to the base's limb lengths: map them by position only (a plain joint
  name) and nudge with `OFFSET`.
- Smooth single-surface bodies (a bean) ripple where the weights blend
  at 0.1 steps: `WEIGHT_STEP = 0.025`.
- A base's sword hitboxes sit along its sword joint: a weaponless model
  on a sword base gets every hitbox put on its limbs (`k.hit` sets the
  bone and a zero offset), and a model that should hold a weapon the
  model lacks gets one built in `fit.py` on the sword joint (a hilt and
  a blade, rigid on it) with hitboxes along the joint's axis (read the
  axis off the base's own sword hitboxes).
- Base motion names differ (Wait/Wait1, SquatWait/SquatWait1,
  AppealR/Appeal, AttackS4/AttackS4S, Marth's AttackS31): build_moves
  resolves aliases and writes each row under its real name (the game
  matches exactly); `k.family("SpecialHi")` covers every row of a special.
- Three crashes only a match finds: a costume past the base's retail
  ones indexing the base's per-costume tables (Jigglypuff's hats hung the
  load: check `ft<Base>*.c` for `[fp->costume_id]` tables and bound
  them); a base whose costumes need an eye animation (Falcon, Ganondorf,
  Samus: "Cannot find symbol ..._Share_matanim_joint"; build_dat writes an
  empty one); a stray return (0x18) left in a flattened script (a
  segfault in Command_06 on that move; read_script ends there now and
  validate.py refuses one). Play every pack before calling it done.
- Hits must be checked against an idle foe at close range (the step test
  in `checklist.md`): a fixed walk overshoots or falls short with every
  walk speed, and a level 9 CPU proves nothing when it wins every exchange.
- Read a move's direction off the side view of a check render, and
  measure rather than guess a whole-body pitch (the base's fall pose may
  already lean: print hips->head for a few angles with `Poser.world()`).
- Editing a script that a running bash loop is executing can corrupt
  that run; and never splice files by the first match of a common word
  (`index("weight ")` hit Python code): match whole lines.

## No model

If there is no model, ask for one first (The Models Resource, the user's
files). Otherwise: reference images through image-to-3D (`tools.md` §6,
Hunyuan3D-MLX + `blender_fit_ai.py`) or primitives (`templates/no-model/`,
`tools.md` §5). Both give a weaker likeness; say so in the report. The
moveset, icon, stock and costume rules stay the same (costume 2 is then
the unstyled model).

## Rules

- Art, models and packs stay out of git. Ask before uploading or releasing
  a pack, and before outward actions (releases, PRs).
- Nothing in the repo names a specific character except its specials
  (`src/melee/ft/kinds/ft<Name>/`) and its `k_moves` entry in ftcustom.c.
- Keep the base's skeleton exactly (`validate.py --base-dir` compares it).
- Look at every render and screenshot before moving on.
