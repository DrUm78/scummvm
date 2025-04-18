/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <new>

/* newlib ctype.h defines _X for hex digit flag.
   This conflicts with the use of _X as a variable name. */
#undef _X

// Set the proper resolution for the FunKey S in games and menu
#define RES_W 240
#define RES_H 240
#define RES_W_OVERLAY 240
#define RES_H_OVERLAY 240

// HACK: With MinGW, GRIM engine seems to crash when using setjmp and longjmp if not using builtin versions
#if defined __MINGW64__ || defined __MINGW32__
#include <setjmp.h>
#undef setjmp
#undef longjmp
#define setjmp(a) (__builtin_setjmp (a))
#define longjmp(a,b) (__builtin_longjmp (a,b))
#endif
