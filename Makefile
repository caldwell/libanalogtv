# Copyright © 2015 David Caldwell <david@porkrind.org>
#
# Permission to use, copy, modify, distribute, and sell this software and its
# documentation for any purpose is hereby granted without fee, provided that
# the above copyright notice appear in all copies and that both that
# copyright notice and this permission notice appear in supporting
# documentation.  No representations are made about the suitability of this
# software for any purpose.  It is provided "as is" without express or
# implied warranty.
#
# `make V=1` to enable verbose mode

Q=$(if $V,,@)
_CC:=$(CC)
%.o: override CC=$(Q)printf "%15s  %s\n" "Compiling" $<; $(_CC)
_CXX:=$(CXX)
%.o: override CXX=$(Q)printf "%15s  %s\n" "Compiling" $<; $(_CXX)
_FC:=$(FC)
%.o: override FC=$(Q)printf "%15s  %s\n" "Compiling" $<; $(_FC)
_AS:=$(AS)
%.o: override AS=$(Q)printf "%15s  %s\n" "Assembling" $<; $(_AS)
_AR:=$(AR)
%.a: override AR=$(Q)printf "%15s  %s\n" "Archiving" $@; $(_AR)

all: analogtv-test

CPPFLAGS   += $(if $(PKGCONFIG), $$(pkg-config $(PKGCONFIG) --cflags))
LDLIBS     += $(if $(PKGCONFIG), $$(pkg-config $(PKGCONFIG) --libs-only-l))
LDFLAGS    += $(if $(PKGCONFIG), $$(pkg-config $(PKGCONFIG) --libs-only-L))

CFLAGS += -Wno-parentheses

CFLAGS += -MMD
-include *.d

ANALOGTV_TEST_OBJS = analogtv-test.o thread.o analogtv.o analogtv-apple2.o
analogtv-test: $(ANALOGTV_TEST_OBJS)
analogtv-test $(ANALOGTV_TEST_OBJS): PKGCONFIG+=gdlib

out-%.png: analogtv-test
	./analogtv-test $(strip $(if $(findstring graphics,$*),1,0) \
	                        $(if $(findstring hires,$*),1,0)    \
	                        $(if $(findstring mixed,$*),1,0)    \
	                        $(if $(findstring page,$*),1,0)     \
	                        $@)

all: out-text.png
all: out-text-page2.png
all: out-graphics-lores.png
all: out-graphics-lores-page2.png
all: out-graphics-lores-mixed.png
all: out-graphics-lores-mixed-page2.png
all: out-graphics-hires.png
all: out-graphics-hires-page2.png
all: out-graphics-hires-mixed.png
all: out-graphics-hires-mixed-page2.png

clean:
	rm -f *.o *.d analog-test
