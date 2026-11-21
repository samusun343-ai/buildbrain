# OSK v2 (input integration) - tests and bench.
# The widget itself (src/tk_osk.c) is part of libbrainui2.a in the
# parent tree; this directory holds the libbrainime adapter, the
# test suite and the perf bench.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
ROOT     = ..
CPPFLAGS += -I$(ROOT)/include -I$(ROOT)/libbrainime/include -I. \
            -D_POSIX_C_SOURCE=200809L

DICT ?= ../../ime-dict/brainux2.bidf
BISC ?= $(ROOT)/libbrainime/data/brainux2.bisc

# libbrainui2.a (M24+) references HarfBuzz/FreeType symbols, so
# consumers linking it need these too (same pattern as the root
# Makefile). Prefer pkg-config; fall back to plain -l flags.
TEXT_LIBS := $(shell pkg-config --libs harfbuzz freetype2 2>/dev/null)
ifeq ($(strip $(TEXT_LIBS)),)
TEXT_LIBS = -lharfbuzz -lfreetype
endif

LIBS = $(ROOT)/libbrainui2.a $(TEXT_LIBS) $(ROOT)/libbrainime/libbrainime.a -lm

all: test_osk bench_osk

$(ROOT)/libbrainui2.a:
	$(MAKE) -C $(ROOT) libbrainui2.a

$(ROOT)/libbrainime/libbrainime.a:
	$(MAKE) -C $(ROOT)/libbrainime libbrainime.a

test_osk: tests/test_osk.c osk_brainime.c $(LIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_osk.c osk_brainime.c \
	    -o $@ $(LIBS)

bench_osk: bench_osk.c osk_brainime.c $(LIBS)
	$(CC) $(CPPFLAGS) $(CFLAGS) bench_osk.c osk_brainime.c \
	    -o $@ $(LIBS)

check: test_osk
	BI_SIDECAR=$(abspath $(BISC)) ./test_osk $(DICT) $(BISC)

bench: bench_osk
	BI_SIDECAR=$(abspath $(BISC)) ./bench_osk $(DICT) $(BISC)

clean:
	rm -f test_osk bench_osk

.PHONY: all check bench clean
