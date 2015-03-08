//  Copyright © 2015 David Caldwell <david@porkrind.org>

#ifndef __APPLE2_VIDEO_FANCY_H__
#define __APPLE2_VIDEO_FANCY_H__

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

struct analogtv_apple2 *apple2_video_fancy_setup(unsigned width, unsigned height);
struct framebuffer *apple2_video_fancy_render(struct analogtv_apple2 *a2context,
                                              unsigned frame,
                                              unsigned frames__second,
                                              struct video_mode video_mode,
                                              uint8_t *ram);
void apple2_video_fancy_cleanup(struct analogtv_apple2 *a2context);

#endif /* __APPLE2_VIDEO_FANCY_H__ */
