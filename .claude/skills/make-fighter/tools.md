# Tools for making a fighter

Paths below: `REPO` is the melee-4k checkout, `DISC` the user's Melee disc
image (`.iso`/`.ciso`, NTSC 1.02), `DEV=~/Melee4K-dev/fighters`, `ID` the
fighter's two-letter id, `BASE` the base fighter's code. Every command was
run as written while making the first custom fighter.

## 1. Setup check

```sh
/Applications/Blender.app/Contents/MacOS/Blender --version | head -1   # 4.2+ (5.x fine)
ls REPO/.venv/bin/python                                               # repo venv (PIL, numpy)
REPO/.venv/bin/python -c "import PIL, numpy; print('ok')"
ls REPO/build/macos/melee                                              # game built (docs/building.md)
df -h ~ | tail -1                                                      # Blender + packs: ~2 GB; AssetFurnace: +10 GB
```

Install what is missing:

| Missing | Install |
|---|---|
| Blender | `brew install --cask blender` (Linux: distro package or blender.org tarball; Windows: winget `BlenderFoundation.Blender`) |
| uv (for MCP for Blender) | `brew install uv` |
| repo venv | `python3 -m venv REPO/.venv && REPO/.venv/bin/pip install pillow numpy` |
| game build | `cmake -S REPO -B REPO/build/macos && cmake --build REPO/build/macos` |

`BLENDER=/Applications/Blender.app/Contents/MacOS/Blender` below.

## 2. Blender, headless (the main route)

Everything in this pipeline runs in Blender **without a window**: no clicks,
repeatable, and it is what `make_pack.sh` does.

```sh
$BLENDER -b --factory-startup --python job.py          # runs job.py, exits
$BLENDER -b --factory-startup --python-expr "import bpy; print(bpy.app.version)"
```

`--factory-startup` ignores user prefs and gives the default scene (with a
"Cube", "Light" and "Camera": delete them in the build script, or the Cube
shows in check renders and shades the bake). A job is a short Python file that `exec`s the repo's
Blender scripts in order; each reads its parameters from globals set first:

```python
DIR = "~/Melee4K-dev/fighters/base/Mr"              # (expanded path)
exec(open(f"{REPO}/tools/fighter/blender_import_base.py").read())
REPO = "..."; TEX = f"{DEV}/{ID}/tex"
exec(open(f"{DEV}/{ID}/build_<name>.py").read())        # model, paint materials
BAKE = {...}; COSTUMES = {...}; OUT = f"{DEV}/{ID}/baked"; bake_all()   # §5b
OUT = f"{DEV}/{ID}/mesh.json"
exec(open(f"{REPO}/tools/fighter/blender_export_fighter.py").read())
```

Print markers from the job (`print("built", ...)`) and grep the output:
Blender prints a lot. A Python error in a job prints `Traceback` and Blender
still exits 0; always grep for `Error|Traceback`.

## 3. Repo tools (`REPO/tools/fighter/`)

Run the Python ones with `REPO/.venv/bin/python`. Blender ones are `exec`ed
inside Blender with globals as shown.

| Tool | What | Use |
|---|---|---|
| `export_base.py DISC BASE OUT` | base fighter's skeleton, meshes and costume template from the disc | once per base: `OUT=$DEV/base/BASE` |
| `blender_import_base.py` | Blender: armature "Skeleton" (bones `J00`..), reference meshes `Ref_D..` | globals `DIR` (export folder) |
| `blender_parts.py` | Blender: modelling primitives on the skeleton | see §5 |
| `blender_bake.py` | Blender: procedural paint materials, then one baked HD texture per export mesh and costume (colour x ambient occlusion) | see §5b |
| `blender_snap.py` | Blender: quick solid render, framed on visible meshes | globals `OUT`, `VIEW` (front, side, back, threequarter), `HIDE` (object names) |
| `blender_export_fighter.py` | Blender: meshes parented to Skeleton (not `Ref_`) -> `mesh.json` | global `OUT`; materials need `hsd_texture` (blender_parts.material sets it) |
| `blender_render_ui.py` | Blender: portrait `csp<n>.png` (544x752), `icon.png` (640x560 face), `stock<n>.png` (144x144 head) with Cycles | globals `OUT`, `COSTUMES` (list of {material: png}), `POSE` ({bone: (x,y,z) degrees}: icon and stock), `CSP_POSE` (the portrait's pose; default `POSE`), `HEAD` (head's share of the height, 0.42), `SAMPLES` |
| `build_dat.py` | one costume file `Pl<ID><C>.dat` from mesh.json over the base's | `--base-dir $DEV/base/BASE --mesh mesh.json --symbol <stem> --out pack/Pl<ID><C>.dat --textures pack/textures [--texture MAT=PNG ...]` |
| `build_ui.py` | menu images `Pl<ID>Ui.dat` (disc sizes) and their HD replacements | `--icon icon.png --csp csp0.png ... --stock stock0.png ... --name "<Name>" --textures pack/textures --out pack/Pl<ID>Ui.dat`; `--name` sets the icon on a Melee HD-style plate (face cut by a grey frame, name on a dark bar) and gives portraits Melee's drop shadow |
| `validate.py PACK` | checks a pack (cfg, symbols, skeleton, weights, budgets, UI, textures) | `--base-dir $DEV/base/BASE --disc DISC`; exit 1 on errors |
| `preview.py DISC PACKS ID OUT` | CPU matches screenshotted into `OUT/sheet.jpg` | `--costumes 0,1,2 --stages 32,31,3 --frames 900` |
| `closeup.py DISC PACKS ID OUT` | the fighter idle in a match, paused: the pause camera zoomed in and turned round it, screenshots into `OUT/closeups.jpg` (frames `OUT/frame_*.png`, full 4K) | `--costume 0 --stage 32`; the in-game model, shading and textures up close |
| `dump_model.py` | prints a .dat's joints, DObjs, materials, vertex formats | `dump_model.py pack/Pl<ID>Nr.dat` or `dump_model.py DISC PlMrNr.dat` |
| `hsd.py`, `model.py` | HSD archive reader/writer, mesh decode | libraries |

