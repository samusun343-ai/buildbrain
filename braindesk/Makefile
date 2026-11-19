CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
SRC = src/bui.c src/bui_wm.c src/scenes.c src/bui_fb.c src/bui_input.c src/bui_alsa.c src/bui_osk.c src/bui_skin.c src/brainreg.c

APPS = bd-files bd-tasks bd-notes bd-clock bd-calc bd-player bd-recorder bd-devices bd-pkg bd-web bd-deck bd-regedit
APPSRC = src/bui.c src/bui_wm.c src/bui_fb.c src/bui_input.c src/bui_alsa.c src/bui_osk.c src/bui_skin.c src/brainreg.c

# QuickJS (vendored, third_party/quickjs) — linked into bd-web only (M3).
QJS_SRC = third_party/quickjs/dtoa.c third_party/quickjs/libregexp.c third_party/quickjs/libunicode.c third_party/quickjs/quickjs.c
QJS_OBJ = $(QJS_SRC:.c=.o)

third_party/quickjs/%.o: third_party/quickjs/%.c
	$(CC) -O2 -c $< -o $@

all: demo shell test_events greeter apps

apps: $(APPS) brainctl

brainctl: src/apps/brainctl.c src/brainreg.c src/brainreg.h
	$(CC) $(CFLAGS) -o $@ src/brainreg.c src/apps/brainctl.c

bd-%: src/apps/bd-%.c src/apps/bdapp.h $(APPSRC)
	$(CC) $(CFLAGS) -o $@ $(APPSRC) $< -lm

# bd-web overrides the generic rule: QuickJS engine + JS wrapper.
bd-web: src/apps/bd-web.c src/apps/bdapp.h src/apps/bd_js.c src/apps/bd_js.h $(APPSRC) $(QJS_OBJ)
	$(CC) $(CFLAGS) -Ithird_party/quickjs -o $@ $(APPSRC) src/apps/bd-web.c src/apps/bd_js.c $(QJS_OBJ) -lm -lpthread -ldl -latomic
# -latomic: QuickJS uses 64-bit atomic builtins; ARMv5 has no native
# 64-bit atomics, so GCC emits __atomic_*_8 libcalls (CI failed the
# bd-web link without it; the Buildroot cross toolchain masks this).

greeter: $(SRC) src/greeter.c
	$(CC) $(CFLAGS) -o $@ $(SRC) src/greeter.c -lm -lcrypt

demo: $(SRC) src/demo.c
	$(CC) $(CFLAGS) -o $@ $(SRC) src/demo.c -lm

shell: $(SRC) src/shell.c
	$(CC) $(CFLAGS) -o $@ $(SRC) src/shell.c -lm

test_events: $(SRC) src/test_events.c
	$(CC) $(CFLAGS) -o $@ $(SRC) src/test_events.c -lm

# Device build (static, ARM): make arm
arm:
	arm-buildroot-linux-gnueabi-gcc -O2 -static -Wall -Wextra -Wno-unused-parameter -o braindesk-shell-arm $(SRC) src/shell.c -lm

clean:
	rm -f demo shell test_events greeter $(APPS) $(QJS_OBJ) *.ppm

.PHONY: all arm clean
