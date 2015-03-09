// Copyright © 2015 David Caldwell <david@porkrind.org>
//
// Permission to use, copy, modify, distribute, and sell this software and its
// documentation for any purpose is hereby granted without fee, provided that
// the above copyright notice appear in all copies and that both that
// copyright notice and this permission notice appear in supporting
// documentation.  No representations are made about the suitability of this
// software for any purpose.  It is provided "as is" without express or
// implied warranty.

#include "analogtv.h"
#include "analogtv-apple2.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <gd.h>

gdImagePtr gd_image_from_fb(struct framebuffer *fb)
{
    gdImagePtr image = gdImageCreateTrueColor(fb->width, fb->height);
    uint8_t (*pix)[fb->height][fb->width*4] = fb->pixels;
    for (unsigned y=0; y<fb->height; y++)
        for (unsigned x=0; x<fb->width; x++)
            image->tpixels[y][x] = (*pix)[y][x*4+2] << 24 | // gd uses ARGB in native word order
                                   (*pix)[y][x*4+0] << 16 |
                                   (*pix)[y][x*4+1] <<  8 |
                                   (*pix)[y][x*4+2] <<  0;
    return image;
}

void save_png(char *filename, gdImagePtr image)
{
    char *data = NULL;
    FILE *out = fopen(filename, "wb");
    if (!out) goto fail;
    int size;
    data = (char *) gdImagePngPtr(image, &size);
    if (!data) goto fail;
    if (fwrite(data, 1, size, out) != size)
        goto fail;
  fail:
    if (out)
        fclose(out);
    gdFree(data);
}

uint8_t text_screen_ram[] = {
#include "text-screen.h"
};

uint8_t hgr_screen_ram[] = {
//#include "hgr-screen.lode.h"
#include "hgr-screen.chop.h"
};

int main(int argc, char **argv)
{
    struct analogtv_apple2 *a2context = analogtv_apple2_setup(1280, 1024);

    struct video_mode video_mode = { .graphics=true, .hires=true, .mixed=false, .page=0 };

    char *filename = "out.png";
    if (argc >= 5) {
        video_mode.graphics    = !!strtoul(argv[1], NULL, 0);
        video_mode.hires       = !!strtoul(argv[2], NULL, 0);
        video_mode.mixed       = !!strtoul(argv[3], NULL, 0);
        video_mode.page        = !!strtoul(argv[4], NULL, 0);
    }
    if (argc >= 6)
        filename = argv[5];

    printf("graphics=%d, hires=%d, mixed=%d, page=%d\n", video_mode.graphics, video_mode.hires, video_mode.mixed, video_mode.page);

    struct framebuffer *fb = analogtv_apple2_render(a2context,
                                                    10000,
                                                    30,
                                                    video_mode,
                                                    video_mode.hires ? hgr_screen_ram : text_screen_ram);

    save_png(filename, gd_image_from_fb(fb));

    analogtv_apple2_cleanup(a2context);

    return 0;
}

