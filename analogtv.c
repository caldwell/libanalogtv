/* analogtv, Copyright (c) 2003, 2004 Trevor Blackwell <tlb@tlb.org> -*- c-basic-offset: 2; -*-
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

/*

  This is the code for implementing something that looks like a conventional
  analog TV set. It simulates the following characteristics of standard
  televisions:

  - Realistic rendering of a composite video signal
  - Compression & brightening on the right, as the scan gets truncated
    because of saturation in the flyback transformer
  - Blooming of the picture dependent on brightness
  - Overscan, cutting off a few pixels on the left side.
  - Colored text in mixed graphics/text modes

  It's amazing how much it makes your high-end monitor look like at large
  late-70s TV. All you need is to put a big "Solid State" logo in curly script
  on it and you'd be set.

  In DirectColor or TrueColor modes, it generates pixel values
  directly from RGB values it calculates across each scan line. In
  PseudoColor mode, it consider each possible pattern of 5 preceding
  bit values in each possible position modulo 4 and allocates a color
  for each. A few things, like the brightening on the right side as
  the horizontal trace slows down, aren't done in PseudoColor.

  I originally wrote it for the Apple ][ emulator, and generalized it
  here for use with a rewrite of xteevee and possibly others.

  A maxim of technology is that failures reveal underlying mechanism.
  A good way to learn how something works is to push it to failure.
  The way it fails will usually tell you a lot about how it works. The
  corollary for this piece of software is that in order to emulate
  realistic failures of a TV set, it has to work just like a TV set.
  So there is lots of DSP-style emulation of analog circuitry for
  things like color decoding, H and V sync following, and more. In
  2003, computers are just fast enough to do this at television signal
  rates. We use a 14 MHz sample rate here, so we can do on the order
  of a couple hundred instructions per sample and keep a good frame
  rate.

  Trevor Blackwell <tlb@tlb.org>
*/

/*
  2014-04-20, Dave Odell <dmo2118@gmail.com>:
  API change: Folded analogtv_init_signal and *_add_signal into *_draw().
  Added SMP support.
  Replaced doubles with floats, including constants and transcendental functions.
  Fixed a bug or two.
*/

#include <limits.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "analogtv.h"
#include "random.h"

/* #define DEBUG 1 */

#ifdef DEBUG
#include <stdio.h>
#endif

#if defined(DEBUG) && (defined(__linux) || defined(__FreeBSD__))
/* only works on linux + freebsd */
#include <machine/cpufunc.h>

#define DTIME_DECL u_int64_t dtimes[100]; int n_dtimes
#define DTIME_START do {n_dtimes=0; dtimes[n_dtimes++]=rdtsc(); } while (0)
#define DTIME dtimes[n_dtimes++]=rdtsc()
#define DTIME_SHOW(DIV) \
do { \
  double _dtime_div=(DIV); \
  printf("time/%.1f: ",_dtime_div); \
  for (i=1; i<n_dtimes; i++) \
    printf(" %0.9f",(dtimes[i]-dtimes[i-1])* 1e-9 / _dtime_div); \
  printf("\n"); \
} while (0)

#else

#define DTIME_DECL
#define DTIME_START  do { } while (0)
#define DTIME  do { } while (0)
#define DTIME_SHOW(DIV)  do { } while (0)

#endif


#define FASTRND_A 1103515245
#define FASTRND_C 12345
#define FASTRND (fastrnd = fastrnd*FASTRND_A+FASTRND_C)

static void analogtv_ntsc_to_yiq(const analogtv *it, int lineno, const float *signal,
                                 int start, int end, struct analogtv_yiq_s *it_yiq);

static float puramp(const analogtv *it, float tc, float start, float over)
{
  float pt=it->powerup-start;
  float ret;
  if (pt<0.0f) return 0.0f;
  if (pt>900.0f || pt/tc>8.0f) return 1.0f;

  ret=(1.0f-expf(-pt/tc))*over;
  if (ret>1.0f) return 1.0f;
  return ret*ret;
}

/*
  There are actual standards for TV signals: NTSC and RS-170A describe the
  system used in the US and Japan. Europe has slightly different systems, but
  not different enough to make substantially different screensaver displays.
  Sadly, the standards bodies don't do anything so useful as publish the spec on
  the web. Best bets are:

    http://www.ee.washington.edu/conselec/CE/kuhn/ntsc/95x4.htm
    http://www.ntsc-tv.com/ntsc-index-02.htm

  In DirectColor or TrueColor modes, it generates pixel values directly from RGB
  values it calculates across each scan line. In PseudoColor mode, it consider
  each possible pattern of 5 preceding bit values in each possible position
  modulo 4 and allocates a color for each. A few things, like the brightening on
  the right side as the horizontal trace slows down, aren't done in PseudoColor.

  I'd like to add a bit of visible retrace, but it conflicts with being able to
  bitcopy the image when fast scrolling. After another couple of CPU
  generations, we could probably regenerate the whole image from scratch every
  time. On a P4 2 GHz it can manage this fine for blinking text, but scrolling
  looks too slow.
*/

/* localbyteorder is MSBFirst or LSBFirst */
#define LSBFirst 0
#define MSBFirst 1
static int localbyteorder;
static const double float_low8_ofs=8388608.0;
static int float_extraction_works;

typedef union {
  float f;
  int i;
} float_extract_t;

static void
analogtv_init(void)
{
  int i;
  {
    unsigned int localbyteorder_loc = (MSBFirst<<24) | (LSBFirst<<0);
    localbyteorder=*(char *)&localbyteorder_loc;
  }

  if (1) {
    float_extract_t fe;
    int ans;

    float_extraction_works=1;
    for (i=0; i<256*4; i++) {
      fe.f=float_low8_ofs+(double)i;
      ans=fe.i&0x3ff;
      if (ans != i) {
#ifdef DEBUG
        printf("Float extraction failed for %d => %d\n",i,ans);
#endif
        float_extraction_works=0;
        break;
      }
    }
  }

}

void
analogtv_set_defaults(analogtv *it)
{
  it->color_control = 70/100.0;
  it->brightness_control = 2/100.0;
  it->contrast_control = 150/100.0;
  it->height_control = 1.0;
  it->width_control = 1.0;
  it->squish_control = 0.0;
  it->powerup=1000.0;

  it->hashnoise_rpm=0;
  it->hashnoise_on=0;
  it->hashnoise_enable=1;

  it->horiz_desync=frand(10.0)-5.0;
  it->squeezebottom=frand(5.0)-1.0;

#ifdef DEBUG
  printf("  use: shm=%d cmap=%d color=%d\n",
         it->use_shm,it->use_cmap,it->use_color);
  printf("  controls: tint=%g color=%g brightness=%g contrast=%g\n",
         it->tint_control, it->color_control, it->brightness_control,
         it->contrast_control);
  printf("  desync: %g %d\n",
         it->horiz_desync, it->flutter_horiz_desync);
  printf("  hashnoise rpm: %g\n",
         it->hashnoise_rpm);
  printf("  size: %d %d  %d %d  xrepl=%d\n",
         it->usewidth, it->useheight,
         it->screen_xo, it->screen_yo, it->xrepl);

  printf("    ANALOGTV_V=%d\n",ANALOGTV_V);
  printf("    ANALOGTV_TOP=%d\n",ANALOGTV_TOP);
  printf("    ANALOGTV_VISLINES=%d\n",ANALOGTV_VISLINES);
  printf("    ANALOGTV_BOT=%d\n",ANALOGTV_BOT);
  printf("    ANALOGTV_H=%d\n",ANALOGTV_H);
  printf("    ANALOGTV_SYNC_START=%d\n",ANALOGTV_SYNC_START);
  printf("    ANALOGTV_BP_START=%d\n",ANALOGTV_BP_START);
  printf("    ANALOGTV_CB_START=%d\n",ANALOGTV_CB_START);
  printf("    ANALOGTV_PIC_START=%d\n",ANALOGTV_PIC_START);
  printf("    ANALOGTV_PIC_LEN=%d\n",ANALOGTV_PIC_LEN);
  printf("    ANALOGTV_FP_START=%d\n",ANALOGTV_FP_START);
  printf("    ANALOGTV_PIC_END=%d\n",ANALOGTV_PIC_END);
  printf("    ANALOGTV_HASHNOISE_LEN=%d\n",ANALOGTV_HASHNOISE_LEN);

#endif

}

