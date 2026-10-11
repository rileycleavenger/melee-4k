/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_MOUSE_MENU_H
#define PC_MOUSE_MENU_H

#include <stdbool.h>
#include <sysdolphin/baselib/forward.h>

/* The world point on the plane z = plane_z under the mouse pointer, seen
 * through `cobj`, while the mouse is driving the menu cursor (pc_mouse_menu
 * in keyboard.c). False when it is not. */
bool pc_mouse_menu_world(HSD_CObj* cobj, float plane_z, float* x, float* y);

#endif
