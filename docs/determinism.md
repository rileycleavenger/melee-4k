# Cross-platform determinism

Online play (docs/online.md) is lockstep: every machine simulates every frame
from the same inputs, so every machine must compute the same game from them,
bit for bit, whatever its CPU, OS or compiler. This is how that was made true
and how it is checked. It was written for melee-pc's rollback netcode (section
5 of the old `docs/netcode-plan.md`, still in git history); the findings and
the fixes are the same for lockstep.

The checks: `tools/net_determinism.py` and `tools/interp_determinism.py`
(one recorded run replayed on every build), `tools/crossplay/` (a recording
from one machine replayed on another: Mac and Windows), and the host's
state-hash comparison every 30 frames in a live game.

## Prerequisites (M0)

Findings from the flag/libm audit (all four AArch64 targets plus MinGW and Linux):
1. Game code already has `-fno-strict-aliasing -fwrapv -ffp-contract=off` (`CMakeLists.txt:137-139`), but `aurora_mtx`/`aurora_gx` and `src/pc/*.c` do not → AArch64 emits `fmadd` in `PSVECNormalize` (`extern/aurora/lib/dolphin/mtx/vec.c:41`), `PSMTXMultVec`, `C_MTXRotRad`, used by collision (`mplib.c:1183,4919`). Fix: `-ffp-contract=off` on `aurora_mtx`, `aurora_gx`, `aurora_gd`, and the `melee` target.
2. `sinf/cosf/tanf/atanf` resolve to platform libm (glibc / mingw-w64 CRT / bionic / iOS) — used by `fighter.c:2217`, `ftcoll.c:910,2856`, `camera.c`, `ground.c:2882`, `mtx.c:334-379` — and `lbtrigf.c:147` only compiles the decomp's `atanf` under `__MWERKS__`. Fix: vendor one implementation (musl's `sinf/cosf/tanf/atanf`, MIT) into `src/pc/libm/`, `#define sinf pc_sinf` in `src/pc/compat.h` plus `-fno-builtin-sinf …`. `sqrtf/sqrt/fabsf/fmodf` are correctly rounded — leave.
3. Unify `-O2` (Linux is RelWithDebInfo, Windows/Android Release `-O3`: `tools/package_windows.sh:53`, `tools/build_android.sh:41`); keep `MELEE_ENABLE_LTO=OFF`; add `-ftrivial-auto-var-init=zero` to `melee_game`.
4. RNG: force an agreed seed at connect (`gmmain.c:169-174` already honours `MELEE_SEED`); `_HSD_RandForgetMemory` resets per scene (`random.c:23-28`) — must be in the snapshot.
5. Disc I/O: a file-cache hit completes the load synchronously, a miss takes N frames (`src/melee/lb/lbfile.c:145-150`); callbacks run on the DVD worker (`dvd.cpp:560-611`). Rule: prewarm every file the match needs (stage, both fighters, items, SFX banks) behind the "ready" barrier, so every in-match load is a 0-frame hit on both peers. Verify with a load-log injection (`MELEE_LOG_DVD`).
6. Alarms are wall-clock (`src/pc/os.c:174-215`); online ticks are driven by the loop, not the pad alarm.
7. Match-start state that must be identical on both peers (`gm_80167BC8` `src/melee/gm/gm_1601.c:3595-3727`): memcard `GameRules`/`GamePrefs`, handicap, unlock bitmasks (`gm_1601.c:2384`), random-stage switches, frozen-stadium (`grpstadium.c:2057-2067`), Sheik/Zelda A-hold at load (`gmvs.c:1597`). Host ships the 0x60-byte `StartMeleeRules` + `PlayerInitData[4]` + stage + seed + toggles; guest overwrites its copy; unlock-all forced on in online modes.