void
analogtv_reconfigure(analogtv *it, int width, int height)
{
  int oldwidth=it->usewidth;
  int oldheight=it->useheight;
  int wlim,hlim,height_diff;

  /* If the window is very small, don't let the image we draw get lower
     than the actual TV resolution (266x200.)

     If the aspect ratio of the window is close to a 4:3 or 16:9 ratio,
     then scale the image to exactly fill the window.

     Otherwise, center the image either horizontally or vertically,
     letterboxing or pillarboxing (but not both).

     If it's very close (2.5%) to a multiple of VISLINES, make it exact
     For example, it maps 1024 => 1000.
   */
  float percent = 0.15;
  float min_ratio =  4.0 / 3.0 * (1 - percent);
  float max_ratio = 16.0 / 9.0 * (1 + percent);
  float ratio;
  float height_snap=0.025;

  hlim = height;
  wlim = width;
  ratio = wlim / (float) hlim;

#ifdef USE_IPHONE
  /* Fill the whole iPhone screen, even though that distorts the image. */
  min_ratio = 0;
  max_ratio = 10;
#endif

  if (wlim < 266 || hlim < 200)
    {
      wlim = 266;
      hlim = 200;
# ifdef DEBUG
      fprintf (stderr,
               "size: minimal: %dx%d in %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, width, height,
               min_ratio, ratio, max_ratio);
# endif
    }
  else if (ratio > min_ratio && ratio < max_ratio)
    {
# ifdef DEBUG
      fprintf (stderr,
               "size: close enough: %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, min_ratio, ratio, max_ratio);
# endif
    }
  else if (ratio >= max_ratio)
    {
      wlim = hlim*max_ratio;
# ifdef DEBUG
      fprintf (stderr,
               "size: center H: %dx%d in %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, width, height,
               min_ratio, ratio, max_ratio);
# endif
    }
  else /* ratio <= min_ratio */
    {
      hlim = wlim/min_ratio;
# ifdef DEBUG
      fprintf (stderr,
               "size: center V: %dx%d in %dx%d (%.3f < %.3f < %.3f)\n",
               wlim, hlim, width, height,
               min_ratio, ratio, max_ratio);
# endif
    }


  height_diff = ((hlim + ANALOGTV_VISLINES/2) % ANALOGTV_VISLINES) - ANALOGTV_VISLINES/2;
  if (height_diff != 0 && fabs(height_diff) < hlim * height_snap)
    {
      hlim -= height_diff;
    }


  /* Most times this doesn't change */
  if (wlim != oldwidth || hlim != oldheight) {

    it->usewidth=wlim;
    it->useheight=hlim;

    it->xrepl=1+it->usewidth/640;
    if (it->xrepl>2) it->xrepl=2;
    it->subwidth=it->usewidth/it->xrepl;

    it->framebuffer_driver.free(it->framebuffer);
    it->framebuffer = it->framebuffer_driver.alloc(it->usewidth, it->useheight);
  }

  it->screen_xo = (width-it->usewidth)/2;
  it->screen_yo = (height-it->useheight)/2;
}

/* Can be any power-of-two <= 32. 16 a slightly better choice for 2-3 threads. */
#define ANALOGTV_SUBTOTAL_LEN 32

typedef struct analogtv_thread_s
{
  analogtv *it;
  unsigned thread_id;
  size_t signal_start, signal_end;
} analogtv_thread;

#define SIGNAL_OFFSET(thread_id) \
  ((ANALOGTV_SIGNAL_LEN * (thread_id) / threads->count) & align)

static int analogtv_thread_create(void *thread_raw, struct threadpool *threads,
                                  unsigned thread_id)
{
  analogtv_thread *thread = (analogtv_thread *)thread_raw;
  unsigned align;

  thread->it = GET_PARENT_OBJ(analogtv, threads, threads);
  thread->thread_id = thread_id;

  align = thread_memory_alignment() /
            sizeof(thread->it->signal_subtotals[0]);
  if (!align)
    align = 1;
  align = ~(align * ANALOGTV_SUBTOTAL_LEN - 1);

  thread->signal_start = SIGNAL_OFFSET(thread_id);
  thread->signal_end = thread_id + 1 == threads->count ?
                       ANALOGTV_SIGNAL_LEN :
                       SIGNAL_OFFSET(thread_id + 1);

  return 0;
}

static void analogtv_thread_destroy(void *thread_raw)
{
}

analogtv *
analogtv_allocate(int width, int height, struct framebuffer_driver framebuffer_driver)
{
  static const struct threadpool_class cls = {
    sizeof(analogtv_thread),
    analogtv_thread_create,
    analogtv_thread_destroy
  };

  analogtv *it=NULL;
  int i;
  const size_t rx_signal_len = ANALOGTV_SIGNAL_LEN + 2*ANALOGTV_H;

  analogtv_init();

  it=(analogtv *)calloc(1,sizeof(analogtv));
  if (!it) return 0;
  it->threads.count=0;
  it->rx_signal=NULL;
  it->signal_subtotals=NULL;

  if (thread_malloc((void **)&it->rx_signal,
                    sizeof(it->rx_signal[0]) * rx_signal_len))
    goto fail;

  assert(!(ANALOGTV_SIGNAL_LEN % ANALOGTV_SUBTOTAL_LEN));
  if (thread_malloc((void **)&it->signal_subtotals,
                    sizeof(it->signal_subtotals[0]) *
                     (rx_signal_len / ANALOGTV_SUBTOTAL_LEN)))
    goto fail;

  if (threadpool_create(&it->threads, &cls, hardware_concurrency()))
    goto fail;

  assert(it->threads.count);

  it->shrinkpulse=-1;

  {
    int red_invprec, red_shift;
    int green_invprec, green_shift;
    int blue_invprec, blue_shift;
    red_invprec = green_invprec = blue_invprec = 16 - 8;
    if (localbyteorder == MSBFirst) {
      red_shift   = 24;
      green_shift = 16;
      blue_shift  = 8;
      it->alpha_value = 0x000000ff;
    } else {
      red_shift   = 0;
      green_shift = 8;
      blue_shift  = 16;
      it->alpha_value = 0xff000000;
    }

    for (i=0; i<ANALOGTV_CV_MAX; i++) {
      int intensity=pow(i/256.0, 0.8)*65535.0; /* gamma correction */
      if (intensity>65535) intensity=65535;
      it->red_values[i]=((intensity>>red_invprec)<<red_shift);
      it->green_values[i]=((intensity>>green_invprec)<<green_shift);
      it->blue_values[i]=((intensity>>blue_invprec)<<blue_shift);
    }
    // printf("red   %08x -> %08x\n", it->red_values[0],   it->red_values[ANALOGTV_CV_MAX-1]);
    // printf("green %08x -> %08x\n", it->green_values[0], it->green_values[ANALOGTV_CV_MAX-1]);
    // printf("blue  %08x -> %08x\n", it->blue_values[0],  it->blue_values[ANALOGTV_CV_MAX-1]);
    // printf("alpha %8s    %08x\n", "", it->alpha_value);
  }

  it->framebuffer_driver = framebuffer_driver;
  analogtv_reconfigure(it, width, height);

  return it;

 fail:
  if (it) {
    if(it->threads.count)
      threadpool_destroy(&it->threads);
    thread_free(it->signal_subtotals);
    thread_free(it->rx_signal);
    free(it);
  }
  return NULL;
}

void
analogtv_release(analogtv *it)
{
  if (it->framebuffer)
    it->framebuffer_driver.free(it->framebuffer);
  threadpool_destroy(&it->threads);
  thread_free(it->rx_signal);
  thread_free(it->signal_subtotals);
  free(it);
}


/*
  First generate the I and Q reference signals, which we'll multiply
  the input signal by to accomplish the demodulation. Normally they
  are shifted 33 degrees from the colorburst. I think this was convenient
  for inductor-capacitor-vacuum tube implementation.

  The tint control, FWIW, just adds a phase shift to the chroma signal,
  and the color control controls the amplitude.

  In text modes (colormode==0) the system disabled the color burst, and no
  color was detected by the monitor.

  freq_error gives a mismatch between the built-in oscillator and the
  TV's colorbust. Some II Plus machines seemed to occasionally get
  instability problems -- the crystal oscillator was a single
  transistor if I remember correctly -- and the frequency would vary
  enough that the tint would change across the width of the screen.
  The left side would be in correct tint because it had just gotten
  resynchronized with the color burst.

*/


/* Here we model the analog circuitry of an NTSC television.
   Basically, it splits the signal into 3 signals: Y, I and Q. Y
   corresponds to luminance, and you get it by low-pass filtering the
   input signal to below 3.57 MHz.

   I and Q are the in-phase and quadrature components of the 3.57 MHz
   subcarrier. We get them by multiplying by cos(3.57 MHz*t) and
   sin(3.57 MHz*t), and low-pass filtering. Because the eye has less
   resolution in some colors than others, the I component gets
   low-pass filtered at 1.5 MHz and the Q at 0.5 MHz. The I component
   is approximately orange-blue, and Q is roughly purple-green. See
   http://www.ntsc-tv.com for details.

   We actually do an awful lot to the signal here. I suspect it would
   make sense to wrap them all up together by calculating impulse
   response and doing FFT convolutions.

*/

static void
analogtv_ntsc_to_yiq(const analogtv *it, int lineno, const float *signal,
                     int start, int end, struct analogtv_yiq_s *it_yiq)
{
  enum {MAXDELAY=32};
  int i;
  const float *sp;
  int phasecorr=(signal-it->rx_signal)&3;
  struct analogtv_yiq_s *yiq;
  int colormode;
  float agclevel=it->agclevel;
  float brightadd=it->brightness_control*100.0 - ANALOGTV_BLACK_LEVEL;
  float delay[MAXDELAY+ANALOGTV_PIC_LEN], *dp;
  float multiq2[4];

  {

    double cb_i=(it->line_cb_phase[lineno][(2+phasecorr)&3]-
                 it->line_cb_phase[lineno][(0+phasecorr)&3])/16.0;
    double cb_q=(it->line_cb_phase[lineno][(3+phasecorr)&3]-
                 it->line_cb_phase[lineno][(1+phasecorr)&3])/16.0;

    colormode = (cb_i * cb_i + cb_q * cb_q) > 2.8;

    if (colormode) {
      double tint_i = -cos((103 + it->color_control)*3.1415926/180);
      double tint_q = sin((103 + it->color_control)*3.1415926/180);

      multiq2[0] = (cb_i*tint_i - cb_q*tint_q) * it->color_control;
      multiq2[1] = (cb_q*tint_i + cb_i*tint_q) * it->color_control;
      multiq2[2]=-multiq2[0];
      multiq2[3]=-multiq2[1];
    }
  }

#if 0
  if (lineno==100) {
    printf("it->line_cb_phase = [%0.3f %0.3f %0.3f %0.3f]\n",
           it->line_cb_phase[lineno][0],it->line_cb_phase[lineno][1],
           it->line_cb_phase[lineno][2],it->line_cb_phase[lineno][3]);
    printf("multiq2 = [%0.3f %0.3f %0.3f %0.3f]\n",
           multiq2[0],multiq2[1],multiq2[2],multiq2[3]);
  }
#endif

  dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
  for (i=0; i<5; i++) dp[i]=0.0f;

  assert(start>=0);
  assert(end < ANALOGTV_PIC_LEN+10);

  dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
  for (i=0; i<24; i++) dp[i]=0.0;
  for (i=start, yiq=it_yiq+start, sp=signal+start;
       i<end;
       i++, dp--, yiq++, sp++) {

    /* Now filter them. These are infinite impulse response filters
       calculated by the script at
       http://www-users.cs.york.ac.uk/~fisher/mkfilter. This is
       fixed-point integer DSP, son. No place for wimps. We do it in
       integer because you can count on integer being faster on most
       CPUs. We care about speed because we need to recalculate every
       time we blink text, and when we spew random bytes into screen
       memory. This is roughly 16.16 fixed point arithmetic, but we
       scale some filter values up by a few bits to avoid some nasty
       precision errors. */

    /* Filter Y with a 4-pole low-pass Butterworth filter at 3.5 MHz
       with an extra zero at 3.5 MHz, from
       mkfilter -Bu -Lp -o 4 -a 2.1428571429e-01 0 -Z 2.5e-01 -l
       Delay about 2 */

    dp[0] = sp[0] * 0.0469904257251935f * agclevel;
    dp[8] = (+1.0f*(dp[6]+dp[0])
             +4.0f*(dp[5]+dp[1])
             +7.0f*(dp[4]+dp[2])
             +8.0f*(dp[3])
             -0.0176648f*dp[12]
             -0.4860288f*dp[10]);
    yiq->y = dp[8] + brightadd;
  }

  if (colormode) {
    dp=delay+ANALOGTV_PIC_LEN-MAXDELAY;
    for (i=0; i<27; i++) dp[i]=0.0;

    for (i=start, yiq=it_yiq+start, sp=signal+start;
         i<end;
         i++, dp--, yiq++, sp++) {
      float sig=*sp;

      /* Filter I and Q with a 3-pole low-pass Butterworth filter at
         1.5 MHz with an extra zero at 3.5 MHz, from
         mkfilter -Bu -Lp -o 3 -a 1.0714285714e-01 0 -Z 2.5000000000e-01 -l
         Delay about 3.
      */

      dp[0] = sig*multiq2[i&3] * 0.0833333333333f;
      yiq->i=dp[8] = (dp[5] + dp[0]
                      +3.0f*(dp[4] + dp[1])
                      +4.0f*(dp[3] + dp[2])
                      -0.3333333333f * dp[10]);

      dp[16] = sig*multiq2[(i+3)&3] * 0.0833333333333f;
      yiq->q=dp[24] = (dp[16+5] + dp[16+0]
                       +3.0f*(dp[16+4] + dp[16+1])
                       +4.0f*(dp[16+3] + dp[16+2])
                       -0.3333333333f * dp[24+2]);
    }
  } else {
    for (i=start, yiq=it_yiq+start; i<end; i++, yiq++) {
      yiq->i = yiq->q = 0.0f;
    }
  }
}

void
analogtv_setup_teletext(analogtv_input *input)
{
  int x,y;
  int teletext=ANALOGTV_BLACK_LEVEL;

  /* Teletext goes in line 21. But I suspect there are other things
     in the vertical retrace interval */

  for (y=19; y<22; y++) {
    for (x=ANALOGTV_PIC_START; x<ANALOGTV_PIC_END; x++) {
      if ((x&7)==0) {
        teletext=(random()&1) ? ANALOGTV_WHITE_LEVEL : ANALOGTV_BLACK_LEVEL;
      }
      input->signal[y][x]=teletext;
    }
  }
}

void
analogtv_setup_frame(analogtv *it)
{
  int i,x,y;

  if (it->flutter_horiz_desync) {
    /* Horizontal sync during vertical sync instability. */
    it->horiz_desync += -0.10*(it->horiz_desync-3.0) +
      ((int)(random()&0xff)-0x80) *
      ((int)(random()&0xff)-0x80) *
      ((int)(random()&0xff)-0x80) * 0.000001;
  }

  for (i=0; i<ANALOGTV_V; i++) {
    it->hashnoise_times[i]=0;
  }

  if (it->hashnoise_enable && !it->hashnoise_on) {
    if (random()%10000==0) {
      it->hashnoise_on=1;
      it->shrinkpulse=random()%ANALOGTV_V;
    }
  }
  if (random()%1000==0) {
    it->hashnoise_on=0;
  }
  if (it->hashnoise_on) {
    it->hashnoise_rpm += (15000.0 - it->hashnoise_rpm)*0.05 +
      ((int)(random()%2000)-1000)*0.1;
  } else {
    it->hashnoise_rpm -= 100 + 0.01*it->hashnoise_rpm;
    if (it->hashnoise_rpm<0.0) it->hashnoise_rpm=0.0;
  }
  if (it->hashnoise_rpm > 0.0) {
    int hni;
    double hni_double;
    int hnc=it->hashnoise_counter; /* in 24.8 format */

    /* Convert rpm of a 16-pole motor into dots in 24.8 format */
    hni_double = ANALOGTV_V * ANALOGTV_H * 256.0 /
                (it->hashnoise_rpm * 16.0 / 60.0 / 60.0);
    hni = (hni_double <= INT_MAX) ? (int)hni_double : INT_MAX;

    while (hnc < (ANALOGTV_V * ANALOGTV_H)<<8) {
      y=(hnc>>8)/ANALOGTV_H;
      x=(hnc>>8)%ANALOGTV_H;

      if (x>0 && x<ANALOGTV_H - ANALOGTV_HASHNOISE_LEN) {
        it->hashnoise_times[y]=x;
      }
      /* hnc += hni + (int)(random()%65536)-32768; */
      {
        hnc += (int)(random()%65536)-32768;
        if ((hnc >= 0) && (INT_MAX - hnc < hni)) break;
        hnc += hni;
      }
    }
/*    hnc -= (ANALOGTV_V * ANALOGTV_H)<<8;*/
  }

  if (it->rx_signal_level != 0.0)
    it->agclevel = 1.0/it->rx_signal_level;


#ifdef DEBUG2
  printf("filter: ");
  for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
    printf(" %0.3f",it->ghostfir[i]);
  }
  printf(" siglevel=%g agc=%g\n", siglevel, it->agclevel);
#endif
}

