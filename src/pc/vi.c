/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * VI layer. aurora provides the window/framebuffer (VIInit/VIConfigure/VIFlush);
 * retrace timing and the XFB flip are emulated here on top of aurora's frame
 * loop. Melee waits in HSD_VIWaitXFBFlush -> VIWaitForRetrace once per frame,
 * so VIWaitForRetrace is the frame boundary.
 */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern void browser_yield(void);
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/pc.h"
#include "pc/interp.h"
#include "pc/launcher.h"
#include "pc/touch.h"
#include "pc/widescreen.h"
#include "pc/net.h"
#include "pc/net_chat.h"
#include <melee/if/ifnetchat.h>
#include <dolphin/pad.h>
#include "pc/net_lan.h"
#include "pc/net_match.h"
#include "pc/net_rank_session.h"

bool pc_exit_requested;

static u32 s_retrace_count;
static VIRetraceCallback s_pre_cb;
static VIRetraceCallback s_post_cb;
static void* s_next_fb;
static void* s_current_fb;
static BOOL s_black;
static bool s_in_frame;
/* When the next 60 Hz boundary is due (SDL ns); the in-between frame of
 * high-refresh presentation is paced half a period before it. */
static u64 s_next_sim_ns;
/* In-between frames presented since the last FPS log line. */
static u32 s_interp_presents;
unsigned pc_sim_ticks;
/* Frames presented this tick under MELEE_INTERP_TEST_STEPS. */
static int s_test_frame;
/* When the frame being drawn was begun (aurora_begin_frame returned). With
 * VSync that follows the display's refresh, which is what the blend factor
 * of a high-refresh frame is computed from. */
static u64 s_frame_begun_ns;
/* Intervals between frame begins since the last FPS line (MELEE_FPS):
 * locked to the display they sit at one refresh with little spread. */
static double s_begin_sum, s_begin_sq;
static u32 s_begin_n;

static u64 refresh_period_ns(void);
/* The game window (pc_vi_set_window) and the display it was last seen on. */
static SDL_Window* s_window;
static SDL_DisplayID s_refresh_display;

/* The same, for the cadence watchdog in pc_frame_boundary. */
static double s_wd_sum, s_wd_sq;
static u32 s_wd_n;

static void note_frame_begun(void) {
    const u64 now = SDL_GetTicksNS();
    if (s_frame_begun_ns != 0) {
        const double ms = (now - s_frame_begun_ns) / 1e6;
        s_begin_sum += ms;
        s_begin_sq += ms * ms;
        s_begin_n++;
        s_wd_sum += ms;
        s_wd_sq += ms * ms;
        s_wd_n++;
    }
    s_frame_begun_ns = now;
}

/* Set when auto-enabled high-refresh presentation slowed the simulation. */
static bool s_interp_fell_behind;

void pc_os_run_alarms(void);
void aurora_heap_check(void);

uint32_t pc_gfx_prewarm(uint32_t max_wait_ms) {
    return aurora_wait_pipelines(max_wait_ms);
}

uint64_t pc_monotonic_ns(void) {
    return SDL_GetTicksNS();
}

#ifdef __EMSCRIPTEN__
/* A precise native sleep would block the browser event loop; the frame
 * boundary is also the only place the page gets control back. */
#define PC_PACE_ALWAYS 1
static bool s_pace_waited;
static void pc_pace_wait(u64 ns) {
    s_pace_waited = true;
    if (ns >= 1000000ull)
        emscripten_sleep((unsigned)(ns / 1000000ull));
    else
        browser_yield();
}
#else
#define PC_PACE_ALWAYS 0
#define pc_pace_wait SDL_DelayPrecise
#endif

/* High-refresh frame grid: with in-between frames on, every frame begins on
 * a slot one display refresh after the last, so presents are evenly spaced
 * and the swapchain queue stays shallow. Left to the timing of
 * aurora_begin_frame alone, a hitch can leave the queue backed up, after
 * which frames arrive in pairs and the display repeats every other one.
 * s_grid_ns is the slot of the frame being drawn; 0 = no grid. */
static u64 s_grid_ns;

/* Wait for the next grid slot before beginning a frame. */
static void pace_to_grid(void) {
    if (s_grid_ns == 0)
        return;
    const u64 target = s_grid_ns + refresh_period_ns();
    const u64 now = SDL_GetTicksNS();
    if (target > now + 200000ull)
        pc_pace_wait(target - now);
}

