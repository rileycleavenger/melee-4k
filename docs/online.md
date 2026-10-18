# Online play

Host Lobby / Join Lobby for 2-4 players, Mac and PC together, with no servers
and no third-party services. Rebuilt for v1.1 after the v1.1.0-rc1 test, where
melee-pc's rollback netcode (BitTorrent DHT discovery, signed identities,
ranked and unranked sets, a multicast host election, quick chat, all built
around exactly one remote peer) connected but did not play: the two machines
started character select from different rules and the session timed out. The
old design is in git history (`docs/netcode-plan.md` before v1.1).

## The rule

**Same starting state + same inputs every frame = same game.** The game is
deterministic across Mac and Windows (docs/determinism.md; proven by replaying
one machine's recording on the other, `tools/crossplay/`). Online play
guarantees both halves of the rule and checks the result continuously.

## How a game runs

```
 host (P1)                         guest (P2..P4)
 ─────────                         ──────────────
 lobby: code, players, pings  <──  JOIN (code's token, name, build)
 Start ──► SETUP + game data  ──►  loads the host's game data
           GO                 ──►  character select, frame 0
 every frame f:
   own input for f+D               INPUT: own input for f+D, newest bundle
   bundle f = every port's    ◄──  held, a state hash every 30 frames
   input for f                ──►  BUNDLE: bundles the guest lacks
   simulate f from bundle f        simulate f from bundle f
```

- **Star topology.** The host owns the session; guests talk only to the host,
  so only the host has to be reachable. Ports are assigned by the host: the
  host is P1, guests P2-P4 in join order. Each machine drives only its own
  port, from its first controller (the keyboard is merged into it).
- **Lockstep.** Every machine, the host included, simulates frame f only from
  the host's bundle for f: everyone's input for that frame. Nothing is
  predicted, so every machine always feeds the game identical inputs. A
  machine's own input for frame f is taken D frames earlier (the input
  delay), which hides the round trip.
- **Input delay D** is fixed per game: from the slowest guest's round trip,
  `ceil(ping * 60 / 1000) + 1` frames, 2 to 8 (2 on the same Wi-Fi). The host
  can set it instead (F1 > Online > Input delay). A late bundle holds the
  frame: the game waits (pumping window events, so the window stays
  responsive) rather than simulate without it.
- **The same start.** At Start the host sends every guest its persistent
  game data (struct `gmm_x0`, 68 KB: rules, VS modes and their teams, unlocks,
  names, random-stage history), the seed, the delay, each player's port, and
  the host's Unlock All and Frozen Stadium settings. Every machine loads it
  and enters character select together; it is logged with a hash
  (`online: game data loaded (..., hash X)`), and the test compares them. The
  function pointers inside the VS-mode rules are process addresses, so each
  machine keeps its own (`keepOwnPointers`). While a session runs, the memory
  card is read-only (`aurora_card_set_read_only`): nothing is saved over a
  guest's own data, and no machine's save completes on a different frame
  than another's no-op. Back in the lobby, each machine gets its own data
  back.
- **Detection.** Every 30 frames each machine hashes its state (pads, seed,
  every fighter's position, facing, percent, action, stocks and velocities:
  `frame_checksum`, net_snapshot.c, mixed with the scene) and sends it to the
  host, which compares. A difference ends the game for everyone with "Out of
  sync - back to the lobby", and each machine dumps its last frames' state
  for diagnosis (`MELEE_NET_STATE_LOG`).
- **Ending.** B on character select takes everyone back to the lobby on the
  same frame (it is an input like any other). A player leaving, a 15 s
  silence or a desync ends the game for everyone with the reason (END), and
  the lobby stays open. The host starts no new game until every guest has
  every bundle of the last one (`players_caught_up`), so nobody is still
  finishing it.

What else keeps every machine's simulation the same (src/pc/net.c):

- **Loads.** Every disc and ARAM transfer is drained before each lockstep
  tick (`dvd_settle`), so a load finishes inside the tick that issued it.
