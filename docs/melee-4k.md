# melee-4k

This fork of melee-pc targets 4K with HD textures and smooth motion on
high-refresh displays, on macOS (Apple Silicon) and Windows, with a one-step
installer.

No game data and no texture packs are in this repository. You supply your own
disc and download the community packs yourself. The tools here only process
them.

## What's in it

| Area | State |
|---|---|
| 4K rendering | Internal resolution up to 10x native; Auto renders at the display's resolution (4K on a 4K display). |
| Widescreen | 16:9 by default, in every scene except the pre-rendered movies; 4:3 ("Original") in settings, switchable mid-match (the HUD moves with it). |
| Frame rate | 60 (default), 120 or 144 fps. Above 60, frames are blended between the game's 60 Hz ticks (fighters, camera and shake, particles, sword trails); the simulation is untouched. Verified at 4K/144 Hz: 60.00 ticks/s, ~143 frames/s. |
| HD textures | The Melee HD 2023 pack wherever it has a texture, then HD Remix and the other community packs; AI upscales (Real-ESRGAN) only where no community version exists. Selectable stock-icon styles. |
| Input | The mouse drives the character select hand, which also appears in the main menus and VS Rules. |
| Online | Host Lobby / Join Lobby by code, Mac and PC together; same-network players connect directly. |
| Text | Glyph spacing in all in-game text (menus, names, headers) reads the right metrics; it was garbled in melee-pc. Melee 4K's own menu entries are rendered in Melee's label style (`src/pc/menu_label.c`). |
| Stability | A soak test plays every stage with every costume, plus every 1P mode and menu, with heap canaries and render audits on: 52/52 pass, steady 60 ticks/s. Fixes so far: Big Blue, Pichu's Thunder, star KOs, Kirby's copy hat, a heap race between disc loading and the game, off-screen damage timing, the off-screen bubble, the results screen. |

## Building from source

macOS (Apple Silicon tested), with Homebrew:

```sh
brew install gcc cmake ninja sdl3 zstd libpng freetype
cmake --preset macos-default
ninja -C build/macos
build/macos/melee "/path/to/Super Smash Bros. Melee (USA) (Rev 2).iso"
```

Windows and Linux builds follow [building.md](building.md). Release
packages: `tools/package_release_macos.sh` and the `release-windows.yml`
workflow (see [setup.md](setup.md)).

## Testing tools

| Tool | What it does |
|---|---|
| `tools/soak.py DISC WORK` | Every stage x costume, every 1P mode and menu, with heap checks and render audits; resumable, writes `summary.md`. |
| `tools/interp_determinism.py DISC WORK` | Records CPU matches, then replays them with smooth motion off and on; any divergence fails. |
| `MELEE_BOOT_SCENE=<mode>` | Boot straight into a mode (`vs`, `classic`, `training`, `adventure`, `allstar`, `target`, `homerun`, `multiman10`, `vs-css`, `trophies`, ..., or `mode:N`). |
| `MELEE_KEY_FIFO=<fifo>` | Scripted keyboard input (`"Up 300"`, `"X 150"`), no window focus needed. |
| `AURORA_SCREENSHOT_FRAMES=<n,...>` | Full-resolution screenshots (TGA) at those frames. |

## What the fork adds