/* A frame has begun: it takes the next slot, or re-anchors the grid when it
 * could not make that slot (a stall, a blocked queue) or there is none. */
static void advance_grid(bool on) {
    if (!on) {
        s_grid_ns = 0;
        return;
    }
    const u64 r = refresh_period_ns();
    const u64 now = s_frame_begun_ns;
    if (s_grid_ns == 0 || now > s_grid_ns + r + r / 2)
        s_grid_ns = now;
    else
        s_grid_ns += r;
}

static void handle_events(const AuroraEvent* event) {
    while (event != NULL && event->type != AURORA_NONE) {
        if (event->type == AURORA_EXIT) {
            pc_exit_requested = true;
        } else if (event->type == AURORA_SDL_EVENT) {
            if (event->sdl.type == SDL_EVENT_KEY_DOWN &&
                event->sdl.key.scancode == SDL_SCANCODE_F1 && !event->sdl.key.repeat)
                pc_menu_toggle();
            pc_menu_event(&event->sdl);
            pc_keyboard_event(&event->sdl);
            pc_touch_event(&event->sdl);
        }
        ++event;
    }
}

void pc_frame_boundary(void) {
    static int timing_debug = -1;
    static int fps_log = -1;
    static u64 fps_t0;
    static u32 fps_n;
    /* An average hides stutter: one 60ms hitch a second barely moves it.
     * Track the outliers instead, plus how far the pacing sleep overshoots
     * its request -- on Windows a coarse timer resolution turns a 5ms wait
     * into ~16ms, which is stutter the frame average never shows. */
    static u64 frame_prev_ns;
    static u64 frame_worst_ns;
    static u32 frame_late_20;
    static u32 frame_late_33;
    static u64 sleep_worst_over_ns;

    if (timing_debug < 0)
        timing_debug = getenv("MELEE_NET_DEBUG") != NULL;
    s_test_frame = 0;
    if (s_in_frame) {
        u64 started = timing_debug ? SDL_GetTicksNS() : 0;
        aurora_end_frame();
        if (timing_debug) {
            u64 elapsed = SDL_GetTicksNS() - started;
            if (elapsed > 20000000ull)
                pc_log_line("net timing: aurora_end_frame %.1f ms at retrace %u frame %d",
                    elapsed / 1e6, s_retrace_count, pc_net_frame());
        }
        s_in_frame = false;
    }
    /* Session messages are host-side work, never rollback simulation. The
     * matcher owns them until both peers cross its READY barrier. */
    if (pc_net_match_state(NULL) == PC_MATCH_READY)
        pc_rank_session_poll();
#ifdef __EMSCRIPTEN__
    extern void pc_audio_pump(void);
    pc_audio_pump();
#endif
    aurora_heap_check();    /* no-op unless MELEE_HEAP_CHECK is set */
    pc_widescreen_update(); /* Auto mode follows window resizes. */
    /* MELEE_LAN_TEST=1|host: the LAN lobby without the menu; "host" starts
     * a match with the first peer found. MELEE_LAN_DIRECT=ip:port: the same
     * with a known peer, no discovery (src/pc/net_lan.c). */
    static int lan_test = -1;
    static u32 lan_frames;
    if (lan_test < 0) {
        const char* t = getenv("MELEE_LAN_TEST");
        lan_test = getenv("MELEE_LAN_DIRECT") != NULL ? 3 :
                   t == NULL                          ? 0 :
                   strcmp(t, "host") == 0             ? 2 :
                                                        1;
    }
    if (lan_test) {
        pc_lan_poll();
        /* Both fixtures start only once the game has its rules loaded (the
         * title screen), not at frame 0: RULES would carry zeros. */
        lan_frames++;
        if (lan_test == 2 && lan_frames >= 300 && pc_lan_state(NULL) == 0) {
            pc_lan_start_match();
        }
        if (lan_test == 3 && lan_frames == 300) {
            const char* d = getenv("MELEE_LAN_DIRECT");
            char host[64];
            const char* colon = strrchr(d, ':');
            if (colon != NULL && (size_t)(colon - d) < sizeof host) {
                memcpy(host, d, (size_t)(colon - d));
                host[colon - d] = '\0';
                pc_lan_connect_direct(host, (uint16_t)atoi(colon + 1));
            } else {
                pc_log_line("lan: MELEE_LAN_DIRECT must be ip:port");
            }
        }
    }
    if (fps_log < 0) {
        fps_log = getenv("MELEE_FPS") != NULL;
        fps_t0 = SDL_GetTicks();
    }
    if (fps_log) {
        u64 now = SDL_GetTicks();
        u64 now_ns = SDL_GetTicksNS();
        fps_n++;
        if (frame_prev_ns != 0) {
            u64 delta = now_ns - frame_prev_ns;
            if (delta > frame_worst_ns) {
                frame_worst_ns = delta;
            }
            if (delta > 20000000ull) {
                frame_late_20++;
            }
            if (delta > 33000000ull) {
                frame_late_33++;
            }
            /* Put the stall in the main log too, where it sits next to
             * whatever aurora reported loading at that moment. An average
             * cannot tell a shader compile from a disc read; a timestamped
             * marker beside the surrounding records can. */
            if (delta > 50000000ull) {
                pc_log_line("STALL %.1fms at frame %u", delta / 1e6, s_retrace_count);
            }
        }
        frame_prev_ns = now_ns;
        if (now - fps_t0 >= 1000) {
            /* Through pc_log_line, not stderr: on Android stderr goes
             * nowhere, so MELEE_FPS printed nothing there. pc_log_line also
             * reaches logcat and MELEE_LOG_FILE. */
            static unsigned ticks_seen;
            const unsigned ticks = pc_sim_ticks - ticks_seen;
            ticks_seen = pc_sim_ticks;
            pc_log_line("fps %.1f ticks %.1f present %.1f worst %.1fms late>20ms %u late>33ms %u "
                        "sleep_overshoot %.1fms",
                fps_n * 1000.0 / (double)(now - fps_t0), ticks * 1000.0 / (double)(now - fps_t0),
                (fps_n + s_interp_presents) * 1000.0 / (double)(now - fps_t0), frame_worst_ns / 1e6,
                frame_late_20, frame_late_33, sleep_worst_over_ns / 1e6);
            if (s_interp_presents != 0 && s_begin_n > 1) {
                const double mean = s_begin_sum / s_begin_n;
                const double var = s_begin_sq / s_begin_n - mean * mean;
                const SDL_WindowFlags wf = s_window != NULL ? SDL_GetWindowFlags(s_window) : 0;
                /* A covered or unfocused window may be throttled by the OS,
                 * which shows up here as a wide spread. */
                pc_log_line("interp frame begin interval %.2f ms (sd %.2f) over %u frames%s%s", mean,
                            var > 0 ? sqrt(var) : 0.0, s_begin_n,
                            (wf & SDL_WINDOW_OCCLUDED) ? " [occluded]" : "",
                            (wf & SDL_WINDOW_INPUT_FOCUS) ? "" : " [no focus]");
            }
            s_begin_sum = s_begin_sq = 0;
            s_begin_n = 0;
            s_interp_presents = 0;
            fps_t0 = now;
            fps_n = 0;
            frame_worst_ns = 0;
            frame_late_20 = 0;
            frame_late_33 = 0;
            sleep_worst_over_ns = 0;
        }
    }

    u64 update_started = timing_debug ? SDL_GetTicksNS() : 0;
    const AuroraEvent* event = aurora_update();
    if (timing_debug) {
        u64 elapsed = SDL_GetTicksNS() - update_started;
        if (elapsed > 50000000ull)
            pc_log_line("net timing: aurora_update %.1f ms at retrace %u frame %d", elapsed / 1e6,
                s_retrace_count, pc_net_frame());
    }
    handle_events(event);
    pc_menu_update();
    /* Nothing draws while the overlay pauses the game, so hold the last
     * frame instead of clearing the EFB to black underneath the menu. */
    aurora_preserve_frame_buffer(pc_menu_is_open());
    pc_keyboard_apply();
    /* MELEE_EXIT_AFTER_FRAMES=<n>: bound a scripted run without needing
     * synthetic input, which is unreliable under Xwayland. Setting
     * pc_exit_requested instead of exiting here on purpose: the window-close
     * path is the one that runs atexit(pc_shutdown_once), and skipping it is
     * what makes Dawn's static destructors race the live device. */
    static int exit_after = -1;
    if (exit_after < 0) {
        const char* n = getenv("MELEE_EXIT_AFTER_FRAMES");
        exit_after = n != NULL ? atoi(n) : 0;
    }
    /* MELEE_OPEN_MENU_AT=<n>: open the settings overlay at frame n, so a
     * scripted run can capture it (synthetic input cannot press F1). */
    static int open_menu_at = -1;
    if (open_menu_at < 0) {
        const char* n = getenv("MELEE_OPEN_MENU_AT");
        open_menu_at = n != NULL ? atoi(n) : 0;
    }
    if (open_menu_at > 0 && s_retrace_count == (u32)open_menu_at && !pc_menu_is_open())
        pc_menu_toggle();
    if (exit_after > 0 && s_retrace_count >= (u32)exit_after && !pc_exit_requested) {
        pc_log_line("MELEE_EXIT_AFTER_FRAMES: reached frame %u, exiting", s_retrace_count);
        pc_exit_requested = true;
    }
    if (pc_exit_requested) {
        pc_net_match_stop();
        pc_net_disconnect();
        pc_lan_stop();
        exit(0);
    }

#ifdef __EMSCRIPTEN__
    s_pace_waited = false;
#endif
    /* Enforce deterministic 60 Hz simulation pacing regardless of display refresh rate
     * (e.g. 120 Hz, 144 Hz, 240 Hz high-refresh monitors). When VSync is enabled on high-refresh
     * displays, aurora_begin_frame() unblocks at monitor refresh rate. Without this check,
     * the simulation would run at 2x-4x speed. Pacing strictly to 60.000 Hz ensures physics,
     * hitboxes, and timers remain bit-identical. */
    const u64 sim_period = pc_sim_period_ns();
    u64 now = SDL_GetTicksNS();
    /* With in-between frames, pace to the tick itself: the pad alarm's next
     * firing, rather than a 60 Hz schedule of the pacer's own that drifts
     * against it (two clocks make frames bunch up within a tick). */
    if (pc_interp_enabled()) {
        const u64 alarm = pc_os_next_tick_ns();
        if (alarm != 0)
            s_next_sim_ns = alarm;
    }
    /* How far behind its schedule this boundary is, and how much of that to
     * run off by skipping the sleep below. Offline a large hitch is dropped:
     * there is nothing to stay in step with. In netplay dropping it left the
     * peer that froze behind the other one by the whole freeze, which time
     * sync then took seconds to close (pc_net_catch_up_ns). */
    u64 late = s_next_sim_ns != 0 && now > s_next_sim_ns ? now - s_next_sim_ns : 0;
    late = pc_net_active() ? pc_net_catch_up_ns(late) : late > sim_period * 2 ? 0 : late;
    if (s_next_sim_ns == 0 || now > s_next_sim_ns) {
        s_next_sim_ns = now - late;
    } else if (now < s_next_sim_ns) {
        const u64 want = s_next_sim_ns - now;
        /* On standard 60 Hz VSync, aurora_begin_frame already waited for VBlank. On high-refresh
         * (120/144/240 Hz) or VSync-off, this throttles simulation to exact 60 Hz. */
        if (PC_PACE_ALWAYS || !aurora_vsync_enabled() || want > 2000000ull) {
            u64 started = (fps_log || timing_debug) ? SDL_GetTicksNS() : 0;
            pc_pace_wait(want);
            if (fps_log || timing_debug) {
                u64 slept = SDL_GetTicksNS() - started;
                if (fps_log && slept > want && slept - want > sleep_worst_over_ns) {
                    sleep_worst_over_ns = slept - want;
                }
                if (timing_debug && slept > 20000000ull)
                    pc_log_line(
                        "net timing: pacing sleep %.1f ms (asked %.1f) at retrace %u frame %d",
                        slept / 1e6, want / 1e6, s_retrace_count, pc_net_frame());
            }
        }
    }
    s_next_sim_ns += sim_period;
    /* Watchdog for auto high-refresh presentation: a display that cannot
     * take two presents per tick shows up as missed boundaries. Below ~57
     * ticks a second over two seconds, go back to one present per tick. */
    {
        static u64 window_ns, prev_ns;
        static u32 window_ticks, window_interp, bad_windows;
        static u32 interp_seen;
        static bool window_stalled;
        if (window_ns == 0)
            window_ns = now;
        /* A loading stall (scene change, shader compile) slows any window
         * regardless of presentation; such windows say nothing. */
        if (prev_ns != 0 && now - prev_ns > 100000000ull)
            window_stalled = true;
        prev_ns = now;
        window_ticks++;
        window_interp += s_interp_presents != interp_seen;
        interp_seen = s_interp_presents;
        if (now - window_ns >= 2000000000ull) {
            const double tick_rate = window_ticks * 1e9 / (double)(now - window_ns);
            /* Frames should begin one display refresh apart. A window on a
             * slower display than reported, or a compositor taking frames at
             * its own pace, shows up as a longer or scattered interval. */
            const double refresh_ms = refresh_period_ns() / 1e6;
            const double mean = s_wd_n ? s_wd_sum / s_wd_n : 0.0;
            const double var = s_wd_n ? s_wd_sq / s_wd_n - mean * mean : 0.0;
            const double sd = var > 0 ? sqrt(var) : 0.0;
            const bool cadence_off = s_wd_n > 30 && (mean > refresh_ms * 1.12 || sd > refresh_ms * 0.35);
            if (window_stalled || window_interp <= window_ticks / 2)
                ; /* no verdict */
            else if (tick_rate < 57.0 || cadence_off)
                bad_windows++;
            else
                bad_windows = 0;
            if (bad_windows >= 3 && !s_interp_fell_behind && getenv("MELEE_FRAME_INTERP") == NULL &&
                pc_get_interp_mode() != 1) {
                s_interp_fell_behind = true;
                bad_windows = 0;
                pc_log_line("interp: %.1f ticks/s, frames %.2f ms apart (sd %.2f) for a %.2f ms "
                            "refresh; presenting once per tick until the window changes display",
                            tick_rate, mean, sd, refresh_ms);
            }
            window_ns = now;
            window_ticks = window_interp = 0;
            window_stalled = false;
            s_wd_sum = s_wd_sq = 0;
            s_wd_n = 0;
        }
    }
#ifdef __EMSCRIPTEN__
    if (!s_pace_waited)
        browser_yield(); /* every frame returns to the event loop at least once */
#endif

    /* aurora_begin_frame returns false while minimized/paused; keep pumping.
     * Sleep a frame between attempts: without it a minimized window spins a
     * core at 100% polling SDL. */
    /* With in-between frames the tick's first frame also waits for its grid
     * slot, so the tick runs on the first slot at or after it is due. */
    const bool grid_on = pc_interp_enabled();
    if (grid_on)
        pace_to_grid();
    for (;;) {
        u64 started = timing_debug ? SDL_GetTicksNS() : 0;
        bool begun = aurora_begin_frame();
        if (timing_debug) {
            u64 elapsed = SDL_GetTicksNS() - started;
            if (elapsed > 20000000ull)
                pc_log_line("net timing: aurora_begin_frame %.1f ms at retrace %u frame %d",
                    elapsed / 1e6, s_retrace_count, pc_net_frame());
        }
        if (begun) {
            note_frame_begun();
            advance_grid(grid_on);
            break;
        }
        update_started = timing_debug ? SDL_GetTicksNS() : 0;
        event = aurora_update();
        if (timing_debug) {
            u64 elapsed = SDL_GetTicksNS() - update_started;
            if (elapsed > 50000000ull)
                pc_log_line("net timing: aurora_update %.1f ms at retrace %u frame %d",
                    elapsed / 1e6, s_retrace_count, pc_net_frame());
        }
        while (event != NULL && event->type != AURORA_NONE) {
            if (event->type == AURORA_EXIT) {
                pc_net_match_stop();
                pc_net_disconnect();
                pc_lan_stop();
                exit(0);
            }
            ++event;
        }
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        SDL_Delay(16);
#endif
    }
    s_in_frame = true;

    /* Presentation-only chat reads the physical local controller, never a
     * rewound/synchronized pad. It cannot change the game simulation. */
    bool chat_eligible = pc_net_chat_available();
    PADStatus chat_pads[4] = {0};
    if (chat_eligible)
        PADRead(chat_pads);
    pc_net_chat_poll(chat_pads[0].button, chat_eligible, SDL_GetTicks());
    ifNetChat_Update(chat_eligible);

    s_retrace_count++;
#ifdef __EMSCRIPTEN__
    // clang-format off
    EM_ASM({if(Module.onFrame)Module.onFrame($0);},s_retrace_count);
    // clang-format on
#endif
    /* Age of the 1000 Hz sample the sim is about to consume, before the pad
     * alarms (fn_800195FC -> PADRead) fire from pc_os_run_alarms. */
    pc_input_latency_record();
    u64 alarms_started = timing_debug ? SDL_GetTicksNS() : 0;
    pc_os_run_alarms();
    if (timing_debug) {
        u64 elapsed = SDL_GetTicksNS() - alarms_started;
        if (elapsed > 50000000ull)
            pc_log_line("net timing: pc_os_run_alarms %.1f ms at retrace %u frame %d",
                elapsed / 1e6, s_retrace_count, pc_net_frame());
    }
    /* Time sync spreads its correction here: a per-frame lengthening of the
     * next wait, plus a whole frame when a gap is too big to nudge away. */
    s_next_sim_ns += pc_net_pace_adjust_ns();
    if (s_pre_cb) {
        u64 started = timing_debug ? SDL_GetTicksNS() : 0;
        s_pre_cb(s_retrace_count);
        if (timing_debug) {
            u64 elapsed = SDL_GetTicksNS() - started;
            if (elapsed > 50000000ull)
                pc_log_line("net timing: pre-retrace callback %.1f ms at retrace %u frame %d",
                    elapsed / 1e6, s_retrace_count, pc_net_frame());
        }
    }
    s_current_fb = s_next_fb;
    if (s_post_cb) {
        u64 started = timing_debug ? SDL_GetTicksNS() : 0;
        s_post_cb(s_retrace_count);
        if (timing_debug) {
            u64 elapsed = SDL_GetTicksNS() - started;
            if (elapsed > 50000000ull)
                pc_log_line("net timing: post-retrace callback %.1f ms at retrace %u frame %d",
                    elapsed / 1e6, s_retrace_count, pc_net_frame());
        }
    }
}

