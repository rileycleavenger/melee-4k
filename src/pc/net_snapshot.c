/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Determinism instruments: the per-frame checksum online play compares
 * (online.c), the state ring dumped when it differs, the state log, the
 * render audit and MELEE_NET_RECORD/REPLAY (the determinism and cross-play
 * tests). Game thread only (net_internal.h). */
#include "compat.h"
#include "pc/net_internal.h"
#include "pc/slp.h"

#include <dolphin/ar.h>
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/synth.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wscalar-storage-order" /* disc-struct unions in lb/types.h */
#include <melee/ft/fighter.h>
#include <melee/ft/inlines.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <melee/pl/player.h>
#pragma GCC diagnostic pop
#include <xxhash.h>

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- record / replay --------------------------------------------------
 * MELEE_NET_RECORD=file  writes the seed, then per frame the four PADStatus
 *                        actually simulated, checksum and tick-start seed.
 * MELEE_NET_REPLAY=file  feeds those pads back in and reports the first
 *                        frame whose checksum differs: the determinism test
 *                        for M0 (docs/determinism.md). Works solo or
 *                        together with netplay (record only).
 * MELEE_NET_STATE_LOG=file
 *                        writes record_state()'s line plus an exact-bits
 *                        line for EVERY frame to `file`, not just into the
 *                        DESYNC ring. A checksum says two platforms differ;
 *                        these say which field of which fighter differs,
 *                        which is the only way to localise a cross-platform
 *                        divergence (two files, one diff).
 *                        Its own file, block-buffered and flushed every 8
 *                        frames, deliberately NOT pc_log_line: that flushes
 *                        stderr and the log file on every call, and two of
 *                        those per frame slowed the loop enough to change
 *                        which frame the pad alarm fires on - the instrument
 *                        moved what it was measuring (the same replay went
 *                        from "identical over 1376 frames" to "diverges at
 *                        1359"). Off by default. */
static FILE* s_rec;
static int32_t s_confirmed = INT32_MIN; /* newest final netplay frame, for slp.c */
static FILE* s_rep;
static bool s_rep_reported;
static FILE* s_state_log;

typedef struct FrameRecord {
    PADStatus pads[4];
    uint32_t ck;
    uint32_t seed;
    /* The scene this frame was simulated in (scene_kind(), -1 before any).
     *
     * A netplay scene does not end when its own code asks to: both peers
     * hold it to a frame they agree on (net.c pc_net_scene_hold). An earlier
     * cut of MRC3 stored that agreed FRAME, which cannot work: gmscene.c's
     * scene_end_gate() only consults the hold on the frame the scene ASKS to
     * end, and that frame is a property of how long the machine took to
     * load -- exactly what differs between a session and a replay of it, so
     * the replay asks on a frame the recording has nothing for and leaves.
     * Storing the scene instead removes the timing from the question: the
     * replay ends a scene when the RECORDING's scene changed, whenever its
     * own code happens to ask. */
    int32_t scene;
} FrameRecord;

static FrameRecord s_rep_next; /* one record of lookahead: see the scene hold */
static bool s_rep_next_ok;

void record_open(void) {
    const char* rec = getenv("MELEE_NET_RECORD");
    const char* rep = getenv("MELEE_NET_REPLAY");
    if (rec != NULL && rec[0] != '\0') {
        s_rec = fopen(rec, "wb");
        pc_log_line("net: %s %s", s_rec ? "recording to" : "cannot open", rec);
    }
    if (rep != NULL && rep[0] != '\0' && !net.active) {
        s_rep = fopen(rep, "rb");
        char magic[4];
        uint32_t seed;
        bool ok = s_rep && fread(magic, 4, 1, s_rep) == 1;
        if (ok && memcmp(magic, "MRC4", 4) == 0) {
            /* The same layout with the older checksum, which folded no
             * velocity: replayed now, every fight frame reads as a
             * divergence that is not one. */
            pc_log_line("net: %s is an MRC4 recording; its checksums predate protocol 9, "
                        "record it again",
                rep);
            ok = false;
        }
        ok = ok && memcmp(magic, "MRC5", 4) == 0 && fread(&seed, 4, 1, s_rep) == 1;
        if (s_rep && !ok) {
            fclose(s_rep);
            s_rep = NULL;
        }
        if (s_rep) {
            *HSD_RandSeedPtr = seed;
            s_rep_next_ok = fread(&s_rep_next, sizeof s_rep_next, 1, s_rep) == 1;
        }
        pc_log_line("net: %s %s", s_rep ? "replaying" : "cannot open", rep);
    }
    const char* sl = getenv("MELEE_NET_STATE_LOG");
    if (sl != NULL && sl[0] != '\0') {
        s_state_log = fopen(sl, "w");
        if (s_state_log != NULL) {
            static char buf[1 << 16];
            setvbuf(s_state_log, buf, _IOFBF, sizeof buf);
        }
        pc_log_line("net: %s state log %s", s_state_log ? "writing" : "cannot open", sl);
    }
}

bool record_active(void) {
    return s_rec != NULL || s_rep != NULL;
}

static FrameRecord s_rep_cur;

/* Load the next record's pads for this frame; false at end of file. The file
 * is read one record ahead so the scene hold can ask what the recording did
 * on the frame AFTER this one, which is how it knows a scene ended. */
static bool replay_load(PADStatus* head) {
    if (!s_rep_next_ok) {
        return false;
    }
    s_rep_cur = s_rep_next;
    s_rep_next_ok = fread(&s_rep_next, sizeof s_rep_next, 1, s_rep) == 1;
    memcpy(head, s_rep_cur.pads, sizeof s_rep_cur.pads);
    *HSD_RandSeedPtr = s_rep_cur.seed;
    return true;
}

static void replay_compare(uint32_t ck) {
    if (!s_rep_reported && s_rep_cur.ck != ck) {
        s_rep_reported = true;
        pc_log_line("net: REPLAY DIVERGED at frame %d (recorded %08x now %08x)", net.frame,
            s_rep_cur.ck, ck);
    }
}

/* Before the fresh tick of net.frame: the replay's pads into the head. */
void replay_feed(PADStatus* head) {
    if (s_rep != NULL && !replay_load(head)) {
        pc_log_line("net: replay finished at frame %d%s", net.frame,
            s_rep_reported ? "" : ", no divergence");
        fclose(s_rep);
        s_rep = NULL;
    }
}

/* Still at tick start, after the checksum and agreed-seed override but before
 * simulation: record the same seed the checksum used, not a prior tick's or
 * a pre-handshake seed. The intervening state/desync logging draws no RNG.
 *
 * During netplay a frame is simulated more than once: the first pass runs on
 * a PREDICTED remote pad and a rollback re-runs it on the real one. Writing
 * the first pass is what the file used to hold, and replaying it then
 * diverges at the first rollback -- which reads exactly like the
 * cross-platform determinism failure the recording exists to localise. So a
 * frame is staged here (a later re-run overwrites its slot) and only written
 * out once it can no longer be rolled back, which is what record_confirm()
 * below does. Offline there is no rollback and staging is a straight
 * write-through. */
static FrameRecord s_rec_ring[RING];
static int32_t s_rec_staged = -1;  /* newest frame staged */
static int32_t s_rec_written = -1; /* newest frame on disk */
static bool s_rec_online;          /* a session has run: offline frames are not recorded */

/* An online session starts at frame 0: a new header, and from then on the
 * recording holds only session frames (the lobby between two sessions runs
 * on each machine's own clock, so it never compares). */
void record_session_begin(void) {
    s_rec_staged = -1;
    s_rec_written = -1;
    s_rec_online = true;
}

static void record_write(const FrameRecord* r, int32_t f) {
    if (f == 0) {
        fwrite("MRC5", 4, 1, s_rec);
        fwrite(&r->seed, 4, 1, s_rec);
    }
    fwrite(r, sizeof *r, 1, s_rec);
    fflush(s_rec); /* runs usually end by SIGTERM; keep every frame */
}

void record_frame(const PADStatus* head, uint32_t ck, int32_t f) {
    if (s_rec != NULL) {
        FrameRecord* r = &s_rec_ring[f & (RING - 1)];
        memcpy(r->pads, head, sizeof r->pads);
        r->ck = ck;
        r->seed = *HSD_RandSeedPtr;
        r->scene = scene_kind();
        if (f > s_rec_staged) {
            s_rec_staged = f;
        }
        if (!net.active && !s_rec_online) {
            record_write(r, f);
            s_rec_written = f;
        }
    }
    if (s_rep != NULL) {
        replay_compare(ck);
    }
}

/* Frames up to and including `upto` are settled: write them out in order.
 * A frame that aged out of the ring before it was confirmed cannot be
 * recovered, so the file stops there rather than silently skipping it. */
void record_confirm(int32_t upto) {
    s_confirmed = net.active ? upto : INT32_MIN;
    if (s_rec == NULL || !net.active) {
        return;
    }
    if (upto > s_rec_staged) {
        upto = s_rec_staged;
    }
    for (int32_t f = s_rec_written + 1; f <= upto; f++) {
        if (f <= net.frame - RING) {
            return; /* its slot has been reused: the record ends here */
        }
        record_write(&s_rec_ring[f & (RING - 1)], f);
        s_rec_written = f;
    }
}

/* Offline replay of a netplay recording: end a scene where the RECORDING
 * ended it. The gate asks "may this scene end now?" on whatever frame this
 * run's code gets round to asking, which is not the frame the recorded
 * session asked on -- it depends on how long each machine took to load. So
 * the answer cannot come from a frame number. It comes from the record one
 * frame ahead: while the recording was still in the same scene on the next
 * frame, this scene has not ended yet.
 *
 * Logged once per hand-off. Between two menus the checksum covers only the
 * pads and the RNG, so a scene left early still hashes equal for a while and
 * the DIVERGED line lands frames later, on a different boundary than the one
 * that slipped; the log is what makes the real boundary visible. */
bool record_replay_scene_hold(int32_t frame) {
    if (s_rep == NULL || !s_rep_next_ok || s_rep_cur.scene < 0) {
        return false;
    }
    bool hold = s_rep_next.scene == s_rep_cur.scene;
    static int32_t logged = -1;
    if (!hold && frame != logged) {
        logged = frame;
        pc_log_line("net: replay ends scene %d at frame %d (recording moves to %d)",
            s_rep_cur.scene, frame, s_rep_next.scene);
    }
    return hold;
}

/* ---- frame checksum --------------------------------------------------- */

/* For the .slp recorder (slp.c), which runs inside the tick. The frame the
 * tick simulates: a rollback's re-run frame, otherwise the newest fresh one
 * (net.frame counts every tick offline too, where tick_frame is not kept). */
int32_t pc_net_sim_frame(void) {
    return net.resim && net.active ? net.tick_frame : net.frame - 1;
}

/* The newest netplay frame no rollback can change, as record_confirm last
 * heard it from fresh_tick; INT32_MIN offline. */
int32_t pc_net_confirmed_frame(void) {
    return s_confirmed;
}

/* MELEE_NET_CHECKSUM_WIDE=1: also fold every live hitbox and every item.
 * Off by default: it changes the checksum, so both sides of a comparison
 * (record and replay, or two peers) must agree on it. */
static bool checksum_wide(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("MELEE_NET_CHECKSUM_WIDE");
        on = e != NULL && e[0] == '1';
    }
    return on;
}

