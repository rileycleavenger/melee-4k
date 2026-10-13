/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The game's side of online play: once per simulated tick, feed it the
 * frame's synced inputs from online.c, and keep everything else the tick
 * depends on identical on every machine. The rules that make that true:
 *
 *   - Inputs. The tick reads the four ports from the pad queue's head;
 *     pc_net_sync overwrites the head with the host's bundle for the frame
 *     (online_wait_bundle), so no machine ever simulates a local sample.
 *     The queue's read cursor must not move under it (pad_qtype_hold).
 *   - Loads. The game polls a read's completion once per tick, so every
 *     disc and ARAM transfer is drained before a tick (dvd_settle): a load
 *     finishes inside the tick that issued it, whatever the disc speed.
 *   - The random seed. A draw outside any tick (the render phase, another
 *     thread) is undone before the next tick, and in a fight the seed each
 *     tick starts from is a function of the session seed and the frame.
 *   - Sound. A sound start hands the game a handle made of the frame and
 *     the call order, never the engine's voice id (pc/net_sfx.h), and "is
 *     this voice still playing", a wall-clock answer, is always no online
 *     (pc_net_audio_deaf).
 *   - Settings the simulation reads come from the host (pc_net_rules) or are
 *     fixed online (free camera off, UCF: launcher.cpp).
 *
 * Every 30 frames the frame's checksum goes to the host (online_note_hash),
 * which ends the game for everyone if any machine differs.
 *
 * Offline, the same hook runs MELEE_NET_RECORD / MELEE_NET_REPLAY, the
 * determinism tests (tools/interp_determinism.py, tools/crossplay). */
#include "compat.h"
#include "pc/net_internal.h"
#include "pc/net_sfx.h"
#include "pc/online.h"

#include <aurora/dvd.h>
#include <dolphin/ar.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <melee/gm/gmvsmelee.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NetSession net;

static SDL_ThreadID s_game_thread;
static int s_status = PC_NET_PEER_OK; /* why the last game ended early */
static uint32_t s_seed_after_tick;    /* seed at the end of the last tick */
static bool s_seed_have;

extern struct GameSceneInfo* gm_804D6720; /* current scene, gmscene.c */

int scene_kind(void) {
    return gm_804D6720 != NULL ? gm_804D6720->scene_kind : -1;
}

bool in_fight(void) {
    const int k = scene_kind();
    return k == GS_VS || k == GS_SUDDEN_DEATH;
}

