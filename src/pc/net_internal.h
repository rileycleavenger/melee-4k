/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Internals shared by net.c (the game's online hooks) and net_snapshot.c
 * (the determinism instruments). Game C callers use net.h; the session and
 * transport are online.c (online.h). */
#ifndef PC_NET_INTERNAL_H
#define PC_NET_INTERNAL_H

#include "pc/net.h"
#include "pc/pc.h"

#include <dolphin/pad.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wscalar-storage-order" /* disc-struct unions in lb/types.h */
#include <melee/gm/types.h>
#pragma GCC diagnostic pop

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The running online game, as the game thread sees it. */
struct NetSession {
    bool active;        /* lockstep running: every tick is a synced frame */
    bool resim;         /* always false: lockstep never re-runs a frame */
    int32_t frame;      /* next frame to simulate (0 = character select's first) */
    int32_t tick_frame; /* frame of the tick running now */
    int local;          /* our port, 0-3 */
    int players;        /* 2-4 */
    int delay;          /* input delay, frames */
    uint32_t seed;
    bool unlock_all, frozen_stadium; /* the host's, for this session */
};
extern struct NetSession net;

#define RING 64 /* frames of recording kept before they are written; power of two */

int scene_kind(void);
bool in_fight(void);
uint32_t fnv1a(uint32_t h, const void* data, size_t n);

/* ---- net_snapshot.c ----------------------------------------------------- */

uint32_t frame_checksum(const PADStatus* head);
void record_state(const PADStatus* head, int32_t frame);
void dump_states_around(int32_t frame);
const char* state_line(int32_t frame); /* one frame's recorded state line, "" if gone */
void record_open(void);
bool record_active(void);
void replay_feed(PADStatus* head);
void record_frame(const PADStatus* head, uint32_t ck, int32_t f);
void record_confirm(int32_t upto);            /* write settled frames out in order */
void record_session_begin(void);              /* an online session's frame 0 is next */
bool record_replay_scene_hold(int32_t frame); /* replay: hold to the recorded exit */

#endif
