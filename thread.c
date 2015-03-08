// Copyright © 2015 David Caldwell <david@porkrind.org>

// Adapted from xscreensaver's thread_util.c:
//   thread_util.c, Copyright (c) 2014 Dave Odell <dmo2118@gmail.com>
//
// Permission to use, copy, modify, distribute, and sell this software and its
// documentation for any purpose is hereby granted without fee, provided that
// the above copyright notice appear in all copies and that both that
// copyright notice and this permission notice appear in supporting
// documentation.  No representations are made about the suitability of this
// software for any purpose.  It is provided "as is" without express or
// implied warranty.

#include <assert.h>
#include <errno.h>
#include "thread.h"

int threadpool_create(struct threadpool *self, const struct threadpool_class *cls, unsigned count)
{
    self->count = count;

    assert(cls);

    self->thread_size = cls->size;
    self->thread_destroy = cls->destroy;

    {
        void *thread = malloc(cls->size * count);
        if(!thread)
            return ENOMEM;

        self->serial_threads = thread;

        for (unsigned i = 0; i != count; ++i) {
            int error = cls->create(thread, self, i);
            if(error) {
                self->count = i;
                threadpool_destroy(self);
                return error;
            }

            thread = (char *)thread + self->thread_size;
        }
    }
    return 0;
}

void threadpool_destroy(struct threadpool *self)
{
    void *thread = self->serial_threads;
    for(unsigned i = 0; i != self->count; ++i) {
        self->thread_destroy(thread);
        thread = (char *)thread + self->thread_size;
    }

    free(self->serial_threads);
}

void threadpool_run(struct threadpool *self, void (*func)(void *))
{
    /* It's perfectly valid to move this to the beginning of threadpool_wait(). */
    void *thread = self->serial_threads;
    for(unsigned i = 0; i != self->count; ++i) {
        func(thread);
        thread = (char *)thread + self->thread_size;
    }
}

void threadpool_wait(struct threadpool *self)
{
}

unsigned thread_memory_alignment()
{
    return sizeof(void *);
}