void
analogtv_setup_sync(analogtv_input *input, int do_cb, int do_ssavi)
{
  int i,lineno,vsync;
  signed char *sig;

  int synclevel = do_ssavi ? ANALOGTV_WHITE_LEVEL : ANALOGTV_SYNC_LEVEL;

  for (lineno=0; lineno<ANALOGTV_V; lineno++) {
    vsync=lineno>=3 && lineno<7;

    sig=input->signal[lineno];

    i=ANALOGTV_SYNC_START;
    if (vsync) {
      while (i<ANALOGTV_BP_START) sig[i++]=ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_H) sig[i++]=synclevel;
    } else {
      while (i<ANALOGTV_BP_START) sig[i++]=synclevel;
      while (i<ANALOGTV_PIC_START) sig[i++]=ANALOGTV_BLANK_LEVEL;
      while (i<ANALOGTV_FP_START) sig[i++]=ANALOGTV_BLACK_LEVEL;
    }
    while (i<ANALOGTV_H) sig[i++]=ANALOGTV_BLANK_LEVEL;

    if (do_cb) {
      /* 9 cycles of colorburst */
      for (i=ANALOGTV_CB_START; i<ANALOGTV_CB_START+36; i+=4) {
        sig[i+1] += ANALOGTV_CB_LEVEL;
        sig[i+3] -= ANALOGTV_CB_LEVEL;
      }
    }
  }
}