uint32_t frame_checksum(const PADStatus* head) {
    /* Inputs as simulated, the RNG seed entering the frame, and each
     * fighter's position, facing, percent, stocks and action state (plan
     * §4), so a physics divergence is caught on the frame it happens. The
     * two velocities went in with protocol 9: a divergence that starts in a
     * velocity reaches the position only a frame or more later, and one
     * that is cancelled (a landing, a wall) never reaches it at all. */
    uint32_t ck = fnv1a(2166136261u, head, 4 * sizeof(PADStatus));
    ck = fnv1a(ck, HSD_RandSeedPtr, sizeof(u32));
    /* Player slots keep dangling entity pointers between scenes, so only
     * look while a fight is running and the entity really is a fighter. */
    for (int slot = 0; in_fight() && slot < 4; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
        if (gobj == NULL || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) {
            continue;
        }
        const Fighter* fp = GET_FIGHTER(gobj);
        s32 stocks = Player_GetStocks(slot);
        ck = fnv1a(ck, &fp->cur_pos, sizeof fp->cur_pos);
        ck = fnv1a(ck, &fp->facing_dir, sizeof fp->facing_dir);
        ck = fnv1a(ck, &fp->dmg.x1830_percent, sizeof fp->dmg.x1830_percent);
        ck = fnv1a(ck, &fp->motion_id, sizeof fp->motion_id);
        ck = fnv1a(ck, &stocks, sizeof stocks);
        ck = fnv1a(ck, &fp->self_vel, sizeof fp->self_vel);
        ck = fnv1a(ck, &fp->x8c_kb_vel, sizeof fp->x8c_kb_vel);
        if (checksum_wide()) {
            /* Hitboxes are placed from bone matrices: a render-side matrix
             * leaking into the simulation shows here on the same frame. */
            for (int i = 0; i < 4; i++) {
                const HitCapsule* hit = &fp->x914[i];
                ck = fnv1a(ck, &hit->state, sizeof hit->state);
                if (hit->state != HitCapsule_Disabled) {
                    ck = fnv1a(ck, &hit->x4C, sizeof hit->x4C);
                }
            }
        }
    }
    if (checksum_wide() && in_fight()) {
        for (int p = 0; p <= HSD_GObjLibInitData.p_link_max; p++) {
            for (HSD_GObj* gobj = HSD_GObjPLinkHead[p]; gobj != NULL; gobj = gobj->next) {
                if (gobj->classifier == HSD_GOBJ_CLASS_ITEM && gobj->user_data != NULL) {
                    const Item* ip = gobj->user_data;
                    ck = fnv1a(ck, &ip->kind, sizeof ip->kind);
                    ck = fnv1a(ck, &ip->pos, sizeof ip->pos);
                    ck = fnv1a(ck, &ip->x40_vel, sizeof ip->x40_vel);
                }
            }
        }
    }
    return ck;
}

