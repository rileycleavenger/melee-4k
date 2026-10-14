/* SPDX-License-Identifier: GPL-3.0-or-later */
/* One trig implementation for every peer (musl's, see LICENSE). glibc,
 * mingw-w64 and bionic all return different bits for sinf/cosf/tanf/atanf,
 * which would desync netplay; so does mingw-w64's msvcrt fmaf, which rounds
 * twice (pc_fmaf.c), and logf is no better (pc_logf.c). Force-included into the game library
 * (compat.h) and aurora_mtx (CMakeLists.txt) so no call reaches the
 * platform libm. */
#ifndef PC_TRIG_H
#define PC_TRIG_H

#include <math.h>

float pc_sinf(float x);
float pc_cosf(float x);
float pc_tanf(float x);
float pc_atanf(float x);
float pc_fmaf(float x, float y, float z);
float pc_logf(float x);

#define sinf pc_sinf
#define cosf pc_cosf
#define tanf pc_tanf
#define atanf pc_atanf
#define fmaf pc_fmaf
#define logf pc_logf

/* The game's own float maths (lbtrigf.c, lb_00CE.c), renamed with every
 * caller so no call can bind elsewhere: mingw-w64's msvcrt math.h defines
 * powf and expf as inline wrappers around msvcrt's double pow and exp, and
 * the v1.1.0-rc2 Windows build called those instead (wall techs,
 * ftCo_PassiveWall.c). */
float pc_atan2f(float y, float x);
float pc_acosf(float x);
float pc_asinf(float x);
float pc_powf(float x, float y);
float pc_expf(float x);
#undef atan2f
#undef acosf
#undef asinf
#undef powf
#undef expf
#define atan2f pc_atan2f
#define acosf pc_acosf
#define asinf pc_asinf
#define powf pc_powf
#define expf pc_expf

/* MELEE_FP_PERTURB=<name> (CMakeLists.txt) re-routes one of them through the
 * 1-ULP wrapper in pc_perturb.c, which is how tools/net_determinism.py
 * proves it catches a libm divergence. Never on in a shipped build; see the
 * header comment of pc_perturb.c for the four fences. */
#ifdef MELEE_FP_PERTURB_SINF
float pc_perturb_sinf(float x);
#undef sinf
#define sinf pc_perturb_sinf
#endif
#ifdef MELEE_FP_PERTURB_COSF
float pc_perturb_cosf(float x);
#undef cosf
#define cosf pc_perturb_cosf
#endif
#ifdef MELEE_FP_PERTURB_TANF
float pc_perturb_tanf(float x);
#undef tanf
#define tanf pc_perturb_tanf
#endif
#ifdef MELEE_FP_PERTURB_ATANF
float pc_perturb_atanf(float x);
#undef atanf
#define atanf pc_perturb_atanf
#endif

#endif