- **Random seed.** A draw outside a tick (the render phase, the audio thread)
  is undone before the next tick; in a fight the seed each tick starts from
  is a function of the session seed and the frame (`fight_reseed`).
- **Sound.** A sound start hands the game a handle made of the frame and the
  call order, never the engine's voice id, which depends on when earlier
  voices ended (pc/net_sfx.h); "is this voice still playing" is always "no"
  online (`pc_net_audio_deaf`).
- **Settings.** Every setting the simulation reads is the host's or fixed
  online: Unlock All and Frozen Stadium are the host's (`pc_net_rules`), UCF
  is on and the free camera off (launcher.cpp).
- **Mouse.** The pointer moves menu cursors directly, not through a stick,
  so online it travels with the input: each tick character or stage select
  samples this machine's pointer in its own world (`pc_mouse_menu_port`),
  it goes out with the next input (`OnlinePad.ptr_x/ptr_y`, 1/256 units),
  and every machine moves a port's hand to that port's pointer from the
  bundle. A stick or d-pad in the same input cancels it. `tools/test_sim_prefs.py`
  fails on any new setting the game reads until it is classified.

## Finding the host

The lobby code (src/pc/net_code.c) is 15 characters, `XXXXX-XXXXX-XXXXX`:
the host's public IPv4 address, its UDP port, a 17-bit token and a 10-bit
check, in RFC 4648 base32. The check rejects every single-character typo and
every swap of two neighbours before anything is sent; 0, 1 and 8 read as O, I
and B. A code is pasted from the clipboard if one is there (Y, or Ctrl/Cmd+V
while typing).

The host's router is asked to open UDP 41000 (NAT-PMP, then UPnP-IGD:
src/pc/net_portmap.c) and the code carries the public address it reports.
What the router allowed decides what the lobby says: open to the internet;
"same Wi-Fi only" when the router refused (forward UDP 41000 by hand); or no
public address at all (a second router or carrier-grade NAT), when the code's
address is 0. A joiner sends JOIN both to the code's address and as a
broadcast to port 41000 on its own network, and takes whichever host answers
with the code's token, so the same code works on the same Wi-Fi even when the
router does not loop back to its own public address.

Windows: Setup adds a firewall rule for the game. macOS: Info.plist declares
local-network use, so macOS asks once.

## Protocol

One UDP socket per machine. Every datagram starts with a header: magic
`M4K1`, protocol version, message type, the sender's slot, the session id and
the build id (a hash of the app version). Datagrams from another build are
dropped, except as the next section says. Messages, inputs and bundles are listed at the top of
src/pc/online.c. Inputs and bundles are unreliable and redundant: every packet
repeats each frame the other side has not acknowledged, up to 32, so a lost
packet costs only the next one. The setup data goes in 1 KB chunks, resent
until each guest's bitmap has them all and the data's hash matches.

## Versions and custom fighters

**Only the latest release plays online.** At boot the game asks GitHub for
the newest non-prerelease release of rileycleavenger/melee-4k
(`pc_latest_release_start`, src/pc/updater.cpp; in the background). Hosting
and joining wait for that answer (`latest_or_fail` in src/pc/online.c) and
are refused when this copy is older: "Online needs the latest Melee 4K
(vX): Check for Updates in the menu". A copy newer than the latest release
(a test or pre-release build) may play. The answer is cached in the user
folder (`melee4k-latest_release`, the tag and when); when GitHub cannot be reached
a cached answer under a week old stands in, and without one online is
refused ("Couldn't reach GitHub to check for the latest version").

A host then lets in only its own build: a JOIN from any other version is
refused ("The host has another Melee 4K version: both need the latest").
So everyone in a lobby runs the same latest version.

Custom fighters (Melee++ only; Melee 4K loads none): the JOIN's extension
(`JoinExt`, magic `M4KX`) carries the content hash of the custom fighter
packs. The same
packs play with custom fighters; different packs play with the **base
fighters only**, for the whole lobby: the host marks its packets
(`Hdr.flags`, `HDR_BASE_ONLY`), custom fighter icons are hidden on
character select, and the lobby shows "Base fighters only".

