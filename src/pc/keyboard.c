/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Keyboard -> controller port 1.
 *
 * Gamepads are handled by aurora's SDL_Gamepad mapping; this covers the
 * no-controller case. Mapping:
 *   Arrows / WASD  main stick      IJKL     C stick
 *   X = A          Z = B           C = X    V = Y
 *   Q = L          E = R           Tab = Z  Enter = Start
 *   D-pad: T/G/F/H
 *
 * Real keys only count while the window holds keyboard focus; MELEE_KEY_FIFO
 * keys always count (see the comment on s_focused below). MELEE_INPUT_TRACE=1
 * logs one "pad: " line per change of the published pad, with the focus and
 * fifo state that produced it.
 */
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <aurora/event.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/pad.h>
#include <dolphin/gx/GXAurora.h>

#include "pc/pc.h"
#include "pc/touch.h"

static bool s_key[SDL_SCANCODE_COUNT];
static bool s_key_latched[SDL_SCANCODE_COUNT];
static bool s_active;
static bool s_suppressed[SDL_SCANCODE_COUNT];
extern bool pc_menu_is_open(void);

/* Real keys are gated on window focus; MELEE_KEY_FIFO keys are not.
 *
 * SDL's keyboard state array is process-global and SDL_PumpEvents fills it
 * from whatever key events reach the process - and a key event can reach a
 * window that does not hold focus. XSendEvent does exactly that (aim a
 * KeyPress at a window by title and SDL3's X11 backend takes it: it tests
 * xany.send_event only for ConfigureNotify), and under Wine a
 * WM_KEYDOWN can be delivered to a stale foreground window. Without a focus
 * rule the game is driven by whatever is typed elsewhere on the desktop, and
 * because PAD_CONFIRM is A|START (gm_1A36.c:118) a single stray Return walks
 * a menu or drops out of a match.
 *
 * The fifo is asymmetric on purpose: every netplay harness drives menus in
 * windows that are unfocused, and some that are never focused at all, so
 * s_fifo_key, the latch it sets and the s_active arm it performs are all
 * ungated. Do not fold the two sources into one gate - that breaks every
 * harness under tools/.
 *
 * Sampled on the event-pump thread (SDL_GetKeyboardFocus is documented
 * main-thread-only) and read by the 1 kHz poll thread. */
static atomic_bool s_focused;
static bool s_trace; /* MELEE_INPUT_TRACE=1: one line per published pad change */

static const struct {
    SDL_Scancode key;
    u16 button;
} s_button_map[] = {
    {SDL_SCANCODE_X, PAD_BUTTON_A},
    {SDL_SCANCODE_Z, PAD_BUTTON_B},
    {SDL_SCANCODE_C, PAD_BUTTON_X},
    {SDL_SCANCODE_V, PAD_BUTTON_Y},
    {SDL_SCANCODE_Q, PAD_TRIGGER_L},
    {SDL_SCANCODE_E, PAD_TRIGGER_R},
    {SDL_SCANCODE_TAB, PAD_TRIGGER_Z},
    {SDL_SCANCODE_RETURN, PAD_BUTTON_START},
    {SDL_SCANCODE_T, PAD_BUTTON_UP},
    {SDL_SCANCODE_G, PAD_BUTTON_DOWN},
    {SDL_SCANCODE_F, PAD_BUTTON_LEFT},
    {SDL_SCANCODE_H, PAD_BUTTON_RIGHT},
};

static s8 axis(SDL_Scancode neg, SDL_Scancode pos) {
    /* A real GameCube stick reads about +-80 at full deflection. */
    const bool neg_on = s_key[neg] || s_key_latched[neg];
    const bool pos_on = s_key[pos] || s_key_latched[pos];
    return (s8)((pos_on ? 80 : 0) - (neg_on ? 80 : 0));
}

static int i8abs(s8 v) {
    return v < 0 ? -(int)v : v;
}

static SDL_Mutex* s_key_mutex;

/* MELEE_KEY_FIFO=path: test driver that does not depend on X focus. Each
 * line is "Name[+Name...] hold_ms" with SDL scancode names ("Return 150",
 * "X+Down 200"); the keys read as held for hold_ms, one line at a time. */
static bool s_fifo_key[SDL_SCANCODE_COUNT];
static bool s_fifo_started;