/* Refresh rate of the display the game window is on, re-read about once a
 * second so dragging the window to another monitor is picked up. */

void pc_vi_set_window(SDL_Window* window) {
    s_window = window;
}

static float display_refresh_hz(void) {
    static float hz;
    static u64 checked_ns;
    const u64 now = SDL_GetTicksNS();
    if (checked_ns != 0 && now - checked_ns < 1000000000ull)
        return hz;
    checked_ns = now;
    SDL_DisplayID display = s_window != NULL ? SDL_GetDisplayForWindow(s_window) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : NULL;
    const float was = hz;
    hz = mode != NULL ? mode->refresh_rate : 0.0f;
    if (display != s_refresh_display) {
        /* Another display: whatever the cadence check concluded about the
         * last one does not apply here. */
        s_refresh_display = display;
        s_interp_fell_behind = false;
    }
    if (hz != was)
        pc_log_line("display refresh %.2f Hz (display %u)", hz, (unsigned)display);
    return hz;
}

/* One display refresh, or half a tick when the rate is unknown. */
static u64 refresh_period_ns(void) {
    const float hz = display_refresh_hz();
    return hz >= 30.0f ? (u64)(1e9 / hz) : pc_sim_period_ns() / 2;
}

/* Blend factor from the frame's grid slot. Frames begin one per display
 * refresh on the grid and each reaches the screen a fixed time later, so the
 * slot is its display time minus a constant: blending to it gives evenly
 * spaced motion at any refresh rate. The tick that just ran is
 * the state due at the next boundary (s_next_sim_ns) and the one before it a
 * sim period earlier, so every frame of the tick falls in [0, 1] and none is
 * clamped (a clamped frame is a visible stall). The cost is that constant:
 * about one refresh of extra latency. */
