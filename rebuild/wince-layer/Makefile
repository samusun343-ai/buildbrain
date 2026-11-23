# BrainCE Layer (WinCE compatibility layer) -- M1 + M2 build.
#
# ce-loader + coredll/GWE shim: one dynamically linked ARM Linux
# binary. M1 was static; M2 links libbrainui2 (font/canvas) and the
# braindesk WM client, so the loader is built exactly like the rest
# of the Brainux userland (image-work/build-userland2.sh): the
# armbin2 wrapper around the Bootlin GCC 13.3 with the Debian armel
# sysroot, PKG_CONFIG pointed at that sysroot. The resulting binary
# runs under qemu-arm -L <sysroot> on the dev host and directly on
# the Brainux guest (same library set the bd-* apps use).
# cetest.exe / ceguitest.exe: WinCE PEs built by test/build-test.sh
# and test/build-guitest.sh (clang + pe-link47).

BW      := $(HOME)/workspace/buildbrain-work
ARMBIN  := $(BW)/image-work/armbin2
SYSROOT := $(BW)/rebuild/browser-engine/.sysroot-arm
ARMCC   := $(ARMBIN)/arm-linux-gcc
export PATH := $(ARMBIN):$(PATH)
export PKG_CONFIG_LIBDIR := $(SYSROOT)/usr/lib/arm-linux-gnueabi/pkgconfig:$(SYSROOT)/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR := $(SYSROOT)

# NB: pkg-config is invoked with the sysroot environment inline --
# GNU make does not pass makefile-exported variables to $(shell).
PKG := PKG_CONFIG_LIBDIR=$(SYSROOT)/usr/lib/arm-linux-gnueabi/pkgconfig:$(SYSROOT)/usr/share/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) pkg-config
CFLAGS  ?= -march=armv5te -mtune=arm926ej-s -Os -Wall -Wextra -std=gnu11
CPPFLAGS += -I../include -I../shell/include -D_GNU_SOURCE \
            $(shell $(PKG) --cflags harfbuzz freetype2 2>/dev/null)
TEXT_LIBS := $(shell $(PKG) --libs harfbuzz freetype2 2>/dev/null)
ifeq ($(strip $(TEXT_LIBS)),)
TEXT_LIBS = -lharfbuzz -lfreetype
endif
TKLIB   := $(BW)/rebuild-arm2/libbrainui2.a

OBJS = loader/ce-loader.o shim/coredll_shim.o shim/fs_bridge.o \
       shim/stub_pool.o shim/gwe.o shim/gdi.o shim/wmclient.o \
       shim/shim_table_glue.o

all: ce-loader test/cetest.exe test/ceguitest.exe

ce-loader: $(OBJS)
	$(ARMCC) -o $@ $(OBJS) $(TKLIB) $(TEXT_LIBS) -lm -lpthread

loader/ce-loader.o: loader/ce-loader.c loader/pe.h shim/ce_types.h shim/fs_bridge.h shim/shim_table.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/coredll_shim.o: shim/coredll_shim.c shim/ce_types.h shim/fs_bridge.h shim/shim_table.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/fs_bridge.o: shim/fs_bridge.c shim/ce_types.h shim/fs_bridge.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/gwe.o: shim/gwe.c shim/ce_types.h shim/fs_bridge.h shim/gwe.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/gdi.o: shim/gdi.c shim/ce_types.h shim/fs_bridge.h shim/gwe.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/shim_table_glue.o: shim/shim_table_glue.c shim/shim_fns.h shim/shim_fns_table.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/wmclient.o: ../shell/src/wmclient.c ../shell/include/wm.h
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/stub_pool.o: shim/stub_pool.S
	$(ARMCC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

shim/shim_table.h: shim/implemented.txt tools/gen_shim_table.py
	python3 tools/gen_shim_table.py

test/cetest.exe: test/cetest.c test/build-test.sh
	python3 tools/make_coredll_exports.py
	sh test/build-test.sh

test/ceguitest.exe: test/ceguitest.c test/build-guitest.sh
	python3 tools/make_coredll_exports.py
	sh test/build-guitest.sh

host-test: all
	sh tools/run-host.sh
	sh tools/run-host-gui.sh

clean:
	rm -f $(OBJS) ce-loader test/cetest.o test/cetest.exe \
	    test/cetest.exe.imports.json test/cetest.exe.map.json \
	    test/ceguitest.o test/ceguitest.exe \
	    test/ceguitest.exe.imports.json test/ceguitest.exe.map.json

.PHONY: all host-test clean