**Harness.** `MELEE_NET_RECORD=file` writes `MRC1`, the seed, then per frame
the four `PADStatus` as simulated and the frame checksum; `MELEE_NET_REPLAY=file`
feeds the pads back in and reports the first frame whose checksum differs
(`net: REPLAY DIVERGED at frame N`), `record_open`/`replay_feed`/`record_frame`
in `src/pc/net_snapshot.c:57-138`. The record stride in this build is **68
bytes** (`4 × sizeof(PADStatus) + 4`; `PADStatus` is 16 bytes on `TARGET_PC`,
not the 12 its 11 used bytes suggest — `extern/aurora/include/dolphin/pad.h:112-126`
appends `u32 extButton`). Anything reading a recording must divide by 68 or it
reports nonsense frame numbers. `MELEE_NET_STATE_LOG=<file>` writes two lines
per frame to its own block-buffered file (one write per 8 frames, deliberately
*not* through `pc_log_line`, which flushes twice per call and perturbed the
very measurement it was taken for): the existing human-readable `net: state`
line, and a new `net: bits` line carrying the raw float bits of exactly the
fields `frame_checksum` folds, because the readable line rounds to three
decimals and hides the 1-ULP differences worth hunting. `record_state` is
called for every frame, returning immediately unless netplay is running or
this knob is set, so a solo replay can be localised field by field; it is only
useful together with `MELEE_NET_RECORD`/`REPLAY`, since `fresh_tick` does not
run otherwise.