static int fifo_thread(void* path) {
    char line[128];
    for (;;) {
        FILE* f = fopen((const char*)path, "r"); /* blocks until a writer opens */
        if (f == NULL) {
            SDL_Delay(200);
            continue;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            SDL_Scancode keys[8];
            int n = 0, hold = 120;
            char* sp = strchr(line, ' ');
            if (sp != NULL) {
                *sp = '\0';
                hold = atoi(sp + 1);
            }
            for (char* tok = strtok(line, "+\n"); tok != NULL && n < 8; tok = strtok(NULL, "+\n")) {
                SDL_Scancode sc = SDL_GetScancodeFromName(tok);
                if (sc != SDL_SCANCODE_UNKNOWN) {
                    keys[n++] = sc;
                }
            }
            if (n == 0) {
                continue;
            }
            SDL_LockMutex(s_key_mutex);
            for (int i = 0; i < n; i++) {
                s_fifo_key[keys[i]] = true;
                s_key_latched[keys[i]] = true;
            }
            s_active = true;
            SDL_UnlockMutex(s_key_mutex);
            SDL_Delay((Uint32)hold);
            SDL_LockMutex(s_key_mutex);
            for (int i = 0; i < n; i++) {
                s_fifo_key[keys[i]] = false;
            }
            SDL_UnlockMutex(s_key_mutex);
            SDL_Delay(100);
        }
        fclose(f);
    }
    return 0;
}

static void lock_keys(void) {
    if (s_key_mutex == NULL) {
        s_key_mutex = SDL_CreateMutex();
    }
    if (s_key_mutex != NULL) {
        SDL_LockMutex(s_key_mutex);
    }
}

static void unlock_keys(void) {
    if (s_key_mutex != NULL) {
        SDL_UnlockMutex(s_key_mutex);
    }
}

/* Mouse in the menus (pc_mouse_menu, used by character and stage select):
 * the pointer drives port 0's hand while it is the last thing that moved,
 * and left/right click press A/B on the keyboard's pad. Only while a menu
 * asked for the mouse in the last half second, so clicks never reach a
 * match. */
static float s_mouse_x, s_mouse_y; /* window points */
static bool s_mouse_moved;         /* since a stick last moved */
static bool s_mouse_button[2];     /* left, right */
static atomic_uint_fast64_t s_mouse_wanted_ms;

static bool mouse_menu_live(void) {
    const uint64_t wanted = atomic_load_explicit(&s_mouse_wanted_ms, memory_order_relaxed);
    return wanted != 0 && SDL_GetTicks() - wanted < 500;
}

/* `u`, `v` in [0,1] across the game image (letterboxing removed). True when
 * the pointer is over it and is driving the hand. */
bool pc_mouse_menu(float* u, float* v) {
    atomic_store_explicit(&s_mouse_wanted_ms, SDL_GetTicks(), memory_order_relaxed);
    const char* test = getenv("MELEE_MOUSE_TEST"); /* "u,v": a fixed pointer, for tests */
    if (test != NULL && test[0] != '\0') {
        return sscanf(test, "%f,%f", u, v) == 2;
    }
    SDL_Window* window = SDL_GetMouseFocus();
    int ww = 0, wh = 0;
    u32 fw = 0, fh = 0;
    if (window == NULL || !s_mouse_moved) {
        return false;
    }
    SDL_GetWindowSize(window, &ww, &wh);
    AuroraGetRenderSize(&fw, &fh);
    if (ww <= 0 || wh <= 0 || fw == 0 || fh == 0) {
        return false;
    }
    /* Aurora fits the framebuffer into the window at its own aspect. */
    float iw = (float)ww, ih = (float)wh;
    const float aspect = (float)fw / (float)fh;
    if (iw / ih > aspect) {
        iw = ih * aspect;
    } else {
        ih = iw / aspect;
    }
    lock_keys();
    const float mx = s_mouse_x, my = s_mouse_y;
    unlock_keys();
    *u = (mx - 0.5f * (ww - iw)) / iw;
    *v = (my - 0.5f * (wh - ih)) / ih;
    return *u >= 0.0f && *u <= 1.0f && *v >= 0.0f && *v <= 1.0f;
}

/* A stick or d-pad hands the cursor back to the controller. */
void pc_mouse_menu_stick_moved(void) {
    s_mouse_moved = false;
}