uint32_t fnv1a(uint32_t h, const void* data, size_t n) {
    const uint8_t* b = data;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

/* ---- the session, as the game sees it ---------------------------------------- */

void pc_net_init(void) {
    s_game_thread = SDL_GetCurrentThreadID();
}

void pc_net_session_begin(const OnlineSetup* setup) {
    memset(&net, 0, sizeof net);
    net.active = true;
    net.local = setup->local_port;
    net.players = setup->players;
    net.delay = setup->delay;
    net.seed = setup->seed;
    net.unlock_all = setup->unlock_all != 0;
    net.frozen_stadium = setup->frozen_stadium != 0;
    s_status = PC_NET_PEER_OK;
    s_seed_have = false;
    net_sfx_reset();
    record_session_begin();
    *HSD_RandSeedPtr = net.seed; /* before character select draws anything */
    online_lockstep_begin();
    pc_log_line("net: game starts: port %d of %d, delay %d, seed %08x", net.local + 1, net.players,
        net.delay, net.seed);
}

void pc_net_session_end(void) {
    if (net.active) {
        pc_log_line("net: game over at frame %d", net.frame);
    }
    net.active = false;
    online_lockstep_end();
}

/* The game ended early (a player left, out of sync, lost connection). */
static void session_failed(void) {
    if (s_status == PC_NET_PEER_OK) {
        switch (online_error()) {
        case ONLINE_ERR_DESYNC:
            s_status = PC_NET_PEER_DESYNC;
            dump_states_around(net.frame);
            break;
        case ONLINE_ERR_TIMEOUT:
            s_status = PC_NET_PEER_TIMEOUT;
            break;
        default:
            s_status = PC_NET_PEER_LEFT;
            break;
        }
        pc_log_line("net: game ended at frame %d: %s", net.frame, online_error_text());
    }
}

bool pc_net_active(void) {
    return net.active;
}

bool pc_net_deterministic(void) {
    return net.active || record_active();
}

int pc_net_local_player(void) {
    return net.local;
}

int32_t pc_net_frame(void) {
    return net.frame;
}

int32_t pc_net_start_frame(void) {
    return 0;
}

uint32_t pc_net_seed(void) {
    return net.seed;
}

bool pc_net_rules(bool* unlock_all, bool* frozen_stadium) {
    if (!net.active) {
        return false;
    }
    *unlock_all = net.unlock_all;
    *frozen_stadium = net.frozen_stadium;
    return true;
}

int pc_net_peer_status(void) {
    if (net.active && s_status == PC_NET_PEER_OK && !online_lockstep_active()) {
        session_failed();
    }
    return s_status;
}

void pc_net_peer_status_clear(void) {
    s_status = PC_NET_PEER_OK;
}

bool pc_net_desync(void) {
    return s_status == PC_NET_PEER_DESYNC;
}

bool pc_net_stats(int* ping_ms, int* delay_frames, unsigned* rollbacks) {
    if (!net.active) {
        return false;
    }
    *ping_ms = online_ping_ms();
    *delay_frames = net.delay;
    *rollbacks = 0;
    return true;
}

int pc_net_quality(void) {
    const int ping = online_ping_ms();
    return ping < 60 ? 0 : ping < 120 ? 1 : 2;
}

/* ---- what lockstep makes simple ------------------------------------------------- */

bool pc_net_resim(void) {
    return false; /* a frame is simulated once */
}

bool pc_net_sfx_on(void) {
    /* The audio thread keys voices off too: only the game thread's calls are
     * the simulation's. */
    return net.active && SDL_GetCurrentThreadID() == s_game_thread;
}

bool pc_net_audio_replay(int32_t* out) {
    (void)out;
    return false;
}

int32_t pc_net_audio_record(int32_t v) {
    return v;
}

/* "Is that voice still playing": no, online, because the true answer is a
 * wall-clock fact and every machine's audio clock is its own (axdriver.c). */
bool pc_net_audio_deaf(bool* answer) {
    if (!net.active || SDL_GetCurrentThreadID() != s_game_thread) {
        return false;
    }
    *answer = false;
    return true;
}

void pc_net_audio_deaf_note(bool live) {
    (void)live;
}

void pc_net_note_io(void) {}

bool pc_net_pure_load(const char* filename) {
    (void)filename;
    return false;
}

bool pc_net_scene_hold(void) {
    /* Replaying a recording: the scene ends where the recording's did. */
    return !net.active && record_replay_scene_hold(net.frame);
}

/* Rumble only for our own controller: the others' are on other machines. */
void pc_net_rumble_command(unsigned port, unsigned command) {
    static int motor[4] = {-1, -1, -1, -1};
    if (port >= 4) {
        return;
    }
    if (net.active) {
        if ((int)port != net.local) {
            return;
        }
        port = 0; /* our controller is this machine's first */
    }
    if (motor[port] == (int)command) {
        return;
    }
    PADControlMotor((s32)port, command);
    motor[port] = (int)command;
}

uint64_t pc_net_pace_adjust_ns(void) {
    return 0;
}

uint64_t pc_net_catch_up_ns(uint64_t late_ns) {
    return late_ns; /* behind: catch up; the other machines wait in lockstep */
}

/* ---- the tick ---------------------------------------------------------------------- */

static PADStatus* pad_head(void) {
    PadLibData* p = &HSD_PadLibData;
    return &p->queue->stat[p->qread * 4];
}

/* With qtype 0 a full raw queue moves its read cursor, merging and
 * overwriting the slot the synced inputs were just written to; qtype 2 drops
 * the new sample instead. HSD_PadInit resets it, so it is held every tick. */
static void pad_qtype_hold(void) {
    HSD_PadLibData.qtype = 2;
}

/* Re-expose the slot the last tick consumed when the queue is empty. */
static PADStatus* unconsume(void) {
    PadLibData* p = &HSD_PadLibData;
    if (p->qcount >= p->qnum) {
        return NULL;
    }
    p->qread = (uint8_t)((p->qread + p->qnum - 1) % p->qnum);
    p->qcount++;
    return &p->queue->stat[p->qread * 4];
}

static void dvd_settle(void) {
    const uint64_t t0 = SDL_GetTicksNS();
    while (aurora_dvd_inflight() > 0 || aurora_arq_inflight() > 0) {
        if (SDL_GetTicksNS() - t0 > 5000000000ull) {
            pc_log_line("net: a disc transfer is still running after 5 s at frame %d", net.frame);
            return;
        }
        SDL_DelayNS(200000);
    }
}

static int seed_steps(uint32_t from, uint32_t to) {
    for (int i = 0; i <= 4096; i++) {
        if (from == to) {
            return i;
        }
        from = from * 214013u + 2531011u;
    }
    return -1;
}

/* A draw between two ticks (the render phase, the audio thread) happens on
 * one machine and not another; undo it so every machine starts the tick
 * from the seed the last tick ended with. Online only: a recording leg must
 * keep the seed as it is (how draws fall around ticks depends on load
 * timing, so rewriting it makes a recording only replay on the same
 * machine). */
static void seed_out_of_tick_check(void) {
    if (!s_seed_have) {
        return;
    }
    const uint32_t now = *HSD_RandSeedPtr;
    if (now == s_seed_after_tick) {
        return;
    }
    static unsigned logged;
    if (logged++ < 3) {
        pc_log_line("net: seed moved outside the tick at frame %d (%d draws)%s", net.frame,
            seed_steps(s_seed_after_tick, now), net.active ? ", restoring" : "");
    }
    if (net.active) {
        *HSD_RandSeedPtr = s_seed_after_tick;
    }
}

/* In a fight the seed entering a tick is a function of the session seed
 * and the frame (Slippi's SyncRNG): a stray draw cannot carry over. */
static void fight_reseed(int32_t f) {
    if (!net.active || !in_fight()) {
        return;
    }
    uint32_t x = net.seed ^ ((uint32_t)f * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    *HSD_RandSeedPtr = x;
}

static OnlinePad to_online(const PADStatus* s) {
    OnlinePad p;
    p.button = s->button;
    p.stick_x = s->stickX;
    p.stick_y = s->stickY;
    p.sub_x = s->substickX;
    p.sub_y = s->substickY;
    p.trigger_l = s->triggerLeft;
    p.trigger_r = s->triggerRight;
    return p;
}

static void from_online(PADStatus* s, const OnlinePad* p, bool present) {
    memset(s, 0, sizeof *s);
    if (!present) {
        s->err = PAD_ERR_NO_CONTROLLER;
        return;
    }
    s->button = p->button;
    s->stickX = p->stick_x;
    s->stickY = p->stick_y;
    s->substickX = p->sub_x;
    s->substickY = p->sub_y;
    s->triggerLeft = p->trigger_l;
    s->triggerRight = p->trigger_r;
}

/* Our input for frame + delay: this machine's first controller (the
 * keyboard is merged into it), whatever port we play as. */
static void submit_local(void) {
    PADStatus pads[4];
    PADRead(pads);
    const OnlinePad mine = to_online(&pads[0]);
    online_submit(net.frame + net.delay, &mine);
}

void pc_net_sync(void) {
    static bool opened;
    if (!opened) {
        opened = true;
        record_open();
    }
    if (net.active) {
        pad_qtype_hold();
        dvd_settle();
    }
    if (net.active || record_active()) {
        seed_out_of_tick_check();
    }
    if (!net.active && !record_active()) {
        net.frame++;
        return;
    }
    if (net.active) {
        if (!online_lockstep_active()) {
            session_failed();
            return;
        }
        submit_local();
        if (!online_wait_bundle(net.frame)) {
            session_failed();
            return;
        }
    }
    /* After the wait: pad alarms delivered during it may have moved the
     * queue, so resolve the slot the tick will read only now. */
    PadLibData* p = &HSD_PadLibData;
    PADStatus* head = p->qcount > 0 ? pad_head() : unconsume();
    if (head == NULL) {
        head = pad_head();
    }
    if (net.active) {
        OnlinePad bundle[ONLINE_MAX_PLAYERS];
        online_bundle(net.frame, bundle);
        for (int port = 0; port < 4; port++) {
            from_online(&head[port], &bundle[port], port < net.players);
        }
    }
    replay_feed(head);
    if (net.active) {
        net_sfx_begin(net.frame, false);
    }
    fight_reseed(net.frame);
    const uint32_t ck = frame_checksum(head);
    record_state(head, net.frame);
    if (net.active && (net.frame % 30) == 0) {
        /* The scene too: two machines in different scenes have nothing
         * else in common to compare. */
        online_note_hash(net.frame, ck ^ ((uint32_t)scene_kind() * 2654435761u));
    }
    record_frame(head, ck, net.frame);
    record_confirm(net.frame);
    net.tick_frame = net.frame;
    net.frame++;
}

bool pc_net_after_tick(bool scene_ending) {
    (void)scene_ending;
    net_sfx_end();
    if (net.active || record_active()) {
        s_seed_after_tick = *HSD_RandSeedPtr;
        s_seed_have = true;
    }
    return false;
}

void pc_net_disconnect(void) {
    pc_net_session_end();
    online_leave();
}