**Checklist (state of the tree):**
- [x] `-ffp-contract=off -fno-fast-math` on `melee_game`, `aurora_mtx`, `aurora_gx`, `aurora_gd` and the `melee` target (`MELEE_FP_FLAGS`, `CMakeLists.txt:46-51`, `:139`, `:183`).
- [x] Trig from `src/pc/libm/` (musl): `pc_trig.h` renames `sinf/cosf/tanf/atanf` (`pc_trig.h:17-20`), pulled in by `compat.h:12` for game code and by `-include` for `aurora_mtx` (`CMakeLists.txt:49`), with `-fno-builtin-*` on both (`:48`, `:140`).
- [x] `fmaf` and `logf` from `src/pc/libm/` too, and the game's own `atan2f/acosf/asinf/powf/expf` renamed with their callers (v1.1). The v1.1.0-rc2 Mac-PC desync: aurora's `ppc_rsqrte` (every `PSVECNormalize`, so every floor normal) calls `fmaf`, which mingw-w64's msvcrt rounds twice — one ULP off the Mac's fused result on a slope normal, then `self_vel.y`. The same toolchain's `math.h` makes `powf`/`expf` inline wrappers around msvcrt's double `pow`/`exp`, so wall techs did not use the game's `powf` at all. `tools/test_sim_libm.py` now fails the Windows release build on any such call, and `tools/online_test.py --guest-exe` plays the CI exe under Wine against the Mac build.
- [x] `-O2` on every platform: `CMAKE_C_FLAGS_RELEASE` is forced to `-O2 -DNDEBUG` (`CMakeLists.txt:20-24`), so the Windows/Android `Release` builds (`package_windows.sh:53`, `build_android.sh:41`) match Linux `RelWithDebInfo`; `-ftrivial-auto-var-init=zero` on `melee_game` (`CMakeLists.txt:141`).
- [x] Agreed seed: `pc_net_connect(seed)` applies it on the host, RULES carries it to the guest, and both re-apply it entering `start_frame` (`net.c:1121-1139`, `hs_done` `net_handshake.c:135-141`); `HSD_RandSeedPtr` and its word are saved and restored with every snapshot.
- [x] Record/replay harness (`MELEE_NET_RECORD`/`MELEE_NET_REPLAY`, `net_snapshot.c:57-138`) and the sync test (`MELEE_NET_SYNCTEST`, `net_snapshot.c`).
- [ ] Tick purity: `MELEE_NET_SYNCTEST` (re-run each tick from a restored snapshot and compare state hashes) still reports mismatches on a replay, so the tick is not yet a pure function of (state, inputs). Numbers, cause and what was ruled out are in §13's first entry; this one gates every other determinism result, including §5.1's.
- [ ] Disc I/O frame counts: a file-cache hit vs miss still changes how many frames a load takes (`lbfile.c:145-150`). The barrier makes the first 120 frames after any game-thread request lockstep (§4), which hides it for rollback, but a load that lands on different frames on the two peers is still a divergence in lockstep. No per-match prewarm behind the ready barrier exists yet; `pc_file_cache_start_prewarm` (`file_cache.cpp:400`) is the boot-time background warm, and `MELEE_PREWARM=0` disables it.
- [x] "An injected 1-ULP libm change is caught" — **done, and it passes.** It could not be executed as written: every math symbol the sim uses is defined inside the executable (`src/pc/libm` + `lbtrigf.c`), so `LD_PRELOAD` has nothing to interpose, and the injection has to happen at build time. The hook is `-DMELEE_FP_PERTURB=sinf|cosf|tanf|atanf` (`CMakeLists.txt:55-80`, `src/pc/libm/pc_perturb.c`, routed through `pc_trig.h`), which returns that function one ULP away from zero on finite non-zero results. Proof: the clean build replays a 1376-frame recording identically; the perturbed build logs `net: fp perturb sinf fired 40335 times by frame 600` and `net: REPLAY DIVERGED at frame 752` — 752 rather than 0 because the checksum only folds fighter state once `in_fight()`. `objdump -d` confirms 166 call sites route to `pc_perturb_sinf` in the perturbed build and to `pc_sinf` in the clean one: the routing is swapped, not added to. Such a build desyncs against every other build by design, so it is fenced four ways — off by default, a CMake `message(WARNING)`, `net: FP PERTURB … never ship` on every run plus a periodic "fired N times" line, and `tools/package_windows.sh` aborting if the staged `melee.exe` carries the marker. (An earlier figure of "24 `sinf` calls in a 2400-frame replay" was wrong by three orders of magnitude and is withdrawn: the counter in the perturbed build measured 40 335 by frame 600. The dynamic-symbol shim that produced it was counting only calls that reached the *platform* libm, which is nearly none, and that is the correct reading of it.)
- [ ] `HSD_PadRumbleInterpret` in re-sim (`controller.c:64`, no gate).
- [~] Cross-platform replay **executed for the first time** by `tools/net_determinism.py`: Linux is self-identical and Windows is not (§5.1). Android and macOS could not be run here at all, and the comparison itself is only as good as §13's first entry — the tick is not yet a pure function of (state, inputs), so this stays open rather than done.
- [ ] Sound→sim feedback (`Ground_801C54DC`, tournament screen): known divergence, cosmetic.

### Cross-platform determinism gate (M0), and the Windows divergence

`tools/net_determinism.py` runs M0: it records one reference run on this Linux
build, replays that single file on every platform reachable from this machine,
and reports the first frame whose checksum differs. A platform with no
hardware here is reported SKIPPED and one that never reached the replay is
BLOCKED — neither is ever conflated with "diverged".

The reference run is deliberately not an idle match. `MELEE_DEBUG_VS=cpu` puts
keyboard-driven Link against a level-4 CPU Mario, the harness presses Start
every 2 s until the match's own stage archive appears in the log, then cycles a
fixed 14-line input pattern through `MELEE_KEY_FIFO`. Two gates refuse a
worthless recording before any replay runs (`tools/net_determinism.py:231-258`):
over the last 600 frames at least half the per-frame checksums must change, and
at least 20 frames must carry input. Both failure modes were hit during
development and caught: a fixed-schedule Start drive left the run at the title
for all 3984 frames (frames 141 and 638 produced *literally the same* checksum
`0x41b8c76f`), and matching the bare archive name matched the boot prewarm's
`[FileCache] STORED: GrNLa.dat` instead of the match's `HIT:` line, scoring
91/600 checksum changes against 600/600 for a real match. This is the same
trap as §4's `in_fight()` gate and §12's retracted numbers.

