/* origin: FreeBSD /usr/src/lib/msun/src/s_fmaf.c */
/*-
 * Copyright (c) 2005-2011 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* musl's fmaf, renamed, without the floating-point environment handling:
 * the game only ever runs round-to-nearest and reads no exception flags.
 * aurora's ppc_rsqrte (every PSVECNormalize, so every collision normal)
 * needs a single rounding here. AArch64 does that in hardware and UCRT's
 * fmaf in software, but mingw-w64's msvcrt fmaf rounds twice and was one
 * ULP off a floor normal in the v1.1.0-rc2 Mac-PC test; this is the same
 * bits everywhere. */

#include <stdint.h>

#include "pc_libm.h"

/*
 * Fused multiply-add: Compute x * y + z with a single rounding error.
 *
 * A double has more than twice as much precision than a float, so
 * direct double-precision arithmetic suffices, except where double
 * rounding occurs.
 */
float pc_fmaf(float x, float y, float z)
{
	double xy, result;
	union {double f; uint64_t i;} u;
	int e;

	xy = (double)x * y;
	result = xy + z;
	u.f = result;
	e = u.i>>52 & 0x7ff;
	/* Common case: The double precision result is fine. */
	if ((u.i & 0x1fffffff) != 0x10000000 || /* not a halfway case */
		e == 0x7ff ||                   /* NaN */
		(result - xy == z && result - z == xy)) /* exact */
	{
		z = result;
		return z;
	}

	/*
	 * If result is inexact, and exactly halfway between two float values,
	 * we need to adjust the low-order bit in the direction of the error.
	 */
	double err;
	int neg = u.i >> 63;
	if (neg == (z > xy))
		err = xy - result + z;
	else
		err = z - result + xy;
	if (neg == (err < 0))
		u.i++;
	else
		u.i--;
	z = u.f;
	return z;
}
