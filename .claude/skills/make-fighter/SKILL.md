---
name: make-fighter
description: Make a new custom fighter for Melee 4K (any character) from reference images - a Melee-style low-poly model on an existing fighter's skeleton, HD textures, costumes, menu images, optional special moves - packaged as a fighter pack the game loads. Use when asked to add, build, model or improve a custom character.
---

# Make a custom fighter

A custom fighter is an existing fighter (its **base**) wearing new costumes:
the base's skeleton, animations, hitboxes and (by default) moves, with a new
model, textures, menu images, name and optionally its own specials and
attributes. The game side is done (`src/pc/fighter_pack.c`,
`src/melee/ft/ftcustom.c`, `docs/fighters.md`); making a fighter is art plus a
pack folder, and C only for new special moves.

Companion files, read them as you reach each step:

- `tools.md` - how to install, run and debug every tool used here (Blender,
  MCP for Blender, AssetFurnace, the repo's `tools/fighter/*`, the game's test
  knobs). **Check setup with it before starting.**
- `checklist.md` - the review checklist; go through it before calling a
  fighter done.
- `moves.md` - writing the fighter's own special moves (C), attributes, and
  the determinism checks they need.
- `templates/` - starting points copied into the character's folder:
  `build_character.py` (the Blender model script), `make_textures.py`,
  `make_pack.sh` (one command: model, images, pack, validation).

## Inputs

Ask for (or take from the request) only what is missing:

1. **Name** as shown in menus (`GENO`, upper-case like Melee's) and a
   **two-letter id** (`Ge`): capital + lower-case letter, not used by the
   disc's `Pl??.dat` files (`validate.py --disc` checks) or another pack.
   Taken on the NTSC 1.02 disc: Bo Ca Ch Cl Co Dk Dr Fc Fe Fx Gk Gl Gn Gw Kb
   Kp Lg Lk Mh Mr Ms Mt Nn Ns Pc Pe Pk Pp Pr Sb Sk Ss Ys Zd (Gn is
   Ganondorf), plus every other pack's id.
2. **Reference images**: front at least; back and three-quarter if
   possible; one per costume colour if they exist. Clean background helps.
3. **Base fighter**: pick by body and moveset (table below) unless given.
4. Optional: a **moves sheet** (what each special does), costume colours.

| Body / feel | Base (code) |
|---|---|
| small, round, all-rounder | Mario (Mr), Dr. Mario (Dr), Luigi (Lg) |
| small, light, fast | Pikachu (Pk), Kirby (Kb), Jigglypuff (Pr), Ness (Ns) |
| medium humanoid, sword | Marth (Ms), Roy (Fe), Link (Lk), Young Link (Cl) |
| medium, fast, gunner | Fox (Fx), Falco (Fc) |
| tall, heavy | Ganondorf (Gn), Captain Falcon (Ca), Bowser (Kp), DK (Dk) |
| slender, floaty | Zelda (Zd), Peach (Pe), Samus (Ss), Mewtwo (Mt) |

The base's bones are fixed: the model is built to *its* proportions (limb
lengths, joint positions), scaled in feel by the model's thickness, not by
moving joints. A character much taller than the base reads as a different
size only visually; hitboxes stay the base's.

## Where things live

Never put the character's art in the repo (it is usually someone's IP; ask
the user before publishing anything). Per character, outside the repo:

```
~/Melee4K-dev/fighters/
  base/<Base>/            export_base.py output (from the user's disc)
  <Id>/                   the character's working folder
    reference/            the reference images
    notes.md              design notes: shapes, colours, proportions, costumes
    make_textures.py      its textures (from templates/)
    build_<name>.py       its Blender model (from templates/)
    make_pack.sh          rebuilds everything (from templates/)
    tex/ ui/ mesh.json    generated
  packs/<Id>/             the pack the game loads (MELEE_FIGHTERS_DIR=.../packs)
    fighter.cfg  Pl<Id><Costume>.dat ...  Pl<Id>Ui.dat  textures/
```

The repo holds only generic tools (`tools/fighter/`), engine code, and C
for special moves (`src/melee/ft/kinds/ft<Name>/`), which contains no art.

## Steps

1. **Setup.** `tools.md` §1: Blender, the repo venv, the disc path. MCP for
   Blender is optional (live look at the scene); the headless route needs
   only Blender.
2. **Export the base** once per base fighter: `export_base.py DISC <Base>
   ~/Melee4K-dev/fighters/base/<Base>` (`tools.md` §3).
3. **Study the references** and write `notes.md`: silhouette, head/body
   ratio, every part (hat, cape, armour...), colours (sample them from the
   images with PIL), materials, what each costume changes. Look at the
   images; do not guess.
4. **Model.** Two routes (`tools.md` §4-§6):
   - **Primitives in Blender** (works everywhere, what Geno used): copy
     `templates/build_character.py`, build each part with
     `tools/fighter/blender_parts.py` (ellipsoid, limb, tube, ring_grid) at
     the base skeleton's joint positions (`jp(i)`), weighted to its bones.
     Melee-like: 1.5-4k triangles total, big readable shapes, painted
     detail in the textures.
   - **AssetFurnace** image-to-3D, then fit and weight in Blender: better
     for organic shapes; needs full Xcode on a Mac and ~10 GB disk.
   Render check views (`blender_snap.py`) front/side/back/three-quarter and
   compare with the references side by side. Iterate until the silhouette
   and proportions match.
5. **Textures.** Copy `templates/make_textures.py`: one PNG per material,
   2048² for large parts (they become HD replacements; the game file gets a
   128² copy). UV conventions: see the template header. Faces, emblems and
   seams are painted, not modelled.
6. **Costumes.** 2-6, Melee-like: the default plus team colours (Re, Bu or
   whichever costume is blue, Gr) and one or two alternates. Each is a set
   of material overrides (material name -> PNG). Name them with Melee's
   suffixes: Nr (default), Re, Bu, Gr, Ye, Bk, Wh, Or, Pi, La, Aq.
7. **Menu images.** `blender_render_ui.py` renders the portrait, icon and
   stock icon per costume; `build_ui.py --name` adds the results screen's
   banner and name plate.
8. **Pack.** `make_pack.sh` (from the template) runs steps 4-7 headless and
   writes the pack: `build_dat.py` per costume (symbol stem plus the costume
   suffix except for Nr), `build_ui.py`, `fighter.cfg`, then `validate.py`.
   fighter.cfg keys: `tools.md` §8.
9. **Check in game** (`tools.md` §9): `preview.py` (matches, every
   costume), then character select, HUD stock icon, results screen. Use
   the `checklist.md`. Fix and rebuild until it passes.
10. **Moves** (if the character should play differently): `moves.md`.
    Attributes alone (weight, speed, jump...) need no C: fighter.cfg.
11. **Gates.** Determinism with the fighter in the match, online with it
    picked (`moves.md` §Verification, `tools.md` §10). Moves and attributes
    are simulation; art is not.
12. **Report** to the user with screenshots: in-match sheet, character
    select, results, and the honest list of what is approximate.

## Rules

- Art and packs stay out of git. Ask before uploading or releasing a pack.
- Nothing in the repo names a specific character except its moves code
  (`src/melee/ft/kinds/ft<Name>/`) and its entry in ftcustom.c's `k_moves`.
- Keep the base's skeleton exactly (`validate.py --base-dir` compares the
  bind pose): animations and hitboxes depend on it.
- Look at every render you make before moving on; compare with references.