static void
analogtv_sync(analogtv *it)
{
  int cur_hsync=it->cur_hsync;
  int cur_vsync=it->cur_vsync;
  int lineno = 0;
  int i,j;
  float osc,filt;
  float *sp;
  float cbfc=1.0f/128.0f;

/*  sp = it->rx_signal + lineno*ANALOGTV_H + cur_hsync;*/
  for (i=-32; i<32; i++) {
    lineno = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;
    sp = it->rx_signal + lineno*ANALOGTV_H;
    filt=0.0f;
    for (j=0; j<ANALOGTV_H; j+=ANALOGTV_H/16) {
      filt += sp[j];
    }
    filt *= it->agclevel;

    osc = (float)(ANALOGTV_V+i)/(float)ANALOGTV_V;

    if (osc >= 1.05f+0.0002f * filt) break;
  }
  cur_vsync = (cur_vsync + i + ANALOGTV_V) % ANALOGTV_V;

  for (lineno=0; lineno<ANALOGTV_V; lineno++) {

    if (lineno>5 && lineno<ANALOGTV_V-3) { /* ignore vsync interval */
      unsigned lineno2 = (lineno + cur_vsync + ANALOGTV_V)%ANALOGTV_V;
      if (!lineno2) lineno2 = ANALOGTV_V;
      sp = it->rx_signal + lineno2*ANALOGTV_H + cur_hsync;
      for (i=-8; i<8; i++) {
        osc = (float)(ANALOGTV_H+i)/(float)ANALOGTV_H;
        filt=(sp[i-3]+sp[i-2]+sp[i-1]+sp[i]) * it->agclevel;

        if (osc >= 1.005f + 0.0001f*filt) break;
      }
      cur_hsync = (cur_hsync + i + ANALOGTV_H) % ANALOGTV_H;
    }

    it->line_hsync[lineno]=(cur_hsync + ANALOGTV_PIC_START +
                            ANALOGTV_H) % ANALOGTV_H;

    /* Now look for the colorburst, which is a few cycles after the H
       sync pulse, and store its phase.
       The colorburst is 9 cycles long, and we look at the middle 5
       cycles.
    */

    if (lineno>15) {
      sp = it->rx_signal + lineno*ANALOGTV_H + (cur_hsync&~3);
      for (i=ANALOGTV_CB_START+8; i<ANALOGTV_CB_START+36-8; i++) {
        it->cb_phase[i&3] = it->cb_phase[i&3]*(1.0f-cbfc) +
          sp[i]*it->agclevel*cbfc;
      }
    }

    {
      float tot=0.1f;
      float cbgain;

      for (i=0; i<4; i++) {
        tot += it->cb_phase[i]*it->cb_phase[i];
      }
      cbgain = 32.0f/sqrtf(tot);

      for (i=0; i<4; i++) {
        it->line_cb_phase[lineno][i]=it->cb_phase[i]*cbgain;
      }
    }

#ifdef DEBUG
    if (0) printf("hs=%d cb=[%0.3f %0.3f %0.3f %0.3f]\n",
                  cur_hsync,
                  it->cb_phase[0], it->cb_phase[1],
                  it->cb_phase[2], it->cb_phase[3]);
#endif

    /* if (random()%2000==0) cur_hsync=random()%ANALOGTV_H; */
  }

  it->cur_hsync = cur_hsync;
  it->cur_vsync = cur_vsync;
}

