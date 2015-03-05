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

void apple2_video_fancy_setup();
struct framebuffer *apple2_video_fancy_render(unsigned frame,
                                              unsigned frames__second,
                                              struct video_mode video_mode,
                                              uint8_t *ram);
void apple2_video_fancy_cleanup();

#endif /* __APPLE2_VIDEO_FANCY_H__ */
