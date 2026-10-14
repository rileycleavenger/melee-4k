/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_MOUSE_MENU_H
#define PC_MOUSE_MENU_H

#include <stdbool.h>
#include <sysdolphin/baselib/forward.h>

/* The world point on the plane z = plane_z under the mouse pointer, seen
 * through `cobj`, while the mouse is driving the menu cursor (pc_mouse_menu
 * in keyboard.c). False when it is not. */
bool pc_mouse_menu_world(HSD_CObj* cobj, float plane_z, float* x, float* y);

/* The pointer driving `port`'s cursor this frame, in the same world.
 * Offline that is this machine's mouse, for port 0 (the keyboard's port).
 * Online every player's own mouse drives their own port: this machine's is
 * sampled and sent with its input, and every machine reads each port's
 * from the host's bundle (pc_net_pointer), so all of them move the hands
 * alike. */
bool pc_mouse_menu_port(HSD_CObj* cobj, float plane_z, int port, float* x, float* y);
/* A stick moved `port`'s cursor: if that is this machine's port, the mouse
 * stops driving it until it moves again. */
void pc_mouse_menu_port_stick(int port);

#endif