/* MELEE_INTERP_TEST_STEPS=<n>: exactly n frames per tick at t = 1/n .. 1,
 * whatever the clock says. Frame captures (AURORA_SCREENSHOT_FRAMES) stall
 * on the GPU, which wrecks timing-based blending; this makes captured
 * sequences show the blend itself. Test only. */
static int test_steps(void) {
    static int n = -1;
    if (n < 0) {
        const char* e = getenv("MELEE_INTERP_TEST_STEPS");
        n = e != NULL ? atoi(e) : 0;
    }
    return n;
}

/* When the next tick happens: the pad alarm that drives it, falling back to
 * the pacer's own schedule. */
static u64 next_tick_ns(void) {
    const u64 alarm = pc_os_next_tick_ns();
    return alarm != 0 ? alarm : s_next_sim_ns;
}

float pc_interp_frame_t(void) {
    if (test_steps() > 1)
        return (float)(s_test_frame + 1) / (float)test_steps();
    const double period = (double)pc_sim_period_ns();
    const u64 at = s_grid_ns != 0 ? s_grid_ns : s_frame_begun_ns;
    double t = 1.0 - ((double)next_tick_ns() - (double)at) / period;
    return t < 0.0 ? 0.0f : t > 1.0 ? 1.0f : (float)t;
}