| Area | Change |
|---|---|
| Rendering | `AURORA_SCREENSHOT_FRAMES=600,1200` writes those frames at full render resolution as TGA (`extern/aurora/lib/gfx/screenshot.cpp`). Aurora logs every framebuffer resize. |
| Textures | `MELEE_TEXTURE_DIR` loads a pack from any folder. `MELEE_TEXTURE_DUMP=1` writes every texture with no replacement to `<cache>/texture_dumps`. Thumbnails of block-compressed replacements whose mip tail is not 4x4 aligned are skipped instead of failing (`dds_io.cpp`). |
| Testing | `MELEE_DEBUG_VS_CHARS=<ckind>:<costume>[h],...` boots the debug VS match with up to four chosen fighters: CPUs, or human with `h`; `<ckind>` can be a custom fighter's pack id. |
| Custom fighters | Fighter packs add new characters on an existing fighter's skeleton: [fighters.md](fighters.md). |
| Product | `product.env` names what the tree builds: the name players see, the GitHub repo its updates and online's latest-version check use, the app, folder and zip names, the version, and whether custom fighter packs load. A fork that ships its own game (Melee++) changes only that file and `install.sh`'s copy of it (`tools/test_product.py` checks they agree). The settings folder (and so the HD textures) is shared. |
| Apple arm64 | Variadic `lbArchive_*` lists end in `NULL`, not `0` (crash loading stages; see `porting-notes.md`). Aurora's `system_info.cpp` builds on older SDKs. |
| Gameplay fixes | Pichu's Thunder no longer dereferences a stale pointer (`ftpikachuspeciallw.c`). Big Blue's stage parameters are read as big-endian disc data (`grbigblue.c`): before this, every Big Blue match crashed at entry. |
| High refresh | `src/pc/interp.c` and `vi.c`: blended frames at the display's refresh rate on top of the 60 Hz simulation (`MELEE_FRAME_INTERP`). See below. |

## Texture pipeline (`tools/textures/`)

The encode and upscale steps need the repo virtualenv:
`python3 -m venv .venv && .venv/bin/pip install Pillow numpy etcpak`. Upscaling
also needs `realesrgan-ncnn-vulkan` (the xinntao/Real-ESRGAN v0.2.5.0 macOS zip)
unpacked in `.deps/realesrgan`.

1. **`inventory.py PACK...`**: per-pack texture counts, scales, formats and
   overlaps.
2. **`merge.py OUT SRC...`**: merges packs by priority, highest first. Files
   are hard-linked and a `MANIFEST.json` records which pack each texture came
   from. Pass a pack's option folders (for example a stock-icon style) before
   the pack itself.
3. **`encode.py IN OUT`**: re-encodes as BC7 DDS with full mip chains, capped
   at 6x the GameCube size and 4096 px. Without mips, high-resolution floor
   textures shimmer into noise. Uncompressed, the merged community pack needs
   about 6 GB of texture memory; encoded it needs 0.86 GB.
4. **`sweep.py DISC PACK WORK`**: plays every stage and every costume (four
   CPUs per match) with the pack loaded and dumping on. `WORK/cache/texture_dumps`
   ends up holding exactly the textures no pack covers.
5. **`upscale.py DUMPS OUT`**: Real-ESRGAN at 4x with wrap padding (seamless
   tiling), colour bleeding under transparent texels, and alpha upscaled
   separately. Flat palette and 4-bit art goes through the anime model,
   everything else through the general model.

Then merge the upscaled output as the lowest-priority source, encode, and
install the result as `textures/packs/10-community` (or point
`MELEE_TEXTURE_DIR` at a folder laid out the same way).

6. **`extract.py DISC OUT`**: finds every texture on the disc without running
   the game (HSD image/palette descriptors, TObj and TexAnim pairings,
   particle texture banks, stage animation tables) and names it with the same
   key aurora computes at runtime: 11,344 textures, 99.9% of the VS sweep's
   runtime keys and every FD key. `--keys-only` lists them (3.5 s),
   `--compare DIR` reports a pack's coverage, `--validate DUMPS` decodes and
   compares against runtime dumps (bit-exact in all ten formats), `--match
   REGEX` writes PNGs of selected textures.
7. **`original_pack.py DISC PACKS`**: builds `packs/05-css-original`, the
   character select icons and hover portraits decoded from the disc and
   AI-upscaled. No longer in the release pack (the Melee HD 2023 character
   select art replaced it); kept for comparison.

8. **`restore_alpha.py MERGED ORIGINALS`**: run on the merged pack before
   encoding. A few community textures were saved without their alpha (HD
   Remix's item foods are opaque on black, which drew a black box around
   every food a Shy Guy carries); this applies the original's cut-out,
   upscaled, to the HD art.