void pc_keyboard_event(const SDL_Event* e) {
    if (e->type == SDL_EVENT_MOUSE_MOTION) {
        lock_keys();
        s_mouse_x = e->motion.x;
        s_mouse_y = e->motion.y;
        s_mouse_moved = true;
        unlock_keys();
        return;
    }
    if ((e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        (e->button.button == SDL_BUTTON_LEFT || e->button.button == SDL_BUTTON_RIGHT))
    {
        const bool down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const int b = e->button.button == SDL_BUTTON_LEFT ? 0 : 1;
        if (down && !mouse_menu_live()) {
            return;
        }
        const SDL_Scancode key = b == 0 ? SDL_SCANCODE_X : SDL_SCANCODE_Z; /* A, B */
        lock_keys();
        s_mouse_button[b] = down;
        if (down) {
            s_key_latched[key] = true;
            s_active = true;
        }
        unlock_keys();
        return;
    }
    if (e->type != SDL_EVENT_KEY_DOWN && e->type != SDL_EVENT_KEY_UP) {
        return;
    }
    if (e->key.scancode >= SDL_SCANCODE_COUNT || e->key.repeat) {
        return;
    }
    /* This runs on the event-pump thread, so SDL_GetKeyboardFocus is legal. A
     * press that arrives without focus is refused outright: it must not arm
     * s_active and it must not set the latch. Releases still run, they only
     * ever clear state. */
    if (e->type == SDL_EVENT_KEY_DOWN && SDL_GetKeyboardFocus() == NULL) {
        static bool logged;
        if (!logged) {
            logged = true;
            pc_log_line("pad: refusing keyboard input, window is not focused"
                        " (MELEE_KEY_FIFO is unaffected)");
        }
        return;
    }
    lock_keys();
    if (e->type == SDL_EVENT_KEY_DOWN) {
        s_key[e->key.scancode] = true;
        s_key_latched[e->key.scancode] = true;
    } else {
        s_key[e->key.scancode] = false;
    }
    s_active = true;
    unlock_keys();
}

static void trace_locked(const PADStatus* st) {
    static PADStatus last;
    static bool have_last;
    size_t i;
    bool fifo_held = false;
    if (have_last && memcmp(&last, st, sizeof last) == 0) {
        return;
    }
    last = *st;
    have_last = true;
    for (i = 0; i < SDL_SCANCODE_COUNT; i++) {
        if (s_fifo_key[i]) {
            fifo_held = true;
            break;
        }
    }
    /* "focus 0 fifo 0" with a non-zero button means the input came from
     * neither source this file owns - which is the one thing a log could not
     * tell us when a Wine build steered its own menus. */
    pc_log_line("pad: btn %04x stick %d,%d sub %d,%d trig %u,%u focus %d fifo %d", st->button,
        st->stickX, st->stickY, st->substickX, st->substickY, (unsigned)st->triggerLeft,
        (unsigned)st->triggerRight, atomic_load_explicit(&s_focused, memory_order_relaxed) ? 1 : 0,
        fifo_held ? 1 : 0);
}

/* Merge the three sources - polled SDL keyboard state (focus-gated), fifo keys
 * and the latch - into port 0's virtual pad. Call with s_key_mutex held. */
static void publish_locked(void) {
    if (!s_fifo_started) {
        const char* fifo = getenv("MELEE_KEY_FIFO");
        s_fifo_started = true;
        s_trace = getenv("MELEE_INPUT_TRACE") != NULL;
        if (fifo != NULL && fifo[0] != '\0') {
            SDL_DetachThread(SDL_CreateThread(fifo_thread, "keyfifo", (void*)fifo));
            s_active = true;
        }
    }
    PADStatus st = {0};
    bool any_active = false;
    if (s_active) {
        size_t i;
        int n = 0;
        /* SDL3 documents SDL_GetKeyboardState as safe to call from any thread
         * (SDL_keyboard.h: "It is safe to call this function from any
         * thread"), so the poll thread reading it while the main thread pumps
         * is not a race by SDL's contract. */
        const bool* keys = SDL_GetKeyboardState(&n);
        if (n > SDL_SCANCODE_COUNT) {
            n = SDL_SCANCODE_COUNT;
        }
        memset(s_key, 0, sizeof s_key);
        if (atomic_load_explicit(&s_focused, memory_order_relaxed)) {
            memcpy(s_key, keys, (size_t)n);
        }
        for (i = 0; i < SDL_SCANCODE_COUNT; i++) {
            s_key[i] |= s_fifo_key[i];
        }
        s_key[SDL_SCANCODE_X] |= s_mouse_button[0];
        s_key[SDL_SCANCODE_Z] |= s_mouse_button[1];
        for (i = 0; i < SDL_SCANCODE_COUNT; i++) {
            if (pc_menu_is_open())
                s_suppressed[i] = s_key[i];
            else if (!s_key[i])
                s_suppressed[i] = false;
            if (s_suppressed[i])
                s_key[i] = false;
        }
        for (i = 0; i < sizeof(s_button_map) / sizeof(s_button_map[0]); i++) {
            SDL_Scancode key = s_button_map[i].key;
            if (s_key[key] || s_key_latched[key]) {
                st.button |= s_button_map[i].button;
            }
        }
        st.stickX = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        if (st.stickX == 0) {
            st.stickX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
        }
        st.stickY = axis(SDL_SCANCODE_DOWN, SDL_SCANCODE_UP);
        if (st.stickY == 0) {
            st.stickY = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
        }
        st.substickX = axis(SDL_SCANCODE_J, SDL_SCANCODE_L);
        st.substickY = axis(SDL_SCANCODE_K, SDL_SCANCODE_I);
        st.triggerLeft = (s_key[SDL_SCANCODE_Q] || s_key_latched[SDL_SCANCODE_Q]) ? 255 : 0;
        st.triggerRight = (s_key[SDL_SCANCODE_E] || s_key_latched[SDL_SCANCODE_E]) ? 255 : 0;
        memset(s_key_latched, 0, sizeof(s_key_latched));
        any_active = true;
    }

    PADStatus touch_st = {0};
    if (pc_touch_get_status(&touch_st)) {
        st.button |= touch_st.button;
        if (touch_st.stickX != 0 || touch_st.stickY != 0) {
            st.stickX = touch_st.stickX;
            st.stickY = touch_st.stickY;
        }
        if (touch_st.substickX != 0 || touch_st.substickY != 0) {
            st.substickX = touch_st.substickX;
            st.substickY = touch_st.substickY;
        }
        if (touch_st.triggerLeft > st.triggerLeft) {
            st.triggerLeft = touch_st.triggerLeft;
        }
        if (touch_st.triggerRight > st.triggerRight) {
            st.triggerRight = touch_st.triggerRight;
        }
        any_active = true;
    }

    /* Port 1 carries a real GC-adapter controller too (gcadapter.c leaves this
     * slot to us). Keyboard/touch layer on top: dominant stick, OR buttons,
     * max triggers -- the same merge aurora does for virtual pads. Clear once
     * when every source goes quiet so a released button does not stick. */
    PADStatus gc_st;
    if (pc_gcadapter_status(0, &gc_st)) {
        st.button |= gc_st.button;
        if (i8abs(gc_st.stickX) > i8abs(st.stickX))
            st.stickX = gc_st.stickX;
        if (i8abs(gc_st.stickY) > i8abs(st.stickY))
            st.stickY = gc_st.stickY;
        if (i8abs(gc_st.substickX) > i8abs(st.substickX))
            st.substickX = gc_st.substickX;
        if (i8abs(gc_st.substickY) > i8abs(st.substickY))
            st.substickY = gc_st.substickY;
        if (gc_st.triggerLeft > st.triggerLeft)
            st.triggerLeft = gc_st.triggerLeft;
        if (gc_st.triggerRight > st.triggerRight)
            st.triggerRight = gc_st.triggerRight;
        any_active = true;
    }

    static bool s_published0;
    if (any_active) {
        if (s_trace) {
            trace_locked(&st);
        }
        PADSetVirtualStatus(0, &st);
        s_published0 = true;
    } else if (s_published0) {
        PADClearVirtualStatus(0);
        s_published0 = false;
    }
}

/* Frame boundary, on the thread that pumps SDL events (pc_frame_boundary,
 * src/pc/vi.c). This is the ONLY writer of port 0's virtual pad, and it runs
 * exactly once per frame, right after the pump: SDL's keyboard array only
 * changes when the main thread pumps events, so nothing between two frame
 * boundaries can be observed here that the boundary cannot observe. A 1 kHz
 * poll used to call this too (src/pc/input_poll.c, deleted): it re-published
 * identical bytes 16 times a frame into state PADRead merges unlocked, and it
 * cleared the sub-frame latch 1 ms after a press, so what the simulation saw
 * depended on thread interleaving. The latch is consumed here instead: PADRead
 * happens once per pad-alarm period (1/60 s cap, lb_0195.c:84-86), so a press
 * shorter than a frame survives to exactly one read and no further. */
/* Light bars (DualSense, DualShock 4) in Melee's port colours: P1 red, P2
 * blue, P3 yellow, P4 green. Re-sent once a second so a controller plugged
 * in mid-game picks it up; aurora skips pads without an RGB LED. */
static void port_lights(void) {
    static const u8 colors[4][3] = {{255, 0, 0}, {0, 64, 255}, {255, 200, 0}, {0, 200, 0}};
    static unsigned calls;
    if (calls++ % 60 != 0) {
        return;
    }
    for (u32 port = 0; port < 4; port++) {
        PADSetColor(port, colors[port][0], colors[port][1], colors[port][2]);
    }
}

void pc_keyboard_apply(void) {
    pc_gcadapter_apply();
    port_lights();
    const bool focused = SDL_GetKeyboardFocus() != NULL;
    lock_keys();
    if (atomic_exchange_explicit(&s_focused, focused, memory_order_relaxed) && !focused) {
        /* Focus just left. Drop the real-key latch so alt-tabbing mid-hold
         * cannot leave a button pressed, but keep what the fifo holds. */
        for (size_t i = 0; i < SDL_SCANCODE_COUNT; i++) {
            if (!s_fifo_key[i]) {
                s_key_latched[i] = false;
            }
        }
    }
    publish_locked();
    unlock_keys();
}
