/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_MENU_HAND_H
#define PC_MENU_HAND_H

#include <stdbool.h>
#include <sysdolphin/baselib/forward.h>

/* The character select hand, following the mouse in menus that have no hand
 * of their own, while the mouse is what moved last (pc_mouse_menu). A menu
 * asks for it once per frame with pc_menu_hand_want(); the scene loop
 * draws it (pc_menu_hand_frame) and drops it with the scene
 * (pc_menu_hand_forget). Game C: it builds HSD objects. */

/* This frame's menu wants the hand. */
void pc_menu_hand_want(void);

/* The hand is showing: the pointer is over the game and drives the menu. */
bool pc_menu_hand_shown(void);

/* The entry under the hand among a list menu's `labels` (NULL entries are
 * skipped), seen through the menu's camera: the label's row, from `left` to
 * `right` of its origin and `half_row` up and down, in that camera's world.
 * -1 when the hand is over none. */
int pc_menu_hand_row(
    HSD_CObj* cobj, HSD_JObj* const* labels, int n, float left, float right, float half_row);

/* Walk the menu's selection toward a hovered entry: +1 steps down, -1 up,
 * 0 none. Fed to the shared menu input (mn_80229624) as up and down
 * presses, so each menu keeps its own sounds and skips its locked entries. */
void pc_menu_hand_step(int dir);
/* That step, handed out once per tick (the shared menu input may be read
 * more than once a tick): -1 up, +1 down, 0 none. */
int pc_menu_hand_take_step(void);
/* Mouse wheel notches since the last call while the hand shows (+ up). */
int pc_menu_hand_take_wheel(void);

/* Scene loop: once per frame after the ticks, and when the scene ends. */
void pc_menu_hand_frame(void);
void pc_menu_hand_forget(void);

#endif
