# libbrainui2 (rebuild) - M1 foundation + M2 widgets/fonts + M3
# (damage-based presentation, shared library + SDK, bundled font)
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
AR      ?= ar

# M24: text shaping (HarfBuzz) + font backend (FreeType), linked
# directly by tk_font.c. Prefer pkg-config; fall back to the
# Debian include layout when it is unavailable.
TEXT_CFLAGS := $(shell pkg-config --cflags harfbuzz freetype2 2>/dev/null)
TEXT_LIBS := $(shell pkg-config --libs harfbuzz freetype2 2>/dev/null)
ifeq ($(strip $(TEXT_LIBS)),)
TEXT_CFLAGS = -I/usr/include/harfbuzz -I/usr/include/freetype2
TEXT_LIBS = -lharfbuzz -lfreetype
endif
CPPFLAGS += $(TEXT_CFLAGS)

SRC = src/textcore.c src/tk_core.c src/tk_draw.c src/tk_widgets.c \
      src/tk_font.c src/tk_image.c src/tk_fb.c src/tk_evdev.c \
      src/tk_kbd.c src/tk_osk.c src/tk_anim.c src/tk_m7.c src/tk_jpeg.c
OBJ = $(SRC:.c=.o)
PICOBJ = $(SRC:.c=.pic.o)

LIB = libbrainui2.a
SHLIB = libbrainui2.so.1.0
SONAME = libbrainui2.so.1

TESTS = test_textcore test_tk test_tk2 test_font test_shape test_kbd test_tk3 test_tk4 test_tk5 test_tk6 test_tk7 test_tk8 test_tk9 test_tk10 test_tk11 test_tk12 test_tk13 test_tk14 test_tk15 test_tk16 test_tk17 test_tk18

PREFIX ?= /usr/local

all: $(LIB) $(SHLIB) tests

$(LIB): $(OBJ)
	$(AR) rcs $@ $(OBJ)

$(SHLIB): $(PICOBJ) libbrainui2.map
	$(CC) -shared -Wl,-soname,$(SONAME) -Wl,--version-script=libbrainui2.map -o $@ $(PICOBJ) $(TEXT_LIBS) -lm
	ln -sf $(SHLIB) $(SONAME)
	ln -sf $(SHLIB) libbrainui2.so

%.o: %.c include/tk.h include/textcore.h src/tk_internal.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.pic.o: %.c include/tk.h include/textcore.h src/tk_internal.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

tests: $(TESTS)

test_textcore: tests/test_textcore.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk: tests/test_tk.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk2: tests/test_tk2.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_font: tests/test_font.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_shape: tests/test_shape.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_kbd: tests/test_kbd.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk3: tests/test_tk3.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk4: tests/test_tk4.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk5: tests/test_tk5.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk6: tests/test_tk6.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk7: tests/test_tk7.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk8: tests/test_tk8.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk9: tests/test_tk9.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk10: tests/test_tk10.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk11: tests/test_tk11.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk12: tests/test_tk12.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk14: tests/test_tk14.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk15: tests/test_tk15.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk16: tests/test_tk16.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk17: tests/test_tk17.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk18: tests/test_tk18.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

test_tk13: tests/test_tk13.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

check: tests
	./test_textcore
	./test_tk
	./test_tk2
	./test_font
	./test_shape
	./test_kbd
	./test_tk3
	./test_tk4
	./test_tk5
	./test_tk6
	./test_tk7
	./test_tk8
	./test_tk9
	./test_tk10
	./test_tk11
	./test_tk12
	./test_tk13
	./test_tk14
	./test_tk15
	./test_tk16
	./test_tk17
	./test_tk18
	$(MAKE) sdk-check

# M3 quantitative evaluation (prints numbers, not pass/fail)
bench_damage: tests/bench_damage.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

bench: bench_damage
	./bench_damage

# M5 paint/flush hot-path evaluation (prints numbers)
bench_paint: tests/bench_paint.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTK_BENCH_BLIT $< -o $@ ./$(LIB) -lm $(TEXT_LIBS)

bench-paint: bench_paint
	./bench_paint

libbrainui2.pc: Makefile
	@{ echo 'prefix=$(PREFIX)'; \
	   echo 'exec_prefix=$${prefix}'; \
	   echo 'libdir=$${exec_prefix}/lib'; \
	   echo 'includedir=$${prefix}/include'; \
	   echo ''; \
	   echo 'Name: libbrainui2'; \
	   echo 'Description: Brainux GUI toolkit v2 (widgets, textcore, fbdev)'; \
	   echo 'Version: 1.0'; \
	   echo 'Libs: -L$${libdir} -lbrainui2'; \
	   echo 'Libs.private: -lm -lharfbuzz -lfreetype'; \
	   echo 'Requires.private: harfbuzz freetype2'; \
	   echo 'Cflags: -I$${includedir}'; \
	 } > $@

install: $(LIB) $(SHLIB) libbrainui2.pc
	mkdir -p $(DESTDIR)$(PREFIX)/include \
	         $(DESTDIR)$(PREFIX)/lib/pkgconfig \
	         $(DESTDIR)$(PREFIX)/share/fonts/libbrainui2
	cp include/tk.h include/textcore.h include/tk_kbd.h include/osk.h \
	   $(DESTDIR)$(PREFIX)/include/
	cp $(LIB) $(DESTDIR)$(PREFIX)/lib/
	cp $(SHLIB) $(DESTDIR)$(PREFIX)/lib/
	ln -sf $(SHLIB) $(DESTDIR)$(PREFIX)/lib/$(SONAME)
	ln -sf $(SHLIB) $(DESTDIR)$(PREFIX)/lib/libbrainui2.so
	cp libbrainui2.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/
	cp fonts/brainui-jp-subset.ttf fonts/brainui-jp-supplement.ttf \
	   fonts/brainui-jp-symbols.ttf fonts/LICENSE.brainui-jp.txt \
	   fonts/NOTICE.supplement.txt \
	   $(DESTDIR)$(PREFIX)/share/fonts/libbrainui2/

# SDK verification (M3): install into a staging dir, then build and
# run the sample app against ONLY the installed headers + shared
# library via pkg-config - the "third-party" path a real app uses.
sdk-check: examples/sdk_settings.c
	rm -rf .sdk-stage
	$(MAKE) install DESTDIR=$(CURDIR)/.sdk-stage
	$(CC) -std=c11 -Wall -Wextra -O2 \
	    `PKG_CONFIG_PATH=$(CURDIR)/.sdk-stage$(PREFIX)/lib/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(CURDIR)/.sdk-stage pkg-config --cflags libbrainui2` \
	    examples/sdk_settings.c -o .sdk-stage/sdk_settings \
	    `PKG_CONFIG_PATH=$(CURDIR)/.sdk-stage$(PREFIX)/lib/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(CURDIR)/.sdk-stage pkg-config --libs libbrainui2` \
	    -Wl,-rpath,$(CURDIR)/.sdk-stage$(PREFIX)/lib
	cd .sdk-stage && ./sdk_settings
	rm -rf .sdk-stage

clean:
	rm -f bench_damage
	rm -f $(OBJ) $(PICOBJ) $(LIB) $(SHLIB) $(SONAME) libbrainui2.so \
	      $(TESTS) libbrainui2.pc
	rm -rf .sdk-stage

.PHONY: all tests check install sdk-check clean