9. **`choices_page.py OUT --source Label=DIR ...`**: an interactive page
   for choosing between the community versions of each texture. Versions
   that look the same are shown once; characters are chosen per costume.
   It saves the choices as `melee4k-texture-choices.json` and writes
   `OUT/plan.json`, every texture with a community version.
10. **`make_choices.py OUT --plan plan.json --choices FILE --base MERGED`**:
   lays out the two layers from those choices: `OUT/community` (the chosen
   versions, plus the stock icons from the current merged pack) and `OUT/ai`
   (AI upscales, only of textures with no community version). A texture
   chosen as "Original" is in neither. Then `restore_alpha.py` on the
   community layer and `encode.py` on both, installed as
   `packs/10-community` and `packs/20-ai-fill`.

Colour stored under zero alpha is kept by both `encode.py` and `upscale.py`:
Melee draws some textures with an alpha test (the character select slot wipe
animates an alpha threshold over art held under alpha 0), so blacking it out
showed up as speckle.

Pack layout, read by `src/pc/textures.cpp`: each `textures/packs/<NN-name>/`
loads at a priority from its order (lower NN wins), `textures/stocks/<style>/`
overrides them when that stock style is picked in settings, and a loose
Dolphin pack directly in `textures/` loads below all of them.

### Packs used so far

Melee HD (the 2023 release by the Melee HD team, a Dolphin `GALE01` folder:
`main` plus `optional` character textures) and Super Smash Bros. HD Remix
Texture Pack (QZ|DOGG) are the main sources; the choices page picks between
them. HD Remix It already
combines Melee HD, Faster Melee HD, SSBM: HQ and Melee Remastered. Melee HD
Submissions, the Melee HD "Our Edits" set and the SD Remix 3.3 extras fill
gaps. "Melee HD Character Select for Dolphin" uses pre-2015 `GALE01_<crc>`
names, which cannot be mapped to `tex1_` hashes, so it is unusable.
ssbmtextures.com (Definitive Melee HD) is offline.

## Checking the 4K target

```sh
# render_scale 6 + widescreen 1 in launcher.cfg renders at 4024x2264
MELEE_BOOT_SCENE=vs MELEE_EXIT_AFTER_FRAMES=1800 MELEE_FPS=1 \
MELEE_TEXTURE_DIR=<encoded pack> build/macos/melee <disc>
```

On an M4 Pro this held a steady 60.0 fps with the encoded pack: worst frame
28 ms, none over 33 ms.

## High-refresh presentation (120/144/240 Hz)

The simulation always runs at exactly 60 Hz, so gameplay, timing and Slippi
replays are unchanged. After each tick the scene loop (`gm_801A4D34`)
presents one frame per display refresh until the next tick is due. Each
frame is drawn with every moving thing blended to the moment that frame
reaches the screen.

**What is blended** (`src/pc/interp.c`). Everything is recorded once per
boundary, blended into place for a draw, and restored right after it:

- **Joints:** every JObj tree's world matrices. Translation and scale are
  lerped and rotation is slerped, with a plain lerp for sheared or
  zero-scale matrices and never a non-finite result. Blending final world
  matrices means no child is rebuilt from a blended parent, and skinned
  meshes follow their bones. After each draw every recorded matrix is put
  back. Any matrix a draw callback rebuilt is also left dirty, so blended
  values never reach game code that reads bone matrices (hitboxes, camera
  boxes).
- **The game camera:** its `CameraTransformState` and screen-shake offset,
  because `Camera_8002A4AC` rebuilds the CObj from them on every draw.
  Other CObjs (HUD, menus) are blended directly.
- **Particles:** world position, matched by pointer, kind, bank and
  remaining life.
- **Sword trails:** each entry of the 3-entry ring is blended with the entry
  of the same age a tick earlier.
- **The HUD** is JObjs drawn through a CObj, so it is covered by the above.
- **Snapping:** anything that moved more than 40 units in a tick (a camera
  more than 160) is drawn where it is now.

