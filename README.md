AnalogTV
========

### A composite CRT simulator library geared towards Apple 2 emulators


What is this?
-------------

This is Trevor Blackwell's excellent analog TV simulator ripped out of
XScreenSaver, where's been living for the past 10 years. All the X11
vestiges have been shed and it's presented here as a stand-alone C library
for use with Apple 2 Emulators, or anyone else who wants a fancy way to make
your screen look like an analog monitor.

What's that all mean, exactly? Here's a screenshot:

![Screenshot](https://github.com/caldwell/libanalogtv/raw/master/Screenshot.png)


Prerequisites
-------------

The main code is self-contained. The `analogtv-test` program uses [libgd][1]
to output .png images.

[1]: http://libgd.bitbucket.org/

Using
-----

The easiest way is to use the analogtv-apple2 interface:

```C
struct analogtv_apple2 *a2context = analogtv_apple2_setup(1280, 720);

uint8_t ram[0xC000];

struct framebuffer *fb = analogtv_apple2_render(a2context,
                                                100 /*frame#*/,
                                                30 /*fps*/,
                                                (struct video_mode video_mode) {
                                                    .graphics=true,
                                                    .hires=true,
                                                    .mixed=false,
                                                    .page=0 },
                                                ram);
analogtv_apple2_cleanup(a2context);
```

### High level Apple2 emulator API

#### analogtv_apple2_setup()

```C
struct analogtv_apple2 *analogtv_apple2_setup(unsigned width, unsigned height);
```

Call this once to allocate and return an analogtv_apple2 context. `width`
and `height` are the inital dimensions of the framebuffer. Currently it
asserts on any erors.

#### analogtv_apple2_reconfigure()

```C
void analogtv_apple2_reconfigure(struct analogtv_apple2 *a2context, unsigned width, unsigned height);
```

Call this to resize the framebuffer of a given analogtv_apple2 context to
a new `width` and `height`.

#### analogtv_apple2_render

```C
struct framebuffer *analogtv_apple2_render(struct analogtv_apple2 *a2context,
                                           unsigned frame,
                                           unsigned frames__second,
                                           struct video_mode video_mode,
                                           uint8_t *ram);
```

This function renders a frame into the framebuffer and returns a pointer to
it.

##### Arguments:

*   `frame`

    The current frame number. See `frames__second`

*   `frame__second`

    Frames per second (read `__` as "over"). This is divided into frame
    to get the time elapsed which is used by the simulator and to
    control the "flash" interval. There's nothing magic about the
    relationship between `frames` and `frames__second`: If you don't
    have a set frames per second timer then just pass milliseconds into
    `frames` and `1000` into `frames__second`.

*   `video_mode`

    This structure has several fields that control the rendering process:

    *   `.graphics`

        This `bool` controls whether the screen should be rendered in
        Text or Graphics mode. See Apple device address 0xc050 and
        0xc051.

    *   `.hires`

        This bool controls whether the screen should be Hires or Lowres
        graphics. It has no meaning in Text mode. See Apple device
        address 0xc056 and 0xc057.

    *   `.mixed`

        This bool controls whether the bottom 4 text lines of the screen
        should be displayed when in Graphics mode. It has no meaning in
        Text mode. See Apple device address 0xc052 and 0xc053.


    *   `.page`

        This should be `0` to render page 1 Text or Graphics `1` to
        render page 2. See Apple device address 0xc054 and 0xc055.

*   `ram`

    This should be a pointer to the RAM image of the Apple 2 being
    emulated. The render function will only look at the appropriate
    place for the data to render (based on the settings in
    `video_mode`):

    * 0x0400-0x07FF for Page 1 of Text or Lores Graphics
    * 0x0800-0x0bFF for Page 2 of Text or Lores Graphics
    * 0x4000-0X5FFF for Page 2 of Hires Graphics
    * 0x2000-0x3FFF for Page 1 of Hires Graphics

##### Returns

`analogtv_apple2_render` returns a `struct framebuffer *`:

```C
struct framebuffer {
    unsigned width;
    unsigned bytes_per_line;
    unsigned height;
    void *pixels; // bytewise RGBA
};
```

*   `width` and `height`

    The width and height of the framebuffer in pixels. This *may* be
    different from the width and height requested by `analogtv_apple2_setup`
    or `analogtv_apple2_reconfigure`—The analogtv will choose new dimensions
    if the requested dimensions are too far outside the optimal aspect ratio
    (4:3 <= ratio <= 16:9), or if the dimensions are too small (minimum
    266x200 pixels).

*   `bytes_per_line`

    The number of bytes to get from one row to the next. Currently this is
    always `width` * 4.

*   `pixels`

    A pointer to the pixel data. The data will be bytewise RGBA format. Ie:

        ((uint8_t*)pixels)[0] // red
        ((uint8_t*)pixels)[1] // green
        ((uint8_t*)pixels)[2] // blue
        ((uint8_t*)pixels)[3] // alpha

#### analogtv_apple2_cleanup

```C
void analogtv_apple2_cleanup(struct analogtv_apple2 *a2context);
```

Call this to deallocate an analogtv_apple2 context.

### Low level AnalogTV simulator API

Undocumented for now. I don't actually understand a lot of it! Documentation
patches welcome. :-)

Credits
-------

This code was taken from Jamie Zawinski's excellent [XScreenSaver][2],
written originally by Trevor Blackwell <tlb@tlb.org> in 2003.

David Caldwell <david@porkrind.org> ripped all the X11 out of it and most of
the XScreenSaver (couldn't bear to completely remove the threading module)
and made it a standalone library in 2015.

[2]: http://www.jwz.org/xscreensaver/

* Copyright © 2003, 2004 Trevor Blackwell <tlb@tlb.org>
* Copyright © 1998-2010 Jamie Zawinski <jwz@jwz.org>
* Copyright © 2003, 2004 Trevor Blackwell <tlb@tlb.org>
* Copyright © 2014 Dave Odell <dmo2118@gmail.com> (thread code)
* Copyright © 2015 David Caldwell <david@porkrind.org>

License
-------

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.  No representations are made about the suitability of this
software for any purpose.  It is provided "as is" without express or
implied warranty.
