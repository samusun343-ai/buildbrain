CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
SRC = src/bui.c src/scenes.c src/bui_fb.c src/bui_input.c

all: demo shell test_events

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
	rm -f demo shell test_events *.ppm

.PHONY: all arm clean
