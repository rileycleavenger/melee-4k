/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_INTERP_H
#define PC_INTERP_H

#include <stdbool.h>

/*
 * High-refresh presentation (src/pc/interp.c, src/pc/vi.c).
 *
 * The simulation stays at 60 Hz. After each tick the scene loop
 * (gm_801A4D34) presents as many frames as the display refreshes before the
 * next tick is due, each one blended to the moment it will be on screen, so
 * motion is even at 120, 144 or 240 Hz alike. Nothing the game can observe
 * changes: no retrace is counted, no VI callback or pad alarm runs for the
 * extra frames, and every borrowed transform is restored after each draw.
 */

/// True when in-between frames should be drawn: MELEE_FRAME_INTERP=1 forces
/// it, =0 disables it, and by default it is on when the window's display
/// refreshes at 100 Hz or more. Never during netplay.
bool pc_interp_enabled(void);

/// Simulation ticks run so far (gm_801A4D34), for the MELEE_FPS line.
extern unsigned pc_sim_ticks;

/// Forget every recorded transform (scene change).
void pc_interp_reset(void);

/// Record where every live JObj (world matrix), camera and particle is
/// after the tick that just ran; the previous record becomes the one blended
/// from. JObj matrices are brought up to date first, as the render would.
void pc_interp_capture(void);

/// Overwrite every recorded transform with the value `t` of the way from the
/// previous tick's (0) to this tick's (1). pc_interp_restore() undoes it.
void pc_interp_apply(float t);

/// Put back what pc_interp_apply() overwrote.
void pc_interp_restore(void);

/// Blend factor for the frame being drawn now: where between the previous
/// tick and this one the display will be when the frame reaches the screen
/// (src/pc/vi.c).
float pc_interp_frame_t(void);

/// `t` (between the last tick and the one before) rescaled to a record
/// `ticks` ticks old, as after a boundary that ran several catch-up ticks.
static inline float pc_interp_span_t(float t, int ticks) {
    return ticks > 1 ? 1.0f - (1.0f - t) / (float)ticks : t;
}

/// True when another frame can be presented before the next tick is due.
bool pc_interp_room_for_frame(void);

/// Present the frame drawn since the last boundary and begin the next one,
/// without any of the boundary's game-visible work (src/pc/vi.c).
void pc_present_interp_frame(void);

#endif
