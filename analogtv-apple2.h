//  Copyright © 2015 David Caldwell <david@porkrind.org>

#ifndef __ANALOGTV_APPLE2_H__
#define __ANALOGTV_APPLE2_H__

#include <stdbool.h>
#include <stdint.h>
#include "analogtv.h"

struct video_mode {
    bool graphics;
    bool hires;
    bool mixed;
    unsigned page;
};

struct analogtv_apple2;

struct analogtv_apple2 *analogtv_apple2_setup(unsigned width, unsigned height);
void analogtv_apple2_reconfigure(struct analogtv_apple2 *a2context, unsigned width, unsigned height);
struct framebuffer *analogtv_apple2_render(struct analogtv_apple2 *a2context,
                                           unsigned frame,
                                           unsigned frames__second,
                                           struct video_mode video_mode,
                                           uint8_t *ram);
void analogtv_apple2_cleanup(struct analogtv_apple2 *a2context);

#endif /* __ANALOGTV_APPLE2_H__ */
