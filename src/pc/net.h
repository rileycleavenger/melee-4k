/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_NET_H
#define PC_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's hooks for online play (src/pc/net.c). The session itself --
 * lobby, transport, lockstep -- is src/pc/online.c (online.h); this is what
 * the simulation asks while one runs. docs/online.md has the design. */

struct OnlineSetup;

void pc_net_init(void);
/* Lockstep from the next tick on (character select's first frame), with the
 * setup every machine agreed on; and back to offline play. */
void pc_net_session_begin(const struct OnlineSetup* setup);
void pc_net_session_end(void);
/* Leave the session and the lobby (quitting the game, closing the window). */
void pc_net_disconnect(void);

bool pc_net_active(void);
/* True when the simulation must be reproducible elsewhere: online, record
 * or replay. Guards machine-seeded retail behaviour. */
bool pc_net_deterministic(void);
bool pc_net_pure_load(const char* filename); /* always false: kept for callers */

/* Replaying a recording: true while the scene that asked to end must keep
 * ticking, so it ends on the frame the recording's did (gmscene.c). */
bool pc_net_scene_hold(void);
/* Controller port the local player drives, 0-3. */
int pc_net_local_player(void);

/* Frame of the next tick; identical on every machine in a session. */
int32_t pc_net_frame(void);
int32_t pc_net_start_frame(void);

/* RNG seed of the session. */
uint32_t pc_net_seed(void);

/* Rules the simulation reads that live outside the game's data, the host's
 * for the session: unlock-all and frozen stadium. False offline (use the
 * local prefs). */
bool pc_net_rules(bool* unlock_all, bool* frozen_stadium);

/* Called once per simulation tick before the pad queue head is consumed:
 * waits for the frame's inputs from the host and writes them into the head,
 * all four ports. */
void pc_net_sync(void);

/* Called after each tick. Returns true when the tick must be run again
 * (never, in lockstep; kept for gmscene.c's loop). */
bool pc_net_after_tick(bool scene_ending);
void pc_net_render_audit(bool after); /* MELEE_NET_RENDER_AUDIT, net_snapshot.c */

/* Frame pacing (src/pc/vi.c): lockstep waits inside pc_net_sync, so pacing
 * runs as offline. */
uint64_t pc_net_pace_adjust_ns(void);
uint64_t pc_net_catch_up_ns(uint64_t late_ns);

/* True while re-simulating (never, in lockstep). */
bool pc_net_resim(void);
/* Rumble: online only our own port's, on this machine's first controller. */
void pc_net_rumble_command(unsigned port, unsigned command);

/* Audio questions the simulation asks (axdriver.c). In lockstep a frame is
 * simulated once, so "did this sound start" needs nothing; "is that voice
 * still playing" is a wall-clock answer that differs between machines, so
 * during a session it is always "no" (true return, *answer = false). */
bool pc_net_audio_replay(int32_t* out);
int32_t pc_net_audio_record(int32_t v);
bool pc_net_audio_deaf(bool* answer);
void pc_net_audio_deaf_note(bool live);
bool pc_net_sfx_on(void);

/* Called whenever the game issues a disc request (nothing to do: every
 * transfer is drained before a lockstep tick). */
void pc_net_note_io(void);

/* Live numbers for the HUD. False when not online. */
bool pc_net_stats(int* ping_ms, int* delay_frames, unsigned* rollbacks);
/* Link quality for the HUD: 0 good, 1 fair, 2 poor. */
int pc_net_quality(void);
/* True once the session ended because the machines' games differed. */
bool pc_net_desync(void);

/* Why the last session ended (kept until cleared). */
enum {
    PC_NET_PEER_OK,
    PC_NET_PEER_LEFT,
    PC_NET_PEER_TIMEOUT,
    PC_NET_PEER_DESYNC,
    PC_NET_PEER_INCOMPATIBLE,
    PC_NET_PEER_RESUME
};
int pc_net_peer_status(void);
void pc_net_peer_status_clear(void);

#ifdef __cplusplus
}
#endif

#endif