/* MELEE_NET_RENDER_AUDIT=1: the render must not write what the simulation
 * reads. A rollback resim and a time-sync advance run ticks with no render
 * between them, so anything a draw callback writes into sim state is written
 * on one peer's timeline and not on the other's -- the screen-KO position
 * (ftDrawCommon_80080E18) was one. This takes frame_checksum's fighter
 * fields and the seed before the render and again after, and names the
 * frame whenever they differ. Pads are left out: the render does not read
 * them and a zeroed set keeps both sides comparable. Debug only; one line
 * per 60 hits so a callback that writes every frame cannot flood the log. */
void pc_net_render_audit(bool after) {
    static int on = -1;
    static uint32_t before;
    static unsigned hits;
    if (on < 0) {
        const char* e = getenv("MELEE_NET_RENDER_AUDIT");
        on = e != NULL && e[0] == '1';
    }
    if (!on) {
        return;
    }
    static const PADStatus none[4];
    /* Per-fighter copies of what the checksum folds, so a hit can name the
     * fighter and the field the render wrote. */
    typedef struct {
        bool live;
        int kind, motion;
        Vec3 pos, self_vel, kb_vel;
        float facing, percent;
    } Snap;
    static Snap snap[4];
    static u32 seed_before;
    uint32_t ck = frame_checksum(none);
    Snap now[4] = {0};
    for (int slot = 0; in_fight() && slot < 4; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
        if (gobj == NULL || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) {
            continue;
        }
        const Fighter* fp = GET_FIGHTER(gobj);
        now[slot] = (Snap){true, fp->kind, fp->motion_id, fp->cur_pos, fp->self_vel, fp->x8c_kb_vel,
            fp->facing_dir, fp->dmg.x1830_percent};
    }
    if (!after) {
        before = ck;
        memcpy(snap, now, sizeof snap);
        seed_before = *HSD_RandSeedPtr;
    } else if (ck != before && (hits++ % 60) == 0) {
        pc_log_line("net: render audit: sim state changed during render at frame %d (%u so far)",
            pc_net_frame(), hits);
        if (*HSD_RandSeedPtr != seed_before) {
            pc_log_line(
                "net: render audit:   RNG seed %08x -> %08x", seed_before, *HSD_RandSeedPtr);
        }
        for (int i = 0; i < 4; i++) {
            const Snap* a = &snap[i];
            const Snap* b = &now[i];
            if (!a->live || !b->live) {
                continue;
            }
#define AUDIT_V3(f)                                                                                \
    if (memcmp(&a->f, &b->f, sizeof a->f) != 0)                                                    \
    pc_log_line("net: render audit:   slot %d kind %d motion %d " #f " (%g,%g,%g) -> (%g,%g,%g)",  \
        i, b->kind, b->motion, a->f.x, a->f.y, a->f.z, b->f.x, b->f.y, b->f.z)
            AUDIT_V3(pos);
            AUDIT_V3(self_vel);
            AUDIT_V3(kb_vel);
#undef AUDIT_V3
            if (a->facing != b->facing || a->percent != b->percent || a->motion != b->motion) {
                pc_log_line("net: render audit:   slot %d kind %d facing %g->%g percent %g->%g "
                            "motion %d->%d",
                    i, b->kind, a->facing, b->facing, a->percent, b->percent, a->motion, b->motion);
            }
        }
    }
}