**Symbols.** fighter.cfg `symbol` is a stem (`PlyGeno5K`). The game looks
up `<stem>_Share_joint` for costume Nr and `<stem><C>_Share_joint` for every
other costume (`PlyGeno5KRe_Share_joint`), as the disc does. So
`build_dat.py --symbol` is the stem for Nr and stem+suffix otherwise; a
wrong one crashes the game when that costume loads ("Cannot find symbol",
lbarchive.c PANIC) and `validate.py` reports it.

**Textures.** `build_dat.py` stores each material's PNG at 128x128 in the
.dat (RGB565, RGB5A3 with alpha) and the full-size PNG in `--textures` named
`tex1_128x128_<hash>_<fmt>.png`, which the game swaps in. `build_ui.py
--textures` does the same for the menu images (`tex1_64x56_..._6.png` for
the icon and so on). Delete `pack/textures` before a rebuild so old ones do
not pile up.

## 4. Base skeleton facts

After `blender_import_base.py`: bones `J00`..`Jnn` in depth-first order,
each with custom property `hsd_world` (bind matrix). Game space: **Y up, +Z
the fighter's front, its left at +X**. Units: Melee's (Mario is ~13 tall).

Get joint positions to design against (in a job):

```python
exec(open(f"{REPO}/tools/fighter/blender_parts.py").read())
for i in range(len(skel.data.bones)):
    print("JOINT", i, tuple(round(c, 2) for c in jp(i)))
```

Which joint is what: look at `Ref_` meshes with `blender_snap.py` and a
bone listing, or which joint each `Ref_` vertex weights most (meshes.json).
For Mario: J04 pelvis, J05 chest, J22 neck, J23 head, J24 cap, J25-J28
face, J06/J29 shoulders (left/right), J08/J31 upper arms, J09/J32 elbows,
J10/J33 wrists, J11-J18 / J34-J41 fingers (two bones each, index first),
J19-J20 / J42-J44 thumbs, J48/J54 hips, J49/J55 knees, J51/J57 ankles,
J52/J58 toes. The bind pose is a T-pose (arms along X). Other bases
differ: always list them.

## 5. Modelling with `blender_parts.py` (primitives route)

In the character's `build_<name>.py` (template in `templates/`). Build each
part as a coarse cage, let `finish` subdivide it smooth, then decimate the
export meshes to the budget:

```python
exec(open(f"{REPO}/tools/fighter/blender_parts.py").read())
exec(open(f"{REPO}/tools/fighter/blender_bake.py").read())   # paint() (§5b)
clear_parts()                                    # removes the previous build's objects
wood = paint("xx_p_wood", "wood", "wood")        # procedural, colour from PALETTE["wood"]
p = Part("Xx_Torso", [wood])                     # one object; a list of materials
loft(p, rings, weights_of, cap_start=True, cap_end=True, seg=18)
ob = p.finish(subsurf=1)                         # Catmull-Clark once (weights, UVs follow)
```

