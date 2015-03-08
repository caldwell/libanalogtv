// Copyright © 2015 David Caldwell <david@porkrind.org>
//
// Adapted from xscreensaver's thread_util.h:
//   thread_util.h, Copyright (c) 2014 Dave Odell <dmo2118@gmail.com>
//
// Permission to use, copy, modify, distribute, and sell this software and its
// documentation for any purpose is hereby granted without fee, provided that
// the above copyright notice appear in all copies and that both that
// copyright notice and this permission notice appear in supporting
// documentation.  No representations are made about the suitability of this
// software for any purpose.  It is provided "as is" without express or
// implied warranty.

#ifndef __THREAD_H__
#define __THREAD_H__

#include <stdlib.h>
#include <stddef.h> // offsetof

struct threadpool
{
/*      This is always the same as the count parameter fed to threadpool_create().
        Here's a neat trick: if the threadpool is zeroed out with a memset, and
        threadpool_create() is never called to create 0 threads, then
        threadpool::count can be used to determine if the threadpool object was
        ever initialized. */
        unsigned count;

        /* Copied from threadpool_class. No need for thread_create here, though. */
        size_t thread_size;
        void (*thread_destroy)(void *self);

        void *serial_threads;
};

struct threadpool_class
{
        /* Size of the thread private object. */
        size_t size;

/*      Create the thread private object. Called in sequence for each thread
        (effectively) from threadpool_create.
    self: A pointer to size bytes of memory, allocated to hold the thread
          object.
    pool: The threadpool object that owns all the threads. If the threadpool
          is nested in another struct, try GET_PARENT_OBJ.
    id:   The ID for the thread; numbering starts at zero and goes up by one
          for each thread.
    Return 0 on success. On failure, return a value from errno.h; this will
    be returned from threadpool_create. */
        int (*create)(void *self, struct threadpool *pool, unsigned id);

/*      Destroys the thread private object. Called in sequence (though not always
        the same sequence as create).  Warning: During shutdown, it is possible
        for destroy() to be called while other threads are still in
        threadpool_run(). */
        void (*destroy)(void *self);
};

int threadpool_create(struct threadpool *self, const struct threadpool_class *cls, unsigned count);
void threadpool_destroy(struct threadpool *self);
void threadpool_run(struct threadpool *self, void (*func)(void *));
void threadpool_wait(struct threadpool *self);
unsigned thread_memory_alignment();

#define hardware_concurrency() 1

#define thread_malloc(ptr, size) (!((*(ptr)) = malloc(size)))
#define thread_free(ptr) free(ptr)
/*
   If a variable 'member' is known to be a member (named 'member_name') of a
   struct (named 'struct_name'), then this can find a pointer to the struct
   that contains it.
*/
#define GET_PARENT_OBJ(struct_name, member_name, member) (struct_name *)((char *)member - offsetof(struct_name, member_name));

#endif /* __THREAD_H__ */

