# shell v2 - launcher model + shell UI on libbrainui2.
# M1: reg/desktop/launcher/iconcache/shellui.
# M2: WM protocol+server+client lib, backends, assist, greeter2.
# Builds against the toolkit in the parent directory; run
# `make -C .. libbrainui2.a` first (the `check` target here does it).
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS += -Iinclude -Igreeter -I../include -I../osk -I../libbrainime/include -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

SRC = src/reg.c src/desktop.c src/launcher.c src/shellui.c src/iconcache.c \
      src/wm.c src/wmclient.c src/backend.c src/assist.c src/kbdsettings.c \
      src/wifi.c src/session.c src/mascot.c src/notify.c src/thememode.c
OBJ = $(SRC:.c=.o)
LIB = libbdshell.a
TKLIB = ../libbrainui2.a

# libbrainui2.a (M24+) references HarfBuzz/FreeType symbols, so
# consumers linking it need these too (same pattern as the root
# Makefile). Prefer pkg-config; fall back to plain -l flags.
TEXT_LIBS := $(shell pkg-config --libs harfbuzz freetype2 2>/dev/null)
ifeq ($(strip $(TEXT_LIBS)),)
TEXT_LIBS = -lharfbuzz -lfreetype
endif

TESTS = test_desktop test_shell test_iconcache test_wm test_backend test_assist test_greeter_auth test_greeter_font test_wifi test_session test_mascot test_notify
HELPERS = fake_wpa session_client im_client
CLIENTS = wm_sample_raw wm_sample_tk
GREETER = braingreeter2
SESSION = braindesk2

all: $(LIB) tests $(CLIENTS) $(GREETER) $(SESSION)

$(LIB): $(OBJ)
	$(AR) rcs $@ $(OBJ)

%.o: %.c include/reg.h include/desktop.h include/launcher.h include/shellui.h include/iconcache.h include/wm.h include/backend.h include/assist.h include/kbdsettings.h include/wifi.h include/session.h include/mascot.h include/notify.h ../include/tk.h ../include/tk_kbd.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TKLIB):
	$(MAKE) -C .. libbrainui2.a

IMELIB = ../libbrainime/libbrainime.a
$(IMELIB):
	$(MAKE) -C ../libbrainime libbrainime.a

tests: $(TESTS) $(HELPERS)

test_desktop: tests/test_desktop.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell -lm

test_shell: tests/test_shell.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_iconcache: tests/test_iconcache.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_wm: tests/test_wm.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_backend: tests/test_backend.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell -lm

test_assist: tests/test_assist.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_greeter_auth: greeter/test_greeter_auth.c greeter/greeter2_auth.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< greeter/greeter2_auth.c -o $@ -lcrypt

test_greeter_font: tests/test_greeter_font.c greeter/greeter2_auth.c src/thememode.c src/reg.c $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< greeter/greeter2_auth.c src/thememode.c src/reg.c -o $@ ../libbrainui2.a $(TEXT_LIBS) -lm -lcrypt

fake_wpa: tests/fake_wpa.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

session_client: tests/session_client.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

im_client: tests/im_client.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_wifi: tests/test_wifi.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell -lm

test_session: tests/test_session.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_mascot: tests/test_mascot.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

test_notify: tests/test_notify.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

bench_mascot: tests/bench_mascot.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

bench_catalog_mem: tests/bench_catalog_mem.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell -lm

wm_sample_raw: clients/wm_sample_raw.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

wm_sample_tk: clients/wm_sample_tk.c $(LIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) -lm

braingreeter2: greeter/braingreeter2.c greeter/greeter2_auth.c src/thememode.c src/reg.c $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< greeter/greeter2_auth.c src/thememode.c src/reg.c -o $@ ../libbrainui2.a $(TEXT_LIBS) -lm -lcrypt

braindesk2: braindesk2.c $(LIB) $(TKLIB) $(IMELIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< ../osk/osk_brainime.c -o $@ -L. -lbdshell ../libbrainui2.a $(TEXT_LIBS) $(IMELIB) -lm

check: tests $(CLIENTS) $(GREETER) $(SESSION)
	./test_desktop
	./test_shell
	./test_iconcache
	./test_wm
	./test_backend
	./test_assist
	./test_greeter_auth
	./test_greeter_font
	./test_wifi
	./test_session
	./test_mascot
	./test_notify

clean:
	rm -f $(OBJ) $(LIB) $(TESTS) $(HELPERS) bench_catalog_mem $(CLIENTS) $(GREETER) $(SESSION)

.PHONY: all tests check clean