**Timing** (`src/pc/vi.c`):

- **One tick clock.** Melee ticks when its 60 Hz pad alarm fires
  (`lb_80019628`, `pc_os_next_tick_ns` in `src/pc/os.c`). With in-between
  frames on, the pacer paces to that alarm instead of a schedule of its own:
  two drifting 60 Hz clocks made frames bunch up within a tick.
- **A frame grid.** Every frame begins on a slot one display refresh after
  the last. That keeps presents evenly spaced and the swapchain queue
  shallow; a backed-up queue delivers frames in pairs. A tick runs on the
  first slot at or after it is due. A frame that misses its slot re-anchors
  the grid.
- **Blend factor.** `t = 1 − (next tick − slot) / 16.67 ms`, which is
  always in [0, 1], so no frame is clamped (a clamped frame is a stall).
  After a boundary that ran k catch-up ticks, `t` is rescaled over the
  k-tick span (`pc_interp_span_t`). Capturing between catch-up ticks
  instead would update JObj matrices where the game never does.
- **Latency:** about one refresh on top of 60 Hz presentation (7 ms at
  144 Hz).
- **Invisible to the game.** The extra presents pump input and window
  events, but count no retrace and run no VI callback or pad alarm.

**When it is on.** `MELEE_FRAME_INTERP` unset means auto: on when the game
window's display (from aurora's window, re-read every second) refreshes at
100 Hz or more. Two presents per tick on a 60 Hz display would halve the
game speed, and macOS has no tearing present mode, so VSync off does not
help there. A watchdog turns it off, until the window moves to another
display, after three 2-second windows in which ticks fell below about 57/s
or frames stopped arriving at the refresh rate. `=1` forces it on and `=0`
turns it off. It is never on in netplay.

**Verified (M4 Pro, XV273K at 3840x2160 @ 144 Hz, HD pack).** Across
Final Destination, Pokémon Stadium, Big Blue, four sword fighters, Pichu
and Pikachu, Classic, the title screen and fullscreen:

- the simulation held 60.00 ticks/s;
- presents held 141–144/s, beginning 6.94 ms apart with a 0.3–1.0 ms
  spread;
- `MELEE_NET_RENDER_AUDIT=1` saw no game-state change from any of the
  extra drawing.

**Diagnostics:**

- `MELEE_FPS=1` adds the tick rate, the present rate and the frame-begin
  interval, tagged `[occluded]` or `[no focus]` when the OS may be
  throttling the window.
- `MELEE_INTERP_TEST_STEPS=n` draws exactly n frames per tick at t = 1/n …
  1, regardless of the clock. Frame captures (`AURORA_SCREENSHOT_FRAMES`)
  stall on the GPU and would wreck clock-based blending; with 3 steps,
  consecutive captures step evenly (median largest/smallest step within a
  tick 1.09).

## Coverage (2026-10-10)

`sweep.py` played all 123 costumes across the 29 playable VS stages and
dumped 1,100 textures that no community pack covers. `upscale.py` filled them,
which brings the merged pack to 6,321 textures (1.18 GB as BC7 with mips). A
re-run of Corneria, previously the second-worst stage with 113 gaps, left no
misses. Not yet covered: menus, the character and stage select screens, 1P
modes, trophies and cutscenes. VS boots do not reach them, so play them by
hand with the dump flags set.

## Known issues

- `lbVector_WorldToScreen` range assert with a fighter camera point of
  `y=-inf z=nan`: seen once on Mute City before interpolation existed. Two
  later sightings were traced to blended matrices leaking out of
  interpolated draws, which is fixed (see above). It has not recurred in 21
  runs since. `MELEE_CAM_BONE=1` logs the fighter and motion state if it
  does.
- Stage ids 0x15 (Akaneia) and 0x1A (Icetop) are not playable VS stages;
  `sweep.py` skips them.
- Not interpolated: SisLib text and other immediate-mode drawing that moves
  per tick. In matches that is nothing visible found so far.