| Call | What |
|---|---|
| `loft(part, rings, weights_of, closed, cap_start, cap_end, mat, v_range, open_arc, seg)` | quads between free-standing ellipses: each ring `(centre, normal, rx, rz[, turn, jag])`, rx across, rz toward the front (+Z), `jag(i, angle) -> offset` per point (a jagged hem, a pointed collar); `open_arc=(38, 322)` keeps that arc of degrees (a cape open at the front); u = 0.5 at the front, v from `v_range[0]` (first ring) to `v_range[1]` |
| `spiral(part, centre, axis, r0, r1, turns, width, weights)` | a rolled ribbon (scroll curl) |
| `ellipsoid`, `limb`, `tube`, `ring_grid` | older primitives, still there (`mat=` picks a material) |
| `part.orient(inside_of)` | turn every face away from `inside_of(face centre)`; do it on open sheets before solidify |
| `part.finish(subsurf=0, solidify=0.0, inner_mat=None, smooth=True)` | make the object; `solidify` gives a sheet thickness, its inner side in `inner_mat` (a cape's lining); `smooth=False` for carved, faceted bits (a nose) |
| `join(name, objects)` | merge finished parts into one export mesh |
| `decimate(ob, ratio)` | fewer triangles, same look; smooth shapes keep it down to ~0.3 |
| `grain(ob, "x")` (blender_bake) | wood grain along x for this part (arms in the T-pose); default y |

- **Rings helper.** An ellipsoid as rings for `loft`:
  `[(c + up*cos(phi)*ry, up, rx*sin(phi), rz*sin(phi)) for phi in ...]`
  (the template's `sphere_rings`, with any `up` axis: an eye looking
  forward, a lid tipped over it).
- Weights: `weights_of(ring, i, pos)` -> `[(bone, w), ...]` summing to 1,
  at most 4 bones. Ball-jointed limbs: each segment rigid on its bone, a
  ball at every joint (as a doll); fingers on the base's finger bones, so
  its fists open and close; a hat on the base's hat/cap bone if it has one
  (Mario: J24); a cape on the chest bone (J05 on Mario) with its top ring
  blended into the neck.
- **Sheets** (capes, collars, ribbons): `orient` them, then `finish(solidify=...)`;
  a double-sided sheet is lit from one side only in the game.
- **Separate meshes by how they move and shade**, since §5b bakes one
  texture each: head, eyes, hat, ribbons, body (torso+legs), arms
  (arms+hands), joints (every ball), boots, cape, collar, rope...
- Name each export mesh at the end (`ob.name = ob.data.name = "Xx_Body"`):
  §5b and `make_pack.sh` use those names.
- Budget: Melee's own are 0.8-6k triangles; an HD custom fighter up to
  ~20k (validate.py warns above). Check with the `exported ... triangles`
  line.

Check every change with `blender_snap.py` from four views (`COLOR =
"RANDOM"`: parts in different colours), then with a baked Cycles look
(§5b), then in the game (`closeup.py`). Put renders next to the references
in one image (PIL) and compare part by part.

## 5b. Painting and baking (`blender_bake.py`)

Materials are procedural (`paint(name, palette_key, pattern, overlay=png)`:
pattern `wood` grain bands, `fabric` soft noise, `flat`; `overlay` an RGBA
PNG painted through the part's "UV" map: faces, emblems, seams, irises).
When the model is built:

```python
PALETTE = {"wood": (232, 172, 108), "cloth": (38, 96, 222), ...}     # sRGB; before paint()
COSTUMES = {"Nr": {}, "Re": {"cloth": (206, 40, 38)}, ...}           # palette changes per costume
BAKE = {"Xx_Body": 2048, "Xx_Head": 2048, "Xx_Eyes": 1024, ...}      # export mesh: texture size
AO_FLOORS = {"Xx_Eyes": 1.0, "Xx_Joints": 1.0}  # 1.0: no occlusion baked on this mesh
AO_NO_CAST = ["Xx_Arms", "Xx_Joints"]           # meshes that shade no other
AO_SAMPLES = 96; OUT = f"{DEV}/{ID}/baked"; bake_all()
```

Per mesh: a "Bake" UV atlas (smart projection), colour baked per costume
(only meshes using a changed palette key), occlusion once, `OUT/<mesh>_<C>.png`
= colour x occlusion, `OUT/bake.json` ({material: {costume: png}}), and the
mesh gets one image material named after it in lower case, ready to
export. Lessons from the first fighter:

- Bake in the **bind pose** (the default). Limbs move far from it: their
  occlusion on the body or a cape shows as dark blotches wherever they are
  not, so arms go in `AO_NO_CAST`.
- **Ball joints** show a different side in every pose: a mesh of their own
  with `AO_FLOORS` 1.0, or the side buried in the bake shows dark.
- **Eyes** set into sockets bake grey: `AO_FLOORS` 1.0.
- Colours: the emission bake is linear float; the baker encodes sRGB itself.
  Wrong-looking (washed out) colours mean a double conversion.
- `--quick` builds in `make_pack.sh` (1024 px, 24 samples) take ~25 s;
  full ones (2048 px, 96) ~80 s on an M4 Pro.

## 6. AssetFurnace (image-to-3D route)

github.com/Bingeljell/image-to-3dlab. Local, no cloud. Read `install.sh`
before running it (its README says so).

```sh
git clone https://github.com/Bingeljell/image-to-3dlab ~/image-to-3dlab
cd ~/image-to-3dlab && bash install.sh --yes --dir ~/image-to-3dlab   # --dry-run first
python scripts/bootstrap_pixal3d.py          # Pixal3D backend, 8.4 GB weights; asks first
```

- **Mac requirement:** Pixal3D compiles Metal kernels: needs **full Xcode**
  (`xcode-select -p` must not be `/Library/Developer/CommandLineTools`), and
  ~10 GB free. 24 GB RAM works for Pixal3D; 32 GB recommended. If any is
  missing, use the primitives route (§5); it is what Geno shipped with.
- Generate (pre-masked transparent PNG; `--back` adds a back view):
  `python scripts/pixal3d_generate.py front.png output/run.glb --seed 42 --back back.png`
  (views land in `output/run.svviews`).
- Finish to a Melee budget with source pixels projected back:
  `python scripts/retopo_repaint.py output/run.glb front.png finished.glb --faces 3000 --skip-paint --views output/run.svviews`
- Then in Blender: import the GLB (`bpy.ops.import_scene.gltf(filepath=...)`),
  scale and place it over the `Ref_` meshes at the base's joint positions,
  parent to `Skeleton` with an Armature modifier, weight with
  `bpy.ops.object.parent_set(type="ARMATURE_AUTO")` then fix shoulders,
  hips, hat and cape vertex groups by hand (vertex groups named `Jnn`), give
  each material `m["hsd_texture"] = png_path` (bake the texture to a PNG
  first) and export with `blender_export_fighter.py`.
- AssetFurnace's own auto-rig is not used: the base's bones are required.

## 7. MCP for Blender (live Blender, optional)

github.com/ahujasid/mcp-for-blender. Lets Claude drive a Blender window and
look at it; useful for interactive fitting (AssetFurnace route) and
checking deformation in poses. Headless jobs (§2) do everything else.

```sh
DISABLE_TELEMETRY=true uvx mcp-for-blender install-addon       # copies the add-on into Blender
claude mcp add blender -e DISABLE_TELEMETRY=true -- $(which uvx) mcp-for-blender
$BLENDER --python REPO/tools/fighter/blender_start_mcp.py       # enables the add-on, starts its server (:9876)
```

MCP tools (when loaded in the session): `get_scene_info`,
`execute_blender_code` (run bpy), `look` (viewport/camera/animation-frame
images), `get_addon_status`. Telemetry stays off.

MCP servers added mid-session are not loaded until a new session. Then talk
to the add-on's socket directly with the repo client (same commands):

```sh
python3 REPO/tools/fighter/blender_mcp.py ping
python3 REPO/tools/fighter/blender_mcp.py code job.py                 # run a file in the live Blender
python3 REPO/tools/fighter/blender_mcp.py code -c "import bpy; print(len(bpy.data.objects))"
python3 REPO/tools/fighter/blender_mcp.py screenshot out.png 1200     # viewport image
```

Pose check in a live scene: set `pb.rotation_euler` on
`bpy.data.objects["Skeleton"].pose.bones["J08"]` etc. and screenshot; or
render with `blender_render_ui.py`'s `POSE`.

## 8. fighter.cfg

```
id       Xx                 two letters, unused (SKILL.md)
name     "NAME"             menus; <= 31 characters
base     Mr                 base fighter code
symbol   PlyName5K          joint symbol stem (§3)
costumes Nr Re Gr Bk        files Pl<id><C>.dat; first is the default; <= 8
teams    Re Nr Gr           optional: red, blue, green team costumes (default Re Bu Gr)
moves    Name               optional: compiled specials (moves.md)
voice    none               optional: none of the base fighter's voice clips (a silent character)
weight   0.95               optional multipliers on the base: weight walk dash
jump     1.05                 jump gravity fall air (0.5-2 sensible)
```

fighter.cfg and every costume file are hashed for online play (same packs
on both machines = custom fighters allowed; otherwise base fighters only).

## 9. The game's test knobs

All with `REPO/build/macos/melee --no-card DISC` (Windows: `melee.exe`):

| Env var | Effect |
|---|---|
| `MELEE_FIGHTERS_DIR=$DEV/packs` | load packs from there (default: `fighters/` in the game's base path - `Melee4K.app/Contents/Resources/` or beside `melee.exe` - else the user folder's `fighters/`; docs/fighters.md) |
| `MELEE_BOOT_SCENE=vs` / `vs-css` | boot into a VS match / character select |
| `MELEE_DEBUG_VS_CHARS=Xx:0,8:0` | the VS players: `<CKind or pack id>:<costume>[h]`; pack ids count their own costumes from 0; `h` = human on that port |
| `MELEE_DEBUG_VS_STAGE=31` | stage (StKind: 32 FD default, 31 Battlefield, 3 Stadium, 8 Yoshi's Story) |
| `MELEE_DEBUG_VS_STOCKS=1` | stock match (reaches GAME! and the results screen) |
| `MELEE_INSTANT_WIN=1` | ends matches fast (results screen checks) |
| `AURORA_SCREENSHOT_FRAMES=300,600` `AURORA_SCREENSHOT_DIR=dir` | TGA screenshots at those frames |
| `MELEE_EXIT_AFTER_FRAMES=n` | quit after n frames |
| `MELEE_KEY_FIFO=fifo` | script keys: lines `"Z 100"`, `"Right+Z 100"` (SDL key names + hold ms) into a `mkfifo`; Z is B, X is A, arrows the stick; with an `h` player this drives the fighter (`tools/net_test.py` `fifo_write`) |
| `MELEE_MOUSE_TEST=u,v[@frame]`, `MELEE_USER_DIR=dir` | pinned pointer (u, v: 0-1 of the window) from that frame; a throwaway user dir (no saved settings) |
| `MELEE_BOOT_SCENE=vs-css` + `MELEE_MOUSE_TEST` on the icon + `X 150` in the fifo | picks the fighter on character select (door portrait, name) |
| `MELEE_TEXANIM_LOG=1` | logs texture animations asked for a frame past their image table (menus picking an icon for a costume they do not have) |

The pause camera (Start, `Return` in the fifo, with an `h` fighter on port
1): the control stick (arrow keys) turns it, Y (`V`) zooms in, X (`C`)
out. `closeup.py` drives it.

Convert screenshots: `sips -s format png -Z 1280 f.tga --out f.png` (Mac),
or PIL. Contact sheets: PIL paste (see `preview.py`). Debug output from game
code: `fprintf(stderr, ...)` and capture stderr; `OSReport` does not reach
`MELEE_LOG_FILE`.

Scripted special-move test (fighter human on port 1, idle opponent):

```sh
mkfifo keys
MELEE_FIGHTERS_DIR=$DEV/packs MELEE_KEY_FIFO=keys MELEE_BOOT_SCENE=vs \
MELEE_DEBUG_VS_CHARS="Xx:0h,8:1h" AURORA_SCREENSHOT_FRAMES=... AURORA_SCREENSHOT_DIR=shots \
MELEE_EXIT_AFTER_FRAMES=2400 REPO/build/macos/melee --no-card DISC &
sleep 9; python3 -c "import sys; sys.path.insert(0,'REPO/tools'); import net_test, time
for k in ['Z 100','Right+Z 100','Down+Z 100','Up+Z 100']: net_test.fifo_write('keys', k); time.sleep(2)"
```

The opponent's damage climbing in the screenshots proves hits land.

## 10. Gates

```sh
# determinism: 3 seeds x 3 stages, record/replay, smooth motion on/off, with the fighter in
MELEE_FIGHTERS_DIR=$DEV/packs MELEE_DEBUG_VS_CHARS="Xx:0,8:0,Xx:1,2:0" \
  python3 REPO/tools/interp_determinism.py DISC /tmp/det        # every row PASS
# online, two local instances (docs/testing.md), fighter picked:
MELEE_FIGHTERS_DIR=$DEV/packs python3 REPO/tools/online_test.py --disc DISC --guest-mouse "U,V@0"  # PASS
# (U,V: the guest's pinned pointer on the fighter's icon, e.g. 0.79,0.455
#  for the first custom icon)
# soak: every stage, character and mode, with every pack costume in the VS rotation
python3 REPO/tools/soak.py DISC /tmp/soak --fighters $DEV/packs          # N/N passed
```

Online with a peer that lacks the pack, or runs v1.1.0, must fall back to
base fighters only (`online: base fighters only (...)` in the log);
`docs/testing.md` covers the online harness, including a Windows peer
under Wine.
