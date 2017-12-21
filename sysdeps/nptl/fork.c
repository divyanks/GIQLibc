/* Copyright (C) 2002-2017 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sysdep.h>
#include <libio/libioP.h>
#include <tls.h>
#include <hp-timing.h>
#include <ldsodefs.h>
#include <stdio-lock.h>
#include <atomic.h>
#include <nptl/pthreadP.h>
#include <fork.h>
#include <arch-fork.h>
#include <futex-internal.h>
#include <malloc/malloc-internal.h>

static void
fresetlockfiles (void)
{
  _IO_ITER i;

  for (i = _IO_iter_begin(); i != _IO_iter_end(); i = _IO_iter_next(i))
    if ((_IO_iter_file (i)->_flags & _IO_USER_LOCK) == 0)
      _IO_lock_init (*((_IO_lock_t *) _IO_iter_file(i)->_lock));
}


pid_t
__libc_fork (void)
{
  pid_t pid;
  struct used_handler
  {
    struct fork_handler *handler;
    struct used_handler *next;
  } *allp = NULL;

  /* Determine if we are running multiple threads.  We skip some fork
     handlers in the single-thread case, to make fork safer to use in
     signal handlers.  POSIX requires that fork is async-signal-safe,
     but our current fork implementation is not.  */
  bool multiple_threads = THREAD_GETMEM (THREAD_SELF, header.multiple_threads);

  /* Run all the registered preparation handlers.  In reverse order.
     While doing this we build up a list of all the entries.  */
  struct fork_handler *runp;
  while ((runp = __fork_handlers) != NULL)
    {
      /* Make sure we read from the current RUNP pointer.  */
      atomic_full_barrier ();

      unsigned int oldval = runp->refcntr;

      if (oldval == 0)
	/* This means some other thread removed the list just after
	   the pointer has been loaded.  Try again.  Either the list
	   is empty or we can retry it.  */
	continue;

      /* Bump the reference counter.  */
      if (atomic_compare_and_exchange_bool_acq (&__fork_handlers->refcntr,
						oldval + 1, oldval))
	/* The value changed, try again.  */
	continue;

      /* We bumped the reference counter for the first entry in the
	 list.  That means that none of the following entries will
	 just go away.  The unloading code works in the order of the
	 list.

	 While executing the registered handlers we are building a
	 list of all the entries so that we can go backward later on.  */
      while (1)
	{
	  /* Execute the handler if there is one.  */
	  if (runp->prepare_handler != NULL)
	    runp->prepare_handler ();

	  /* Create a new element for the list.  */
	  struct used_handler *newp
	    = (struct used_handler *) alloca (sizeof (*newp));
	  newp->handler = runp;
	  newp->next = allp;
	  allp = newp;

	  /* Advance to the next handler.  */
	  runp = runp->next;
	  if (runp == NULL)
	    break;

	  /* Bump the reference counter for the next entry.  */
	  atomic_increment (&runp->refcntr);
	}

      /* We are done.  */
      break;
    }

  /* If we are not running multiple threads, we do not have to
     preserve lock state.  If fork runs from a signal handler, only
     async-signal-safe functions can be used in the child.  These data
     structures are only used by unsafe functions, so their state does
     not matter if fork was called from a signal handler.  */
  if (multiple_threads)
    {
      _IO_list_lock ();

      /* Acquire malloc locks.  This needs to come last because fork
	 handlers may use malloc, and the libio list lock has an
	 indirect malloc dependency as well (via the getdelim
	 function).  */
      call_function_static_weak (__malloc_fork_lock_parent);
    }

#ifdef ARCH_FORK
  pid = ARCH_FORK ();
#else
# error "ARCH_FORK must be defined so that the CLONE_SETTID flag is used"
  pid = INLINE_SYSCALL (fork, 0);
#endif


  if (pid == 0)
    {
      struct pthread *self = THREAD_SELF;

      /* See __pthread_once.  */
      if (__fork_generation_pointer != NULL)
	*__fork_generation_pointer += __PTHREAD_ONCE_FORK_GEN_INCR;

#if HP_TIMING_AVAIL
      /* The CPU clock of the thread and process have to be set to zero.  */
      hp_timing_t now;
      HP_TIMING_NOW (now);
      THREAD_SETMEM (self, cpuclock_offset, now);
      GL(dl_cpuclock_offset) = now;
#endif

#ifdef __NR_set_robust_list
      /* Initialize the robust mutex list setting in the kernel which has
	 been reset during the fork.  We do not check for errors because if
	 it fails here, it must have failed at process startup as well and
	 nobody could have used robust mutexes.
	 Before we do that, we have to clear the list of robust mutexes
	 because we do not inherit ownership of mutexes from the parent.
	 We do not have to set self->robust_head.futex_offset since we do
	 inherit the correct value from the parent.  We do not need to clear
	 the pending operation because it must have been zero when fork was
	 called.  */
# if __PTHREAD_MUTEX_HAVE_PREV
      self->robust_prev = &self->robust_head;
# endif
      self->robust_head.list = &self->robust_head;
# ifdef SHARED
      if (__builtin_expect (__libc_pthread_functions_init, 0))
	PTHFCT_CALL (ptr_set_robust, (self));
# else
      extern __typeof (__nptl_set_robust) __nptl_set_robust
	__attribute__((weak));
      if (__builtin_expect (__nptl_set_robust != NULL, 0))
	__nptl_set_robust (self);
# endif
#endif

      /* Reset the lock state in the multi-threaded case.  */
      if (multiple_threads)
	{
	  /* Release malloc locks.  */
	  call_function_static_weak (__malloc_fork_unlock_child);

	  /* Reset the file list.  These are recursive mutexes.  */
	  fresetlockfiles ();

	  /* Reset locks in the I/O code.  */
	  _IO_list_resetlock ();
	}

      /* Reset the lock the dynamic loader uses to protect its data.  */
      __rtld_lock_initialize (GL(dl_load_lock));

      /* Run the handlers registered for the child.  */
      while (allp != NULL)
	{
	  if (allp->handler->child_handler != NULL)
	    allp->handler->child_handler ();

	  /* Note that we do not have to wake any possible waiter.
	     This is the only thread in the new process.  The count
	     may have been bumped up by other threads doing a fork.
	     We reset it to 1, to avoid waiting for non-existing
	     thread(s) to release the count.  */
	  allp->handler->refcntr = 1;

	  /* XXX We could at this point look through the object pool
	     and mark all objects not on the __fork_handlers list as
	     unused.  This is necessary in case the fork() happened
	     while another thread called dlclose() and that call had
	     to create a new list.  */

	  allp = allp->next;
	}

      /* Initialize the fork lock.  */
      __fork_lock = LLL_LOCK_INITIALIZER;
    }
  else
    {
      /* Release acquired locks in the multi-threaded case.  */
      if (multiple_threads)
	{
	  /* Release malloc locks, parent process variant.  */
	  call_function_static_weak (__malloc_fork_unlock_parent);

	  /* We execute this even if the 'fork' call failed.  */
	  _IO_list_unlock ();
	}

      /* Run the handlers registered for the parent.  */
      while (allp != NULL)
	{
	  if (allp->handler->parent_handler != NULL)
	    allp->handler->parent_handler ();

	  if (atomic_decrement_and_test (&allp->handler->refcntr)
	      && allp->handler->need_signal)
	    futex_wake (&allp->handler->refcntr, 1, FUTEX_PRIVATE);

	  allp = allp->next;
	}
    }

  return pid;
}
weak_alias (__libc_fork, __fork)
libc_hidden_def (__fork)
weak_alias (__libc_fork, fork)
