# BrainCE Layer (WinCE compatibility layer) -- M1 build.
#
# ce-loader + coredll shim: one statically linked ARM Linux binary
# built with the project's Bootlin armv5-eabi toolchain (static so
# the same artifact runs under qemu-arm on the dev host and on the
# Brainux guest regardless of the guest libc version).
# cetest.exe: WinCE PE built by test/build-test.sh (clang + pe-link47).

TC      ?= $(HOME)/workspace/buildbrain-work/toolchain/armv5-eabi--glibc--stable-2024.05-1/bin/arm-buildroot-linux-gnueabi
ARMCC   := $(TC)-gcc
CFLAGS  ?= -march=armv5te -mtune=arm926ej-s -Os -Wall -Wextra -std=gnu11
LDFLAGS ?= -static

OBJS = loader/ce-loader.o shim/coredll_shim.o shim/fs_bridge.o shim/stub_pool.o

all: ce-loader test/cetest.exe

ce-loader: $(OBJS)
	$(ARMCC) $(LDFLAGS) -o $@ $(OBJS)

loader/ce-loader.o: loader/ce-loader.c loader/pe.h shim/ce_types.h shim/fs_bridge.h shim/shim_table.h
	$(ARMCC) $(CFLAGS) -c -o $@ $<

shim/coredll_shim.o: shim/coredll_shim.c shim/ce_types.h shim/fs_bridge.h shim/shim_table.h
	$(ARMCC) $(CFLAGS) -c -o $@ $<

shim/fs_bridge.o: shim/fs_bridge.c shim/ce_types.h shim/fs_bridge.h
	$(ARMCC) $(CFLAGS) -c -o $@ $<

shim/stub_pool.o: shim/stub_pool.S
	$(ARMCC) $(CFLAGS) -c -o $@ $<

shim/shim_table.h: shim/implemented.txt tools/gen_shim_table.py
	python3 tools/gen_shim_table.py

test/cetest.exe: test/cetest.c test/build-test.sh
	python3 tools/make_coredll_exports.py
	sh test/build-test.sh

host-test: all
	sh tools/run-host.sh

clean:
	rm -f $(OBJS) ce-loader test/cetest.o test/cetest.exe \
	    test/cetest.exe.imports.json test/cetest.exe.map.json

.PHONY: all host-test clean
