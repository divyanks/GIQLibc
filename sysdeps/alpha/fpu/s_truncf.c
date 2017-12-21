/* Copyright (C) 2007-2017 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Richard Henderson.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library.  If not, see
   <http://www.gnu.org/licenses/>.  */

#include <math.h>
#include <libm-alias-float.h>


/* Use the chopped rounding mode conversion instructions to implement trunc. */

float
__truncf (float x)
{
  float two23 = copysignf (0x1.0p23, x);
  float r, tmp;

  if (isgreaterequal (fabsf (x), 0x1.0p23))
    return x;

  __asm (
	 "adds/suc %2, %3, %1\n\tsubs/suc %1, %3, %0"
	 : "=&f"(r), "=&f"(tmp)
	 : "f"(x), "f"(two23));

  /* trunc(-0) == -0, and in general we'll always have the same
     sign as our input.  */
  return copysignf (r, x);
}

libm_alias_float (__trunc, trunc)
