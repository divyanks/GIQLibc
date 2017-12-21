/* Linux pause syscall implementation.
   Copyright (C) 2017 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

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

#include <signal.h>
#include <unistd.h>
#include <sysdep-cancel.h>
#include <not-cancel.h>

/* Suspend the process until a signal arrives.
   This always returns -1 and sets errno to EINTR.  */
int
__libc_pause (void)
{
#ifdef __NR_pause
  return SYSCALL_CANCEL (pause);
#else
  return SYSCALL_CANCEL (ppoll, NULL, 0, NULL, NULL);
#endif
}
weak_alias (__libc_pause, pause)

int
__pause_nocancel (void)
{
#ifdef __NR_pause
  return INLINE_SYSCALL_CALL (pause);
#else
  return INLINE_SYSCALL_CALL (ppoll, NULL, 0, NULL, NULL);
#endif
}
libc_hidden_def (__pause_nocancel)