bool pc_interp_room_for_frame(void) {
    if (test_steps() > 1)
        return s_test_frame + 1 < test_steps();
    /* Another frame fits if it would begin, a refresh from now, before the
     * next tick is due; otherwise this frame is the tick's last and is
     * presented by the boundary. */
    const u64 at = s_grid_ns != 0 ? s_grid_ns : s_frame_begun_ns;
    return at + refresh_period_ns() + 500000ull <= next_tick_ns();
}

/* High-refresh presentation (src/pc/interp.h): present what the scene loop
 * just drew as an in-between frame. Deliberately none of the boundary's game-visible work: no
 * retrace count, no VI callbacks, no pad alarms, no exit-after count. Events
 * are still pumped so no key press or window event is lost. */
void pc_present_interp_frame(void) {
    if (!s_in_frame)
        return;
    aurora_end_frame();
    s_in_frame = false;
    s_interp_presents++;
    s_test_frame++;
    handle_events(aurora_update());
    pc_keyboard_apply();
    pace_to_grid();

    while (!aurora_begin_frame()) {
        handle_events(aurora_update());
        if (pc_exit_requested)
            return;
#ifdef __EMSCRIPTEN__
        emscripten_sleep(8);
#else
        SDL_Delay(8);
#endif
    }
    note_frame_begun();
    advance_grid(true);
    s_in_frame = true;
}