/*
  The point of this stuff is to ensure that when useheight is not a
  multiple of VISLINES so that TV scan lines map to different numbers
  of vertical screen pixels, the total brightness of each scan line
  remains the same.
  ANALOGTV_MAX_LINEHEIGHT corresponds to 2400 vertical pixels, beyond which
  it interpolates extra black lines.
 */

static void
analogtv_setup_levels(analogtv *it, double avgheight)
{
  int i,height;
  static const double levelfac[3]={-7.5, 5.5, 24.5};

  for (height=0; height<avgheight+2.0 && height<=ANALOGTV_MAX_LINEHEIGHT; height++) {

    for (i=0; i<height; i++) {
      it->leveltable[height][i].index = 2;
    }
    
    if (avgheight>=3) {
      it->leveltable[height][0].index=0;
    }
    if (avgheight>=5) {
      if (height >= 1) it->leveltable[height][height-1].index=0;
    }
    if (avgheight>=7) {
      it->leveltable[height][1].index=1;
      if (height >= 2) it->leveltable[height][height-2].index=1;
    }

    for (i=0; i<height; i++) {
      it->leveltable[height][i].value = 
        (40.0 + levelfac[it->leveltable[height][i].index]*puramp(it, 3.0, 6.0, 1.0)) / 256.0;
    }

  }
}

static void rnd_combine(unsigned *a0, unsigned *c0, unsigned a1, unsigned c1)
{
  *a0 = (*a0 * a1) & 0xffffffffu;
  *c0 = (c1 + a1 * *c0) & 0xffffffffu;
}

static void rnd_seek_ac(unsigned *a, unsigned *c, unsigned dist)
{
  unsigned int a1 = *a, c1 = *c;
  *a = 1, *c = 0;

  while(dist)
  {
    if(dist & 1)
      rnd_combine(a, c, a1, c1);
    dist >>= 1;
    rnd_combine(&a1, &c1, a1, c1);
  }
}

static unsigned int rnd_seek(unsigned a, unsigned c, unsigned rnd, unsigned dist)
{
  rnd_seek_ac(&a, &c, dist);
  return a * rnd + c;
}

static void analogtv_init_signal(const analogtv *it, double noiselevel, unsigned start, unsigned end)
{
  float *ps=it->rx_signal + start;
  float *pe=it->rx_signal + end;
  float *p=ps;
  unsigned int fastrnd=rnd_seek(FASTRND_A, FASTRND_C, it->random0, start);
  unsigned int fastrnd_offset;
  float nm1,nm2;
  float noisemul = sqrt(noiselevel*150)/(float)0x7fffffff;

  fastrnd_offset = fastrnd - 0x7fffffff;
  nm1 = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * noisemul;
  while (p != pe) {
    nm2=nm1;
    fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;
    fastrnd_offset = fastrnd - 0x7fffffff;
    nm1 = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * noisemul;
    *p++ = nm1*nm2;
  }
}

static void analogtv_add_signal(const analogtv *it, const analogtv_reception *rec, unsigned start, unsigned end, int ec)
{
  analogtv_input *inp=rec->input;
  float *ps=it->rx_signal + start;
  float *pe=it->rx_signal + end;
  float *p=ps;
  signed char *ss=&inp->signal[0][0];
  signed char *se=&inp->signal[0][0] + ANALOGTV_SIGNAL_LEN;
  signed char *s=ss + ((start + (unsigned)rec->ofs) % ANALOGTV_SIGNAL_LEN);
  signed char *s2;
  int i;
  float level=rec->level;
  float hfloss=rec->hfloss;
  unsigned int fastrnd=rnd_seek(FASTRND_A, FASTRND_C, it->random1, start);
  float dp[5];

  const float noise_decay = 0.99995f;
  float noise_ampl = 1.3f * powf(noise_decay, start);

  if (ec > end)
    ec = end;

  /* assert((se-ss)%4==0 && (se-s)%4==0); */

  for (i = start; i < ec; i++) { /* Sometimes start > ec. */

    /* Do a big noisy transition. We can make the transition noise of
       high constant strength regardless of signal strength.

       There are two separate state machines. here, One is the noise
       process and the other is the

       We don't bother with the FIR filter here
    */

    float sig0=(float)s[0];
    unsigned int fastrnd_offset = fastrnd - 0x7fffffff;
    float noise = (fastrnd_offset <= INT_MAX ? (int)fastrnd_offset : -1 - (int)(UINT_MAX - fastrnd_offset)) * (50.0f/(float)0x7fffffff);
    fastrnd = (fastrnd*FASTRND_A+FASTRND_C) & 0xffffffffu;

    p[0] += sig0 * level * (1.0f - noise_ampl) + noise * noise_ampl;

    noise_ampl *= noise_decay;

    p++;
    s++;
    if (s>=se) s=ss;
  }

  dp[0]=0.0;
  s2 = s;
  for (i=1; i<5; i++) {
    s2 -= 4;
    if (s2 < ss)
      s2 += ANALOGTV_SIGNAL_LEN;
    dp[i] = (float)((int)s2[0]+(int)s2[1]+(int)s2[2]+(int)s2[3]);
  }

  assert(p <= pe);
  assert(!((pe - p) % 4));
  while (p != pe) {
    float sig0,sig1,sig2,sig3,sigr;

    sig0=(float)s[0];
    sig1=(float)s[1];
    sig2=(float)s[2];
    sig3=(float)s[3];

    dp[0]=sig0+sig1+sig2+sig3;

    /* Get the video out signal, and add some ghosting, typical of RF
       monitor cables. This corresponds to a pretty long cable, but
       looks right to me.
    */

    sigr=(dp[1]*rec->ghostfir[0] + dp[2]*rec->ghostfir[1] +
          dp[3]*rec->ghostfir[2] + dp[4]*rec->ghostfir[3]);
    dp[4]=dp[3]; dp[3]=dp[2]; dp[2]=dp[1]; dp[1]=dp[0];

    p[0] += (sig0+sigr + sig2*hfloss) * level;
    p[1] += (sig1+sigr + sig3*hfloss) * level;
    p[2] += (sig2+sigr + sig0*hfloss) * level;
    p[3] += (sig3+sigr + sig1*hfloss) * level;

    p += 4;
    s += 4;
    if (s>=se) s = ss + (s-se);
  }

  assert(p == pe);
}