/* What went into the checksum, one line, so two peers' logs can be diffed
 * by eye when a DESYNC is reported. */
#define STATE_RING 64
static char s_state_ring[STATE_RING][320];

/* Raw bits of a float: the human line below rounds to three decimals, which
 * hides exactly the 1-ULP differences a codegen or libm divergence starts
 * as, and every one of those fields is folded into the checksum verbatim. */
static uint32_t f32bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* Everything frame_checksum() folds, exactly, one line per frame. This is
 * what two platforms' logs are diffed on to name the first field that
 * differs (MELEE_NET_STATE_LOG). */
static void log_state_bits(int32_t frame) {
    char buf[1024]; /* four fighters of at most 152 characters each */
    int n = snprintf(buf, sizeof buf, "f%d seed=%08x", frame, *HSD_RandSeedPtr);
    for (int slot = 0; in_fight() && slot < 4 && n < (int)sizeof buf - 160; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
        if (gobj == NULL || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) {
            continue;
        }
        const Fighter* fp = GET_FIGHTER(gobj);
        n += snprintf(buf + n, sizeof buf - (size_t)n,
            " p%d pos=%08x/%08x/%08x dir=%08x pct=%08x mid=%d st=%d vel=%08x/%08x/%08x "
            "kb=%08x/%08x/%08x",
            slot, f32bits(fp->cur_pos.x), f32bits(fp->cur_pos.y), f32bits(fp->cur_pos.z),
            f32bits(fp->facing_dir), f32bits(fp->dmg.x1830_percent), fp->motion_id,
            Player_GetStocks(slot), f32bits(fp->self_vel.x), f32bits(fp->self_vel.y),
            f32bits(fp->self_vel.z), f32bits(fp->x8c_kb_vel.x), f32bits(fp->x8c_kb_vel.y),
            f32bits(fp->x8c_kb_vel.z));
    }
    fprintf(s_state_log, "net: bits %s\n", buf);
    /* The wide checksum's items, one line each. */
    if (checksum_wide() && in_fight()) {
        for (int p = 0; p <= HSD_GObjLibInitData.p_link_max; p++) {
            for (HSD_GObj* gobj = HSD_GObjPLinkHead[p]; gobj != NULL; gobj = gobj->next) {
                if (gobj->classifier == HSD_GOBJ_CLASS_ITEM && gobj->user_data != NULL) {
                    const Item* ip = gobj->user_data;
                    fprintf(s_state_log,
                        "net: bits f%d item kind=%d pos=%08x/%08x/%08x vel=%08x/%08x/%08x\n",
                        frame, (int)ip->kind, f32bits(ip->pos.x), f32bits(ip->pos.y),
                        f32bits(ip->pos.z), f32bits(ip->x40_vel.x), f32bits(ip->x40_vel.y),
                        f32bits(ip->x40_vel.z));
                }
            }
        }
    }
}