bool pc_interp_enabled(void) {
    /* MELEE_FRAME_INTERP overrides the Smooth motion setting (interp pref:
     * 0 auto, 1 on, 2 off). */
    static int env = -2; /* -1 unset, 0 off, 1 on */
    if (env == -2) {
        const char* e = getenv("MELEE_FRAME_INTERP");
        env = e == NULL || e[0] == '\0' ? -1 : e[0] == '0' ? 0 : 1;
    }
    const int pref = pc_get_interp_mode();
    const int mode = env != -1 ? env : pref == 1 ? 1 : pref == 2 ? 0 : -1;
    if (mode == 0 || pc_net_active())
        return false;
    if (mode == 1)
        return true;
    /* Two presents per tick on a 60 Hz display would halve the game speed
     * (even with VSync off: Metal has no tearing present mode), so only draw
     * the extra frame where the display can show it, and stop for good if
     * the simulation still fell behind (pc_frame_boundary). */
    return !s_interp_fell_behind && display_refresh_hz() >= 100.0f;
}

void VIWaitForRetrace(void) {
    pc_frame_boundary();
    /* The overlay pauses the game. Melee's whole simulation hangs off this
     * call returning, so keep presenting frames and pumping input here and
     * simply do not hand one back until the menu closes. */
    while (pc_menu_is_open() && !pc_exit_requested) {
        pc_frame_boundary();
    }
}

u32 VIGetRetraceCount(void) {
    return s_retrace_count;
}

u64 pc_sim_period_ns(void) {
    return 1000000000ull / 60;
}

u32 VIGetNextField(void) {
    return s_retrace_count & 1;
}

u32 VIGetDTVStatus(void) {
    return 0;
}

void* VIGetCurrentFrameBuffer(void) {
    return s_current_fb;
}

void* VIGetNextFrameBuffer(void) {
    return s_next_fb;
}

void VISetNextFrameBuffer(void* fb) {
    s_next_fb = fb;
}

void VISetBlack(BOOL black) {
    s_black = black;
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = s_pre_cb;
    s_pre_cb = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = s_post_cb;
    s_post_cb = cb;
    return old;
}

u16 VIPadFrameBufferWidth(u16 width) {
    return (u16)((width + 15) & ~15);
}
