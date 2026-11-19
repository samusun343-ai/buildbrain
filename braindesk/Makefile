CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
SRC = src/bui.c src/scenes.c src/bui_fb.c src/bui_input.c src/bui_alsa.c src/bui_osk.c src/bui_skin.c

APPS = bd-files bd-tasks bd-notes bd-clock bd-calc bd-player bd-recorder bd-devices bd-pkg
APPSRC = src/bui.c src/bui_fb.c src/bui_input.c src/bui_alsa.c src/bui_osk.c src/bui_skin.c

all: demo shell test_events greeter apps

apps: $(APPS)

bd-%: src/apps/bd-%.c src/apps/bdapp.h $(APPSRC)
	$(CC) $(CFLAGS) -o $@ $(APPSRC) $< -lm

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
	rm -f demo shell test_events greeter $(APPS) *.ppm

.PHONY: all arm clean