static void analogtv_thread_add_signals(void *thread_raw)
{
  const analogtv_thread *thread = (analogtv_thread *)thread_raw;
  const analogtv *it = thread->it;
  unsigned i, j;
  unsigned subtotal_end;
  
  unsigned start = thread->signal_start;
  while(start != thread->signal_end)
  {
    float *p;
    
    /* Work on 8 KB blocks; these should fit in L1. */
    /* (Though it doesn't seem to help much on my system.) */
    unsigned end = start + 2048;
    if(end > thread->signal_end)
      end = thread->signal_end;

    analogtv_init_signal (it, it->noiselevel, start, end);

    for (i = 0; i != it->rec_count; ++i) {
      analogtv_add_signal (it, it->recs[i], start, end,
                           !i ? it->channel_change_cycles : 0);
    }

    assert (!(start % ANALOGTV_SUBTOTAL_LEN));
    assert (!(end % ANALOGTV_SUBTOTAL_LEN));

    p = it->rx_signal + start;
    subtotal_end = end / ANALOGTV_SUBTOTAL_LEN;
    for (i = start / ANALOGTV_SUBTOTAL_LEN; i != subtotal_end; ++i) {
      float sum = p[0];
      for (j = 1; j != ANALOGTV_SUBTOTAL_LEN; ++j)
        sum += p[j];
      it->signal_subtotals[i] = sum;
      p += ANALOGTV_SUBTOTAL_LEN;
    }
    
    start = end;
  }
}

static int analogtv_get_line(const analogtv *it, int lineno, int *slineno,
                             int *ytop, int *ybot, unsigned *signal_offset)
{
  *slineno=lineno-ANALOGTV_TOP;
  *ytop=(int)((*slineno*it->useheight/ANALOGTV_VISLINES -
                  it->useheight/2)*it->puheight) + it->useheight/2;
  *ybot=(int)(((*slineno+1)*it->useheight/ANALOGTV_VISLINES -
                  it->useheight/2)*it->puheight) + it->useheight/2;
#if 0
  int linesig=analogtv_line_signature(input,lineno)
    + it->hashnoise_times[lineno];
#endif
  *signal_offset = ((lineno+it->cur_vsync+ANALOGTV_V) % ANALOGTV_V) * ANALOGTV_H +
                    it->line_hsync[lineno];

  if (*ytop==*ybot) return 0;
  if (*ybot<0 || *ytop>it->useheight) return 0;
  if (*ytop<0) *ytop=0;
  if (*ybot>it->useheight) *ybot=it->useheight;

  if (*ybot > *ytop+ANALOGTV_MAX_LINEHEIGHT) *ybot=*ytop+ANALOGTV_MAX_LINEHEIGHT;
  return 1;
}

static void
analogtv_blast_imagerow(const analogtv *it,
                        float *rgbf, float *rgbf_end,
                        int ytop, int ybot)
{
  int i,y;
  float *rpf;
  char *level_copyfrom[3];
  int xrepl=it->xrepl;
  unsigned lineheight = ybot - ytop;
  if (lineheight > ANALOGTV_MAX_LINEHEIGHT) lineheight = ANALOGTV_MAX_LINEHEIGHT;
  for (i=0; i<3; i++) level_copyfrom[i]=NULL;

  for (y=ytop; y<ybot; y++) {
    char *rowdata=it->framebuffer->pixels + y*it->framebuffer->bytes_per_line;
    unsigned line = y-ytop;

    int level=it->leveltable[lineheight][line].index;
    float levelmult=it->leveltable[lineheight][line].value;

    /* Fast special cases to avoid the slow XPutPixel. Ugh. It goes to show
       why standard graphics sw has to be fast, or else people will have to
       work around it and risk incompatibility. The quickdraw folks
       understood this. The other answer would be for X11 to have fewer
       formats for bitm.. oh, never mind. If neither of these cases work
       (they probably cover 99% of setups) it falls back on the Xlib
       routines. */

    if (level_copyfrom[level]) {
      memcpy(rowdata, level_copyfrom[level], it->framebuffer->bytes_per_line);
    }
    else {
      level_copyfrom[level] = rowdata;

      if (1/* all we support is 32 bit RGBA format */) {
        /* int is more likely to be 32 bits than long */
        unsigned int *pixelptr=(unsigned int *)rowdata;
        unsigned int pix;

        for (rpf=rgbf; rpf!=rgbf_end; rpf+=3) {
          int ntscri=rpf[0]*levelmult;
          int ntscgi=rpf[1]*levelmult;
          int ntscbi=rpf[2]*levelmult;
          if (ntscri>=ANALOGTV_CV_MAX) ntscri=ANALOGTV_CV_MAX-1;
          if (ntscgi>=ANALOGTV_CV_MAX) ntscgi=ANALOGTV_CV_MAX-1;
          if (ntscbi>=ANALOGTV_CV_MAX) ntscbi=ANALOGTV_CV_MAX-1;
          pix = (it->red_values[ntscri] |
                 it->green_values[ntscgi] |
                 it->blue_values[ntscbi] |
                 it->alpha_value);
          pixelptr[0] = pix;
          if (xrepl>=2) {
            pixelptr[1] = pix;
            if (xrepl>=3) pixelptr[2] = pix;
          }
          pixelptr+=xrepl;
        }
      }
    }
  }
}