Tests: `tools/online_test.py` runs both sides on this build (both pass the
gate); `--guest-env MELEE_APP_REV=v9.0.0` poses as another version (the
host refuses it), `MELEE_FIGHTERS_DIR` in `--guest-env` gives one side
other packs (base fighters only). `MELEE_ONLINE_LATEST=<tag>` sets the
latest release without asking GitHub; `MELEE_APP_REV=<tag>` makes this copy
that version (an older one is refused).

Timeouts: 10 s of silence in the lobby, 15 s in a game, 20 s for a join to
be answered.

## Files

| File | |
|---|---|
| `src/pc/online.c`, `online.h` | lobby, transport, setup, lockstep |
| `src/pc/net.c`, `net.h` | the game's side: the per-tick hook (`pc_net_sync`), the rules above |
| `src/pc/net_snapshot.c` | frame checksum, record/replay, state log |
| `src/pc/net_sfx.c` | sound handles |
| `src/pc/net_code.c` | lobby codes (unit test `tools/test_net_code.c`) |
| `src/pc/net_portmap.c` | NAT-PMP / UPnP |
| `src/melee/gm/gmonlinemode.c` | the GM_ONLINE scene flow and the lobby scene |
| `src/melee/mn/mnonlinelobby.c` | draws the lobby |
| `src/melee/if/ifnet.c` | the in-game line: your port, delay, ping |
| `tools/online_test.py` | 2-4 instances on one machine through real games |

## Testing

`tools/online_test.py` (docs/testing.md) runs a host and 1-3 joiners on one
machine through character select, stage select, a match, results and back to
the lobby, and compares every joiner's per-frame checksums with the host's.
Results on an M-series Mac, 2026-10-12:

| Run | Result |
|---|---|
| 2 players, 2 games | pass, 7,064 joiner frames identical |
| 4 players, 2 games | pass, 19,797 joiner frames identical |
| 3 players, 5% loss, 50 ms delay, 20 ms jitter (delay 8 chosen), 2 games | pass, 15,880 |
| 3 players, joiners' own rules changed (teams, 1 stock) | pass: all loaded the host's data (same hash), 6,794 frames identical |

Mac against Windows: `tools/crossplay/` replays one machine's recording on
the other; the real gate is two computers (same Wi-Fi with each machine
hosting once, then across networks).

## Environment variables

| Variable | |
|---|---|
| `MELEE_BOOT_SCENE=host\|join` | Boot straight into Host Lobby or Join Lobby. |
| `MELEE_ONLINE_JOIN=<code>` | Join Lobby: join this code at once. |
| `MELEE_ONLINE_AUTOSTART=<n>` | Host: start as soon as n players are in. |
| `MELEE_ONLINE_LOSS=<pct>`, `MELEE_ONLINE_DELAY_MS=<ms>`, `MELEE_ONLINE_JITTER_MS=<ms>` | Link simulator on every packet this machine sends. |
| `MELEE_PORTMAP_OFF=1` | Leave the router alone (tests). |
| `MELEE_PORTMAP_UPNP=1` | Skip NAT-PMP, to test the UPnP path. |
| `MELEE_TEST_LOCAL_RULES=1` | Change this machine's own rules (teams, 1 stock) before the lobby opens. |
| `MELEE_NET_RECORD=<file>` | Record every frame's pads and checksum; online, each game is a new section (header) and frames outside a game are not recorded. |
| `MELEE_NET_REPLAY=<file>` | Replay a recording offline and report the first frame that differs. |
| `MELEE_NET_STATE_LOG=<file>` | The state behind each frame's checksum, readable and as raw bits. |
| `MELEE_USER_DIR`, `MELEE_CACHE_DIR` | Separate settings and caches for several instances on one machine. |
