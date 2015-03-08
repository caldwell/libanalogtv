//  Copyright © 2015 David Caldwell <david@porkrind.org>

#include "apple2-video-fancy.h"
#include "analogtv.h"
#include <assert.h>

static struct framebuffer *fb_alloc(unsigned width, unsigned height)
{
    struct framebuffer *fb = calloc(sizeof(struct framebuffer), 1);
    fb->width = width;
    fb->height = height;
    fb->bytes_per_line = width * 4;
    fb->pixels = malloc(width*height*4);
    return fb;
}

static void fb_free(struct framebuffer *fb)
{
    if (fb) {
        free(fb->pixels);
        free(fb);
    }
}

static unsigned text_line_offset(unsigned y) {
    return (y&7)<<7 | ((y&0x18)>>3) * 40;
}

static unsigned hgr_line_offset(unsigned y) {
    return (y&7)<<10 | text_line_offset(y>>3);
}

static uint8_t apple_2_plus_charset[] = {
    #include "apple2+.charset.h"
};

struct analogtv_apple2 {
    analogtv *atv;
    analogtv_input *input;
    analogtv_reception reception;
};

struct analogtv_apple2 *apple2_video_fancy_setup(unsigned width, unsigned height)
{
    struct analogtv_apple2 *a2context = calloc(sizeof(struct analogtv_apple2), 1);
    assert(a2context);

    a2context->atv = analogtv_allocate(width, height, (struct framebuffer_driver) { .alloc=fb_alloc, .free=fb_free });
    assert(a2context->atv);

    a2context->input = analogtv_input_allocate();
    assert(a2context->input);

    a2context->reception.input = a2context->input;
    a2context->reception.level = 1.0;

    analogtv_set_defaults(a2context->atv);
    a2context->atv->squish_control=0.05;

    return a2context;
}

struct framebuffer *apple2_video_fancy_render(struct analogtv_apple2 *a2context,
                                              unsigned frame,
                                              unsigned frames__second,
                                              struct video_mode video_mode,
                                              uint8_t *ram)
{
    a2context->atv->powerup = (float)frame / frames__second;
    analogtv_setup_sync(a2context->input, video_mode.graphics/*do_cb*/, false/*do_ssavi*/);

    analogtv_setup_frame(a2context->atv);

    float flash_period = 1.44/((12000+2*3300000)*.0000001); // A 555 timer running in astable mode. Formula from National's data sheet. Ra=12K Rb=3.3M C=.1uF
    flash_period /= 8; // hack--my calculations are incorrect and it runs way too slow.
    bool flash_state = (int)((float)frame / frames__second / flash_period) & 1;

    unsigned text_start = ((unsigned[]){0x0400, 0x0800})[video_mode.page]; // text or lo-res
    unsigned hgr_start  = ((unsigned[]){0x2000, 0x4000})[video_mode.page]; // hi-res

    // Fill input
    for (unsigned textrow=0; textrow<24; textrow++) {
        for (unsigned row=textrow*8; row<textrow*8+8; row++) {
            /* First we generate the pattern that the video circuitry shifts out
               of memory. It has a 14.something MHz dot clock, equal to 4 times
               the color burst frequency. So each group of 4 bits defines a color.
               Each character position, or byte in hires, defines 14 dots, so odd
               and even bytes have different color spaces. So, pattern[0..600]
               gets the dots for one scan line. */

            signed char *pp=&a2context->input->signal[row+ANALOGTV_TOP+4][ANALOGTV_PIC_START+100];
            if (video_mode.graphics && !video_mode.hires && (row<160 || !video_mode.mixed)) { // lores
                for (int x=0; x<40; x++) {
                    uint8_t c = ram[text_start + text_line_offset(textrow) + x];
                    uint8_t nib=c >> (((row/4)&1)*4) & 0xf;
                    /* The low or high nybble was shifted out one bit at a time. */
                    for (unsigned i=0; i<14; i++) {
                        *pp = (((nib>>((x*14+i)&3))&1)
                               ?ANALOGTV_WHITE_LEVEL
                               :ANALOGTV_BLACK_LEVEL);
                        pp++;
                    }
                }
            } else { // hires and text
                bool text = !video_mode.graphics || row>=160 && video_mode.mixed;
                unsigned row_addr = text ? text_start + text_line_offset(textrow)
                                         : hgr_start  + hgr_line_offset(row);
                // printf("Row address %3d: %04x\n", row, row_addr);
                /* Emulate the mysterious pink line, due to a bit getting
                   stuck in a shift register between the end of the last
                   row and the beginning of this one. */
                if ((ram[row_addr +  0] & 0x80) &&
                    (ram[row_addr + 39] & 0x40)) {
                    pp[-1]=ANALOGTV_WHITE_LEVEL;
                }

                for (unsigned xs=0; xs<40; xs++) {
                    uint8_t seg = ram[row_addr+xs];
                    if (text) {
                        uint8_t ch = seg, y = row&7;;
                        //seg = charset_scanline(seg, row&7);
                        bool inverse = (ch & 0xc0) == 0 || // Inverse
                                       (ch & 0xc0) == 0x40 && flash_state; // Flash
                        seg = apple_2_plus_charset[(ch & 0x3f ^ 0x20)*8 + y] ^ (inverse ? 0x7f : 0);
                    }


                    int shift=(seg&0x80)?-1:0; // apple2.c has ?0:1 but that gives the wrong colors.

                    /* Each of the low 7 bits in hires mode corresponded to 2 dot
                       clocks, shifted by one if the high bit was set. */
                    for (unsigned i=0; i<7; i++) {
                        pp[shift+1] = pp[shift] = (((seg>>i)&1)
                                                   ?ANALOGTV_WHITE_LEVEL
                                                   :ANALOGTV_BLACK_LEVEL);
                        pp+=2;
                    }
                }
            }
        }
    }

    const analogtv_reception *recs = &a2context->reception; // fake array of receptions
    analogtv_draw(a2context->atv, 0.02, &recs, 1);

    return a2context->atv->framebuffer;
}

void apple2_video_fancy_cleanup(struct analogtv_apple2 *a2context)
{
    analogtv_release(a2context->atv);
    free(a2context);
}