static void analogtv_thread_draw_lines(void *thread_raw)
{
  const analogtv_thread *thread = (analogtv_thread *)thread_raw;
  const analogtv *it = thread->it;

  int lineno;

  float *raw_rgb_start;
  float *raw_rgb_end;
  raw_rgb_start=(float *)calloc(it->subwidth*3, sizeof(float));

  if (! raw_rgb_start) return;

  raw_rgb_end=raw_rgb_start+3*it->subwidth;

  for (lineno=ANALOGTV_TOP + thread->thread_id;
       lineno<ANALOGTV_BOT;
       lineno += it->threads.count) {
    int i;

    int slineno, ytop, ybot;
    unsigned signal_offset;

    const float *signal;

    int scanstart_i,scanend_i,squishright_i,squishdiv,pixrate;
    float *rgb_start, *rgb_end;
    float pixbright;
    int pixmultinc;

    float *rrp;

    struct analogtv_yiq_s yiq[ANALOGTV_PIC_LEN+10];

    if (! analogtv_get_line(it, lineno, &slineno, &ytop, &ybot,
        &signal_offset))
      continue;

    signal = it->rx_signal + signal_offset;

    {

      float bloomthisrow,shiftthisrow;
      float viswidth,middle;
      float scanwidth;
      int scw,scl,scr;

      bloomthisrow = -10.0f * it->crtload[lineno];
      if (bloomthisrow<-10.0f) bloomthisrow=-10.0f;
      if (bloomthisrow>2.0f) bloomthisrow=2.0f;
      if (slineno<16) {
        shiftthisrow=it->horiz_desync * (expf(-0.17f*slineno) *
                                         (0.7f+cosf(slineno*0.6f)));
      } else {
        shiftthisrow=0.0f;
      }

      viswidth=ANALOGTV_PIC_LEN * 0.79f - 5.0f*bloomthisrow;
      middle=ANALOGTV_PIC_LEN/2 - shiftthisrow;

      scanwidth=it->width_control * puramp(it, 0.5f, 0.3f, 1.0f);

      scw=it->subwidth*scanwidth;
      if (scw>it->subwidth) scw=it->usewidth;
      scl=it->subwidth/2 - scw/2;
      scr=it->subwidth/2 + scw/2;

      pixrate=(int)((viswidth*65536.0f*1.0f)/it->subwidth)/scanwidth;
      scanstart_i=(int)((middle-viswidth*0.5f)*65536.0f);
      scanend_i=(ANALOGTV_PIC_LEN-1)*65536;
      squishright_i=(int)((middle+viswidth*(0.25f + 0.25f*puramp(it, 2.0f, 0.0f, 1.1f)
                                            - it->squish_control)) *65536.0f);
      squishdiv=it->subwidth/15;

      rgb_start=raw_rgb_start+scl*3;
      rgb_end=raw_rgb_start+scr*3;

      assert(scanstart_i>=0);

#ifdef DEBUG
      if (0) printf("scan %d: %0.3f %0.3f %0.3f scl=%d scr=%d scw=%d\n",
                    lineno,
                    scanstart_i/65536.0f,
                    squishright_i/65536.0f,
                    scanend_i/65536.0f,
                    scl,scr,scw);
#endif
    }

    {
      analogtv_ntsc_to_yiq(it, lineno, signal,
                           (scanstart_i>>16)-10, (scanend_i>>16)+10, yiq);

      pixbright=it->contrast_control * puramp(it, 1.0f, 0.0f, 1.0f)
        / (0.5f+0.5f*it->puheight) * 1024.0f/100.0f;
      pixmultinc=pixrate;
      i=scanstart_i; rrp=rgb_start;
      while (i<0 && rrp!=rgb_end) {
        rrp[0]=rrp[1]=rrp[2]=0;
        i+=pixmultinc;
        rrp+=3;
      }
      while (i<scanend_i && rrp!=rgb_end) {
        float pixfrac=(i&0xffff)/65536.0f;
        float invpixfrac=1.0f-pixfrac;
        int pati=i>>16;
        float r,g,b;

        float interpy=(yiq[pati].y*invpixfrac + yiq[pati+1].y*pixfrac);
        float interpi=(yiq[pati].i*invpixfrac + yiq[pati+1].i*pixfrac);
        float interpq=(yiq[pati].q*invpixfrac + yiq[pati+1].q*pixfrac);

        /*
          According to the NTSC spec, Y,I,Q are generated as:

          y=0.30 r + 0.59 g + 0.11 b
          i=0.60 r - 0.28 g - 0.32 b
          q=0.21 r - 0.52 g + 0.31 b

          So if you invert the implied 3x3 matrix you get what standard
          televisions implement with a bunch of resistors (or directly in the
          CRT -- don't ask):

          r = y + 0.948 i + 0.624 q
          g = y - 0.276 i - 0.639 q
          b = y - 1.105 i + 1.729 q
        */

        r=(interpy + 0.948f*interpi + 0.624f*interpq) * pixbright;
        g=(interpy - 0.276f*interpi - 0.639f*interpq) * pixbright;
        b=(interpy - 1.105f*interpi + 1.729f*interpq) * pixbright;
        if (r<0.0f) r=0.0f;
        if (g<0.0f) g=0.0f;
        if (b<0.0f) b=0.0f;
        rrp[0]=r;
        rrp[1]=g;
        rrp[2]=b;

        if (i>=squishright_i) {
          pixmultinc += pixmultinc/squishdiv;
          pixbright += pixbright/squishdiv/2;
        }
        i+=pixmultinc;
        rrp+=3;
      }
      while (rrp != rgb_end) {
        rrp[0]=rrp[1]=rrp[2]=0.0f;
        rrp+=3;
      }

      analogtv_blast_imagerow(it, raw_rgb_start, raw_rgb_end,
                              ytop,ybot);
    }
  }

  free(raw_rgb_start);
}

