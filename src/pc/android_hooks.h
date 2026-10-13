/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Platform hooks from the Android Java side
 * (src/pc/android_compat.cpp). Every one of these is a no-op, or returns
 * NULL, on every other platform, so callers need no #ifdef. */
#ifndef PC_ANDROID_HOOKS_H
#define PC_ANDROID_HOOKS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hold a WifiManager.MulticastLock: without it the Wi-Fi stack drops every
 * multicast and broadcast frame not addressed to this device. Online play no
 * longer uses multicast (docs/online.md); kept for the platform layer.
 *
 * Nesting is counted: the OS lock is taken on the 0 -> 1 acquire and dropped
 * on the 1 -> 0 release. An unbalanced release is ignored rather than letting
 * MulticastLock.release() throw. Safe to call from any thread. */
void pc_android_multicast_lock_acquire(void);
void pc_android_multicast_lock_release(void);

/* The user's device name ("Sian's Pixel 7") reduced to a DNS label, or NULL
 * when there is none to be had. gethostname() is no use on Android: stock
 * devices leave the kernel hostname at "localhost", so every instance would
 * announce localhost-<id>._meleepc._udp.local. and claim localhost.local.
 * The result is cached; the buffer is owned by the callee. */
const char* pc_android_device_name(void);

/* Tell the Android touch overlay whether the RmlUi launcher owns the screen.
 * The overlay is the only touch entry point, and its analog-stick capture
 * zone covers the lower-left quarter of the screen, so while the launcher is
 * up it swallowed the taps meant for "Choose disc" -- the launcher is
 * unreachable on a phone with no other input. Inert (all touches pass
 * through, nothing is drawn) while active. Safe to call from any thread. */
void pc_android_set_launcher_active(bool active);

#ifdef __cplusplus
}
#endif

#endif