One end-to-end invocation on the current tree, one canonical file
(sha256 `96654c7444af3dcf`, 2400 frames, seed 7):

| platform | frames | first diverging frame | what the row is |
|---|---|---|---|
| linux-record | 2400 | identical | the recording leg against the canonical file: a recording defect cannot masquerade as a platform divergence |
| linux | 2400 | identical | replay of the canonical file |
| linux-flip | 1200 | frame 1200 (expected 1200) | sensitivity check, below |
| windows (Proton) | 2400 | **identical** | replay of the same file, on both the log line and the recorded-stream compare |
| android | – | SKIPPED: no device or emulator attached | – |
| macos | – | SKIPPED: no macOS host, no osxcross toolchain | – |

The canonical file is itself produced by a Linux *replay* leg, not by the
record leg, because the record leg drives input through `MELEE_KEY_FIFO` and
therefore paces differently — and the simulation is sensitive to pacing (§13).
The record-vs-canonical comparison survives as its own row rather than being
assumed away.

Every row needs two independent verdicts: the `net: replay finished at frame N`
/ `net: REPLAY DIVERGED at frame N` log line, and a byte comparison of the
checksum stream the replay leg recorded for itself (every leg also sets
`MELEE_NET_RECORD`) against the reference (`tools/net_determinism.py:260-282`).
Disagreement between the two fails the run as a harness fault, not as a
platform result. The same comparison asserts the pads the leg simulated are
byte-identical to the pads in the file, so a build whose replay silently fed
nothing cannot pass as "identical".

The sensitivity check is what makes the green Linux row mean anything: one byte
is flipped in `PADStatus.stickX` of pad 0 at frame `frames/2`, inside the region
the checksum hashes, and the harness must report divergence at exactly that
frame — byte 81610 → `REPLAY DIVERGED at frame 1200 (recorded efda93d1 now
01e2a60e)`. It also validates the 68-byte stride: a wrong stride would land the
flip on a different frame.

**Windows used to diverge at the first in-fight frame; it no longer does, and
the cause was a launcher preference reaching the simulation.** The
investigation is worth keeping in full, because every intermediate conclusion
was wrong in an instructive way.

Measured first: three Proton replays diverged at the same frame with
byte-identical checksums, and `MELEE_NET_STATE_LOG` (§5) showed **only the RNG
seed differed** — position, facing, percent, `motion_id` and stocks equal to
the bit on both platforms. `HSD_Rand` is an LCG
(`src/sysdolphin/baselib/random.c:3-16`), so the seeds are countable: Windows
made exactly **one fewer draw** in the tick that enters the fight. That
excluded float arithmetic — libm, FMA, `-ffp-contract`, optimisation level,
physics codegen — *by measurement*, since all of those perturb a mantissa and
none was perturbed.

Two attributions along the way were wrong, and both are worth naming:

- **Not the card-absent save data.** A probe inside the four unlock predicates
  measured the stage mask, the character mask and the feature byte as `0x0000`
  on **both** platforms. That default is a deliberate zero, built by the
  `memzero` in `gmMainLib_8015F600` (`src/melee/gm/gmmain_lib.c:1414`).