/* Remember what went into this frame's checksum (overwritten when the
 * frame is re-simulated, so the last write is the confirmed timeline), and
 * log it too under MELEE_NET_STATE_LOG. Called for every frame, not only
 * the netplay ones: a solo replay is how a platform divergence is chased. */
void record_state(const PADStatus* head, int32_t frame) {
    if (!net.active && !s_state_log) {
        return;
    }
    char* buf = s_state_ring[frame & (STATE_RING - 1)];
    int n = snprintf(buf, sizeof s_state_ring[0], "f%d seed=%08x pads=%04x/%d,%d %04x/%d,%d", frame,
        *HSD_RandSeedPtr, head[0].button, head[0].stickX, head[0].stickY, head[1].button,
        head[1].stickX, head[1].stickY);
    for (int slot = 0; in_fight() && slot < 4 && n < (int)sizeof s_state_ring[0] - 80; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
        if (gobj == NULL || gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) {
            continue;
        }
        const Fighter* fp = GET_FIGHTER(gobj);
        n += snprintf(buf + n, sizeof s_state_ring[0] - (size_t)n,
            " p%d=(%.3f,%.3f) v(%.3f,%.3f) kb(%.3f,%.3f) f%.0f %.1f%% m%d s%d", slot,
            (double)fp->cur_pos.x, (double)fp->cur_pos.y, (double)fp->self_vel.x,
            (double)fp->self_vel.y, (double)fp->x8c_kb_vel.x, (double)fp->x8c_kb_vel.y,
            (double)fp->facing_dir, (double)fp->dmg.x1830_percent, fp->motion_id,
            Player_GetStocks(slot));
    }
    if (s_state_log != NULL) {
        fprintf(s_state_log, "net: state %s\n", buf);
        log_state_bits(frame);
        /* A harness SIGKILLs the run a moment after the divergence it was
         * waiting for, so the tail must already be on disc; every 8 frames
         * is one write syscall per 4 KB instead of four per frame. */
        if ((frame & 7) == 0) {
            fflush(s_state_log);
        }
    }
}

/* Dump the recorded states around `frame` (both peers do this on DESYNC, so
 * the two logs can be diffed line by line). */
void dump_states_around(int32_t frame) {
    /* Most of the ring: the checksum covers position but not velocity, so
     * the frame a desync is reported on is not the frame the two timelines
     * parted -- that one is found by diffing the two peers' dumps back
     * until the lines agree. */
    for (int32_t f = frame - 40; f <= frame + 1; f++) {
        const char* s = s_state_ring[f & (STATE_RING - 1)];
        if (f >= 0 && strncmp(s, "f", 1) == 0) {
            pc_log_line("net: state %s", s);
        }
    }
}

/* One frame's recorded state line, "" when that frame is not in the ring:
 * the audit (net.c) keeps a copy from before a re-run and compares it with
 * the line the re-run wrote, which names the field that moved instead of
 * leaving a raw memory block to resolve by hand. */
const char* state_line(int32_t frame) {
    const char* s = s_state_ring[frame & (STATE_RING - 1)];
    return s[0] == 'f' ? s : "";
}
