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
"Cube" that is not parented to the skeleton, so it is never exported; hide
it in check renders). A job is a short Python file that `exec`s the repo's
Blender scripts in order; each reads its parameters from globals set first:

```python
DIR = "~/Melee4K-dev/fighters/base/Mr"              # (expanded path)
exec(open(f"{REPO}/tools/fighter/blender_import_base.py").read())
REPO = "..."; TEX = f"{DEV}/{ID}/tex"
exec(open(f"{DEV}/{ID}/build_<name>.py").read())
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
| `blender_snap.py` | Blender: quick solid render, framed on visible meshes | globals `OUT`, `VIEW` (front, side, back, threequarter), `HIDE` (object names) |
| `blender_export_fighter.py` | Blender: meshes parented to Skeleton (not `Ref_`) -> `mesh.json` | global `OUT`; materials need `hsd_texture` (blender_parts.material sets it) |
| `blender_render_ui.py` | Blender: portrait `csp<n>.png`, `icon.png`, `stock<n>.png` with Cycles | globals `OUT`, `COSTUMES` (list of {material: png}), `POSE` ({bone: (x,y,z) degrees}), `SAMPLES` |
| `build_dat.py` | one costume file `Pl<ID><C>.dat` from mesh.json over the base's | `--base-dir $DEV/base/BASE --mesh mesh.json --symbol <stem> --out pack/Pl<ID><C>.dat --textures pack/textures [--texture MAT=PNG ...]` |
| `build_ui.py` | menu images `Pl<ID>Ui.dat` | `--icon icon.png --csp csp0.png ... --stock stock0.png ... --name "<Name>" --out pack/Pl<ID>Ui.dat` |
| `validate.py PACK` | checks a pack (cfg, symbols, skeleton, weights, budgets, UI, textures) | `--base-dir $DEV/base/BASE --disc DISC`; exit 1 on errors |
| `preview.py DISC PACKS ID OUT` | CPU matches screenshotted into `OUT/sheet.jpg` | `--costumes 0,1,2 --stages 32,31,3 --frames 900` |
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
`tex1_128x128_<hash>_<fmt>.png`, which the game swaps in. Delete
`pack/textures` before a rebuild so old ones do not pile up.

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
bone listing; for Mario: J04 hip, J05 waist, J22 chest, J23 neck/head,
J06/J29 shoulders (left/right), J08/J31 upper arms, J09/J32 elbows, J10/J33
wrists, J48/J54 hips, J49/J55 knees, J51/J57 ankles. Other bases differ:
always list them.

## 5. Modelling with `blender_parts.py` (primitives route)

In the character's `build_<name>.py` (template in `templates/`):

```python
exec(open(f"{REPO}/tools/fighter/blender_parts.py").read())
clear_parts()                                   # removes the previous build's objects
m = material("xx_body", f"{TEX}/body.png", double=False)   # double=True: no backface culling
p = Part("Xx_Body", m)                          # one object, one material
ellipsoid(p, centre, (rx, ry, rz), [("J23", 1.0)], seg=12, rings=8)
limb(p, jp(8), jp(9), 0.46, 0.42, "J08", None)  # tube a->b, radii, bone(s)
tube(p, centres, radii, lambda k, i, pos: [("J23", 1.0)], seg=16, uv_v=(0.1, 1.0), cap=True)
ring_grid(p, rows, weights_of, uv_of, closed=False)   # open strip (a cape open at the front)
p.finish()
```

- Weights: lists of `(bone name, weight)` summing to 1; at most 4 bones a
  vertex. A part that should follow two bones (cape over the shoulders)
  blends them per vertex in its weights function.
- `tube` keeps rings square to the curve: hat tips, horns, tails, scrolls.
  Centres going straight up give level rings, so one tube can be a dome
  that bends into a tip.
- Flip an inner shell's faces (`f.normal_flip()` on `part.bm.faces` before
  `finish`) for linings; or make the material double sided.
- UVs: `ellipsoid`/`tube` put u = 0.5 at +Z (the front) and run v pole to
  pole / along the tube, so a face or emblem painted at the image's
  horizontal centre lands at the front. Image y = (1 - v) * height.
- Budget: Melee fighters are 1.5-4k triangles; `seg` 8-16 is plenty.

Check every change with `blender_snap.py` from four views, and with Cycles
renders (`blender_render_ui.py`) for colour. Put them next to the
references in one image (PIL) and compare part by part.

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
| `MELEE_MOUSE_TEST`, `MELEE_USER_DIR=dir` | pinned pointer; a throwaway user dir (no saved settings) |

Convert screenshots: `sips -s format png -Z 1280 f.tga --out f.png` (Mac),
or PIL. Contact sheets: PIL paste (see `preview.py`).

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
python3 REPO/tools/online_test.py --disc DISC ...                # PASS, every frame identical
```

The soak (`tools/soak.py`) and the online interop tests are in
`docs/testing.md`.