- **Not `gm_80164600` at `ground.c:1441`.** GCC cross-jumps the six identical
  `HSD_Randi(RANDI_MAX)` sites of `Ground_801C24F8` into one call, so a
  backtrace naming that line cannot name the `case`. A probe printing the
  switch selector and every predicate showed `x14 = 6` on both platforms: the
  roll is `case 6`, gated by **`gm_80164ABC`** ("are all unlockable characters
  unlocked"). `gm_80164600` answered 0 on both and was never the cause. **A
  merged call site in a backtrace is not evidence of which arm ran.**

The differing input was `unlock_all` in **`launcher.cfg`** — 1 in the Linux
install, 0 in the Wine prefix, because `pc_launcher_configure` loads that file
from `SDL_GetPrefPath()` and Proton resolves it inside the prefix.
`pc_is_unlock_all_enabled()` is an early-out in three of the four unlock
predicates (`gm_1601.c:2273`, `:2402`, `:2463`) but not the fourth, so from
identical all-zero save data the two hosts answered differently, one
`HSD_Randi(100)` alt-BGM roll was skipped on one side, and the seeds parted for
ever. A host-local preference file was an input to the simulation.

**The fix** makes the card-absent default itself unlocked, in
`gmMainLib_8015F600` immediately after the `memzero` that establishes it
(`gmmain_lib.c:1312-1316`), gated on `aurora_card_is_present()` — the
`--no-card` flag, exported for this at
`extern/aurora/lib/dolphin/card.cpp:42`. It sits at the one point every path
that rebuilds the default save routes through: boot, soft reset, data delete,
and the card-absent memory-card scene — which is where an earlier version of
the fix was silently undone, because with `--no-card` the game still enters
that scene and rebuilds the default (found with a gdb watchpoint on the mask).
`CARDProbe` was deliberately not the test: it also answers false when a card
image is merely missing, which would silently unlock a fresh install. Three
alternatives were rejected for reasons worth recording — adding the missing
early-out to the fourth predicate puts the simulation's dependency on
`launcher.cfg` in *one more* place (that preference is read at ~60 predicate
call sites, several gating RNG draws: `ground.c:1404`, `:1413`, `:1431`,
`gmmain_lib.c:875`, `it_279C.c:1221`); deriving the masks from the preference
keeps the host-local file as the input; removing the early-outs deletes the
feature for card users.

With the masks full, all four predicates answer from save data on every
platform whatever `launcher.cfg` says, and the harness reports the windows row
`identical` over all 2400 frames on both verdicts. The property that matters on
one machine was tested too: two instances with separate `launcher.cfg`, one
`unlock_all 1` and one `0`, now force the same masks — pre-fix they answered
the predicate differently, which is the same-machine form of the same bug.

A second, unrelated LP64 defect was fixed in the same function: the default
save was cleared with a literal `memzero(…, 0x10A30)` — the GameCube size of
`struct gmm_x0`, while the struct measures `0x10C88` on this build because
pointers inside it grew — leaving the last `0x258` bytes unreset on every soft
reset and data delete. Invisible at boot, stale afterwards; it is `sizeof(*…)`
now (`gmmain_lib.c:1414`).

**Read this as a class, not an incident.** Save-data-derived predicates gate
RNG draws, so *any* disagreement in unlock state between two peers makes one
draw and the other not, and they desync on that frame with identical fighter
state — which is also the best explanation for the DESYNC §12.2 reports in the
online flow. The wire side of that is now closed: unlock state is hashed into
the handshake and forced for the session (§6.4). A future predicate that reads
some *other* host-local state reintroduces the class.

One suspect from the original list survives, untested and no longer needed for
this fork: **reads of uninitialised heap**. `-ftrivial-auto-var-init=zero`
covers automatic storage but not OSAlloc cells; `MELEE_HEAP_CHECK=1` and
aurora's canary/backtrace support are the tools if a future divergence points
that way. They were not run, and no claim about heap is made here.
Kept for the record, since both were checked and both stay true: the two
targets compile the game with identical flags (`-O2 -DNDEBUG -std=gnu11
-fpermissive -fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math
-fno-builtin-{sinf,cosf,tanf,atanf} -ftrivial-auto-var-init=zero
-fexec-charset=CP932`, differing only in `-fPIC` versus `-mno-ms-bitfields`,
GCC 16.2.1 and 16.2.0, neither with FMA in the baseline); and although the
platform libms *do* differ — 20000-sample bit comparison of glibc against
mingw-w64 under Wine: `atan2f` 3276 mismatches, `acosf` 1537, `asinf` 1369,
`sinf` 245, `cosf` 231, all exactly 1 ULP, `powf`/`expf`/`logf`/`fmodf`/`sqrtf`
0 — routing the simulation's trig into the tree (`pc_trig.h`) removes them as
a suspect. `sizeof(long)` differs (8 versus 4) but every struct the checksum
reads measures the same on both: `Fighter` 11584 bytes with `cur_pos` 196,
`facing_dir` 64, `motion_id` 20, `dmg` 8112; `HSD_GObj` 96 with `classifier` 0
and `user_data` 72.

**Two measurement defects had to be fixed before any of this was trustworthy**,
and they are worth knowing as a class. First, a run was not reproducible from
its own recording: the same binary replaying what it had just recorded diverged
at frame 1359 (1066 on another recording) *reproducibly*, while two independent
replays of that recording were bit-identical to each other — the first
differing frame was always the first frame of an input transition. Second, the
instrument moved what it measured: with the state log going through
`pc_log_line` (which flushes stderr *and* the log file on every call, four
flushes a frame) the divergence moved from 1359 to 1360, and turning the log on
could take a replay from "identical over 1376 frames" to "diverges at 1359".
The state log now writes its own block-buffered file, one write per 8 frames,
and is verified non-perturbing: with it on, the record leg, the canonical
replay and a second replay agree on all 2400 frames.

**The Windows build, and three defects that had to be fixed to get one.** The
numbers above come from a `build-win` deleted and rebuilt from scratch, which
now starts under Proton with no hand-copying at all. Getting there took three
separate fixes, each worth remembering as a class:

1. **It did not compile.** `net.c` declared `enum { RC_NONE, RC_ACTIVE,
   RC_FAILED }` and mingw-w64's `wingdi.h` defines `RC_NONE` as an empty macro
   (raster capabilities), producing 13 errors. The enumerators are `RSM_*` now
   (§6.5). Short, generic, all-caps names in `src/pc` want checking against
   the Win32 headers.
2. **`nod.dll` was never staged.** `aurora_copy_runtime_dlls` copies each
   imported target's `IMPORTED_LOCATION` *file name*, and `nodConfig.cmake`
   points that at `bin/libnod.dll`, while the MinGW import library in the same
   package was built against `nod.dll` — which is the name in our import
   table. The prebuilt ships both, byte-identical. `find_package(nod)` ran in
   aurora's directory scope, so the target is not visible at the top level and
   the file has to be found on disc; `CMakeLists.txt` now globs
   `${CMAKE_BINARY_DIR}/_deps/*nod*/bin` and copies it POST_BUILD.
3. **`libwinpthread-1.dll` was never staged** — a toolchain file, in no
   runtime-DLL set, which `tools/package_windows.sh` found for the zip, so
   this only ever broke running *from the build directory*. The same
   POST_BUILD loop finds it in the sysroot's `bin/`.

Together those were the `c0000135` (`STATUS_DLL_NOT_FOUND`) that killed the
process before any log line existed. Packaging passes both of its gates —
"14 binaries, all imports resolve" and "clean (no `MELEE_FP_PERTURB` marker)" —
each injection-validated on a *copy* of the stage directory, because the
script rebuilds the real one every run: deleting `nod.dll` from the copy fires
`MISSING nod.dll <- melee.exe`, a perturbed image fires the marker gate. One
allowlist fix was needed: `IPHLPAPI.DLL` is a stock Windows DLL that entered
our import table with LAN discovery (`GetAdaptersAddresses`) and is now in
`OS_DLLS`. `MELEE_STATE_SECTIONS` is untouched: Windows still has no snapshot
region and still runs lockstep (§4) — no attempt was made to bend
`melee_state.ld` onto PE, because PE/COFF has no `INSERT AFTER`.

One environment trap for anyone repeating this: the disc must be handed to
Wine on a path without this checkout's trailing space, and a disc that cannot
be opened does not fail loudly — `pc_launcher_run` falls through to the RmlUi
launcher and waits (`src/pc/launcher.cpp:981-995`).

Reproduce: `python3 tools/net_determinism.py --frames 2400` (all rows, ~12 min),
`--only linux,linux-flip` (no Proton, ~2 min), `--rec <file> --only windows`.
Work directory `/tmp/net_determinism`; Android and macOS print the exact
invocation they would use next to the missing prerequisite.

### Android (aarch64) against Linux (x86-64): what actually diverges

Measured on real hardware — a Pixel 8 Pro (Tensor G3) and an SM-T505
(Snapdragon), both against this x86-64 Linux box. Nearly every candidate is
dead, and the two that were real are not arithmetic at all.

**Ruled out, each by measurement, not argument:**

| Class | How it was killed |
|---|---|
| FMA contraction | `llvm-objdump` of the shipped `libmelee.so`: 1571 `fmadd` in the binary, **0** in any of its 9392 game symbols. `-ffp-contract=off` reaches every sim TU on both targets |
| Platform libm | `atan2f`/`asinf`/`acosf` are decomp code (`src/melee/lb/lbtrigf.c:22,45,61`), defined inside the binary, so the PLT binds locally. Linking the REAL build objects from each target into one probe and running 200k inputs on each machine gives identical hashes for `atan2f`/`asinf`/`acosf`/`pc_sinf`/`pc_cosf`/`pc_atanf`. (glibc and bionic *do* differ on those functions — 22334/200000 inputs for `atan2f` — which is why the vendored trig exists, but the game never calls them) |
| float→int UB | All 986 melee_game TUs rebuilt with `-fsanitize=float-cast-overflow,float-divide-by-zero`: **zero** reports over 3526 match frames, against an injected control that fires every run. The shapes genuinely differ per ISA (aarch64 `fcvtzs` saturates, x86 `cvttss2si` yields INT_MIN) but the simulation never feeds one an out-of-range value |
| `long double` | Zero uses in the tree |
| NaN sign/payload, min/max, signed zero, denormals | Identical on both; and the state log over 4001 frames contains no NaN, inf or denormal in any checksummed field |
| Runtime FP mode | FPCR on the device is `0x0` (FZ and DN off) after Vulkan init; MXCSR `0x1f80`. Zero `msr fpcr` in the shipped library |
| Compile flags | Identical on both targets for every sim TU, including `-fexec-charset=CP932`, which `tools/gcc_launcher.py:108` adds unconditionally even though CMake records Android's compiler as Clang and omits it from `build.ninja` |
| Plain `char` signedness | The one default that was *not* identical: AArch64 GCC makes `char` unsigned, x86-64 (and mwcc, the original compiler) signed. Compiling every game TU both ways changes 33 functions on x86-64 and 53 on AArch64 (64 distinct). Read line by line, only one computes something different: `fn_802FE470`, the unlock-notice scene (`GS_PRIZE_INTERFACE`), whose `char x2 = -1` lead-in counter skipped its six frames on ARM, outside netplay. The rest pick a different load or register, or test `u8 != (char) -1` before a loader that rejects index 255 either way. `-fsigned-char` on every first-party target (`CMakeLists.txt`) removes the AArch64 difference outright |

**The two real causes, both fixed.** Neither is a floating-point problem:
they are the seed itself (`net_snapshot.c:148` folds it, and outside a fight
the checksum is *only* the pads and the seed).

1. `gmTitle_801A165C` stirred the RNG once per wall-clock second-of-minute.
   Two machines reaching the title eight seconds apart stirred 41 times
   against 49. Suppressed under `pc_net_deterministic()`.
2. Scene-enter draws (stage init, CPU AI init in `Fighter_Create`) land on
   different frames on machines of different speed. `seed_out_of_tick_check`
   restores the seed a tick starts from, netplay only.

**Scene entry is not frame-aligned; mostly fixed, not closed.** A load spans
a device-dependent number of SIMULATED frames, because the game polls a
read's completion once per tick: a peer whose file cache is warm finishes in
one tick while the other spends hundreds. Both peers then enter the same
scene on different `net.frame` values and their sims run different code with
nothing wrong in the arithmetic.

Caught end to end on a real tablet<->PC match (SM-T505 P1 vs this PC P2,
driven through the genuine online flow: lobby -> CSS -> SSS -> match, with
`sss: picks P1=14 P2=14 -> 14` on both). The tablet was in the fight at
frame 39470 and the PC was not — identical seed `cc8a2134`, identical pads —
and `net: DESYNC` followed at once, because `frame_checksum` folds fighter
fields only while `in_fight()`. The two logs realigned 883 frames (~15 s)
later, which is how far apart the peers entered the scene. The tablet
prewarms 42 archives at boot; this PC read them cold.

`dvd_settle()` (src/pc/net.c) now drains `aurora_dvd_inflight()` and
`aurora_arq_inflight()` at the top of every netplay tick, so a read
completes inside the tick that issued it and the poll succeeds on the same
tick for both peers. Measured on the same pairing: the scene-entry gap fell
from **883 frames to about 30** (fight entered at 4412 on the PC, 4442 on
the tablet), which is a 30x improvement but still a desync.

The residual is the remaining deliverable, and it now reproduces on this one
machine in about five minutes, with no phone:

```sh
python3 tools/net_test.py --lan --state-log --minutes 1   # add --cold-cache
```

What differs between the peers is not speed, it is *where in the tick* a
read completes. A warm/warm pair on one machine hits it: measured on
`b445723b7`, A left the SSS for the match at frame **3169** and B at
**3168**, and `net: DESYNC` fired on the frame the first peer started
folding fighter fields. The same pairing with A slowed to 1.5 ms per disc
read (`--cold-cache`, which also disables its prewarm) entered every scene
on the same frame and passed - a uniformly slow peer is *more* aligned than
a peer that is merely different, because every one of its reads then spans
a tick boundary instead of racing it.

`net: scene N -> M at frame F` (src/pc/net.c) is the line to diff, and
`--state-log` writes `<work>/a.state` and `b.state` for the field-by-field
comparison.

Two fixes at the I/O layer were tried and both are dead, each for a reason
worth keeping:

1. **Deliver a completion only at a tick boundary** (worker parks until the
   game thread opens a gate in `dvd_settle`). Wedges the CSS load: the game
   polls a block's status inside a tick, so a completion that waits for the
   next tick is never reached.
2. **Run disc commands inline on the caller's thread** so a read costs zero
   ticks everywhere. Wedges in the same place for the opposite reason:
   `HSD_DevComDVDWakeUp` (src/sysdolphin/baselib/devcom.c:346-389) sets its
   busy flag `HSD_DevCom_804D77F5 = 1` *after* `DVDReadAsyncPrio` returns.
   On hardware interrupts are off there so the callback cannot run first; an
   inline callback clears the flag and the outer call then re-sets it, and
   the queue never wakes again.

What both attempts prove is that the load's tick cost cannot be equalised
from underneath. The transition frame has to be agreed instead, the way the
lobby already hands over to the CSS: each peer announces "ready to leave
scene X at frame N" and both leave at `max(Na, Nb)`
(`gm_Scene_OnlineLobby_OnFrame` + `pc_lan_start_frame` is the pattern). The
hook is the break path of `gm_801A4D34` (src/melee/gm/gmscene.c:379-390):
scene loads run inside that tick loop, which is why their cost is counted in
frames at all, and it is the one place every scene exit passes through.

Separately, the residual single-field desync around frame 3200 of the
`--lan` row reproduces on the PRE-fix binary and on two identical PCs, so it
is not cross-architecture either.