void
analogtv_draw(analogtv *it, double noiselevel,
              const analogtv_reception *const *recs, unsigned rec_count)
{
  int i,lineno;
  int /*bigloadchange,*/drawcount;
  double baseload;

  it->rx_signal_level = noiselevel;
  for (i = 0; i != rec_count; ++i) {
    const analogtv_reception *rec = recs[i];
    double level = rec->level;
    analogtv_input *inp=rec->input;

    it->rx_signal_level =
      sqrt(it->rx_signal_level * it->rx_signal_level +
           (level * level * (1.0 + 4.0*(rec->ghostfir[0] + rec->ghostfir[1] +
                                        rec->ghostfir[2] + rec->ghostfir[3]))));

    /* duplicate the first line into the Nth line to ease wraparound computation */
    memcpy(inp->signal[ANALOGTV_V], inp->signal[0],
           ANALOGTV_H * sizeof(inp->signal[0][0]));
  }

  analogtv_setup_frame(it);

  it->random0 = random();
  it->random1 = random();
  it->noiselevel = noiselevel;
  it->recs = recs;
  it->rec_count = rec_count;
  threadpool_run(&it->threads, analogtv_thread_add_signals);
  threadpool_wait(&it->threads);

  it->channel_change_cycles=0;

  /* rx_signal has an extra 2 lines at the end, where we copy the
     first 2 lines so we can index into it while only worrying about
     wraparound on a per-line level */
  memcpy(&it->rx_signal[ANALOGTV_SIGNAL_LEN],
         &it->rx_signal[0],
         2*ANALOGTV_H*sizeof(it->rx_signal[0]));

  /* Repeat for signal_subtotals. */

  memcpy(&it->signal_subtotals[ANALOGTV_SIGNAL_LEN / ANALOGTV_SUBTOTAL_LEN],
         &it->signal_subtotals[0],
         (2*ANALOGTV_H/ANALOGTV_SUBTOTAL_LEN)*sizeof(it->signal_subtotals[0]));

  analogtv_sync(it); /* Requires the add_signals be complete. */

  baseload=0.5;
  /* if (it->hashnoise_on) baseload=0.5; */

  /*bigloadchange=1;*/
  drawcount=0;
  it->crtload[ANALOGTV_TOP-1]=baseload;
  it->puheight = puramp(it, 2.0, 1.0, 1.3) * it->height_control *
    (1.125 - 0.125*puramp(it, 2.0, 2.0, 1.1));

  analogtv_setup_levels(it, it->puheight * (double)it->useheight/(double)ANALOGTV_VISLINES);

  for (lineno=ANALOGTV_TOP; lineno<ANALOGTV_BOT; lineno++) {
    int slineno, ytop, ybot;
    unsigned signal_offset;
    if (! analogtv_get_line(it, lineno, &slineno, &ytop, &ybot, &signal_offset))
      continue;

    if (lineno==it->shrinkpulse) {
      baseload += 0.4;
      /*bigloadchange=1;*/
      it->shrinkpulse=-1;
    }

#if 0
    if (it->hashnoise_rpm>0.0 &&
        !(bigloadchange ||
          (slineno<20 && it->flutter_horiz_desync) ||
          it->gaussiannoise_level>30 ||
          ((it->gaussiannoise_level>2.0 ||
            it->multipath) && random()%4) ||
          linesig != it->onscreen_signature[lineno])) {
      continue;
    }
    it->onscreen_signature[lineno] = linesig;
#endif
    drawcount++;

    /*
      Interpolate the 600-dotclock line into however many horizontal
      screen pixels we're using, and convert to RGB.

      We add some 'bloom', variations in the horizontal scan width with
      the amount of brightness, extremely common on period TV sets. They
      had a single oscillator which generated both the horizontal scan and
      (during the horizontal retrace interval) the high voltage for the
      electron beam. More brightness meant more load on the oscillator,
      which caused an decrease in horizontal deflection. Look for
      (bloomthisrow).

      Also, the A2 did a bad job of generating horizontal sync pulses
      during the vertical blanking interval. This, and the fact that the
      horizontal frequency was a bit off meant that TVs usually went a bit
      out of sync during the vertical retrace, and the top of the screen
      would be bent a bit to the left or right. Look for (shiftthisrow).

      We also add a teeny bit of left overscan, just enough to be
      annoying, but you can still read the left column of text.

      We also simulate compression & brightening on the right side of the
      screen. Most TVs do this, but you don't notice because they overscan
      so it's off the right edge of the CRT. But the A2 video system used
      so much of the horizontal scan line that you had to crank the
      horizontal width down in order to not lose the right few characters,
      and you'd see the compression on the right edge. Associated with
      compression is brightening; since the electron beam was scanning
      slower, the same drive signal hit the phosphor harder. Look for
      (squishright_i) and (squishdiv).
    */

    {
      /* This used to be an int, I suspect by mistake. - Dave */
      float totsignal=0;
      float ncl/*,diff*/;
      unsigned frac;
      size_t end0, end1;
      const float *p;

      frac = signal_offset & (ANALOGTV_SUBTOTAL_LEN - 1);
      p = it->rx_signal + (signal_offset & ~(ANALOGTV_SUBTOTAL_LEN - 1));
      for (i=0; i != frac; i++) {
        totsignal -= p[i];
      }

      end0 = (signal_offset + ANALOGTV_PIC_LEN);

      end1 = end0 / ANALOGTV_SUBTOTAL_LEN;
      for (i=signal_offset / ANALOGTV_SUBTOTAL_LEN; i<end1; i++) {
        totsignal += it->signal_subtotals[i];
      }

      frac = end0 & (ANALOGTV_SUBTOTAL_LEN - 1);
      p = it->rx_signal + (end0 & ~(ANALOGTV_SUBTOTAL_LEN - 1));
      for (i=0; i != frac; i++) {
        totsignal += p[i];
      }

      totsignal *= it->agclevel;
      ncl = 0.95f * it->crtload[lineno-1] +
        0.05f*(baseload +
               (totsignal-30000)/100000.0f +
               (slineno>184 ? (slineno-184)*(lineno-184)*0.001f * it->squeezebottom
                : 0.0f));
      /*diff=ncl - it->crtload[lineno];*/
      /*bigloadchange = (diff>0.01 || diff<-0.01);*/
      it->crtload[lineno]=ncl;
    }
  }

  threadpool_run(&it->threads, analogtv_thread_draw_lines);
  threadpool_wait(&it->threads);

#if 0
  /* poor attempt at visible retrace */
  for (i=0; i<15; i++) {
    int ytop=(int)((i*it->useheight/15 -
                    it->useheight/2)*puheight) + it->useheight/2;
    int ybot=(int)(((i+1)*it->useheight/15 -
                    it->useheight/2)*puheight) + it->useheight/2;
    int div=it->usewidth*3/2;

    for (x=0; x<it->usewidth; x++) {
      y = ytop + (ybot-ytop)*x / div;
      if (y<0 || y>=it->useheight) continue;
      XPutPixel(it->image, x, y, 0xffffff);
    }
  }
#endif

#ifdef DEBUG
  if (0) {
    struct timeval tv;
    double fps;
    char buf[256];
    gettimeofday(&tv,NULL);

    fps=1.0/((tv.tv_sec - it->last_display_time.tv_sec)
             + 0.000001*(tv.tv_usec - it->last_display_time.tv_usec));
    sprintf(buf, "FPS=%0.1f",fps);
    XDrawString(it->dpy, it->window, it->gc, 50, it->useheight*2/3,
                buf, strlen(buf));

    it->last_display_time=tv;
  }
#endif
}

analogtv_input *
analogtv_input_allocate()
{
  analogtv_input *ret=(analogtv_input *)calloc(1,sizeof(analogtv_input));

  return ret;
}


#ifdef FIXME
/* add hash */
  if (it->hashnoise_times[lineno]) {
    int hnt=it->hashnoise_times[lineno] - input->line_hsync[lineno];

    if (hnt>=0 && hnt<ANALOGTV_PIC_LEN) {
      double maxampl=1.0;
      double cur=frand(150.0)-20.0;
      int len=random()%15+3;
      if (len > ANALOGTV_PIC_LEN-hnt) len=ANALOGTV_PIC_LEN-hnt;
      for (i=0; i<len; i++) {
        double sig=signal[hnt];

        sig += cur*maxampl;
        cur += frand(5.0)-5.0;
        maxampl = maxampl*0.9;

        signal[hnt]=sig;
        hnt++;
      }
    }
  }
#endif


void
analogtv_reception_update(analogtv_reception *rec)
{
  int i;

  if (rec->multipath > 0.0) {
    for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
      rec->ghostfir2[i] +=
        -(rec->ghostfir2[i]/16.0) + rec->multipath * (frand(0.02)-0.01);
    }
    if (random()%20==0) {
      rec->ghostfir2[random()%(ANALOGTV_GHOSTFIR_LEN)]
        = rec->multipath * (frand(0.08)-0.04);
    }
    for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
      rec->ghostfir[i] = 0.8*rec->ghostfir[i] + 0.2*rec->ghostfir2[i];
    }

    if (0) {
      rec->hfloss2 += -(rec->hfloss2/16.0) + rec->multipath * (frand(0.08)-0.04);
      rec->hfloss = 0.5*rec->hfloss + 0.5*rec->hfloss2;
    }

  } else {
    for (i=0; i<ANALOGTV_GHOSTFIR_LEN; i++) {
      rec->ghostfir[i] = (i>=ANALOGTV_GHOSTFIR_LEN/2) ? ((i&1) ? +0.04 : -0.08) /ANALOGTV_GHOSTFIR_LEN
        : 0.0;
    }
  }
}

