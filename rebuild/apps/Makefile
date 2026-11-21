# apps wave 1 - bd-files / bd-notes / bd-tasks on libbrainui2 (toolkit v2).
#
# Everything here builds ONLY against the toolkit's public headers
# (../include), the shell's public headers (../shell/include) and
# libbrainime's public header - the SDK discipline the toolkit M3
# sdk-check established. No src/ internals are referenced.
#
# Layout per app: <app>/<x>model.c (pure model, no toolkit) +
# <app>/bd_<app>.c (UI; its main() is compiled only into the app
# binary via -DBD_<APP>_MAIN, so tests can link the same file) +
# <app>/tests/test_<app>.c (model tests) and, where present,
# <app>/tests/test_<app>_ui.c (headless UI drive tests).
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
ROOT     = ..
CPPFLAGS += -I$(ROOT)/include -I$(ROOT)/shell/include \
            -I$(ROOT)/libbrainime/include -I$(ROOT)/osk -Icommon \
            -Ifiles -Inotes -Itasks \
            -Iclock -Icalc -Iplayer -Irecorder -Ipkg -Isettings -Iadmin \
            -Ialarmd \
            -I. -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

TKLIB    = $(ROOT)/libbrainui2.a
SHELLIB  = $(ROOT)/shell/libbdshell.a
IMELIB   = $(ROOT)/libbrainime/libbrainime.a

# libbrainui2.a (M24+) references HarfBuzz/FreeType symbols, so
# consumers linking it need these too (same pattern as the root
# Makefile). Prefer pkg-config; fall back to plain -l flags.
TEXT_LIBS := $(shell pkg-config --libs harfbuzz freetype2 2>/dev/null)
ifeq ($(strip $(TEXT_LIBS)),)
TEXT_LIBS = -lharfbuzz -lfreetype
endif

COMMON   = common/bdstore.c
FMODEL   = files/fmodel.c common/bdmime.c
NMODEL   = notes/nmodel.c
TMODEL   = tasks/tmodel.c
CKMODEL  = clock/clockmodel.c
KCMODEL  = calc/calcmodel.c
PLMODEL  = player/playmodel.c player/pengine.c
RCMODEL  = recorder/recmodel.c
PKMODEL  = pkg/pkgmodel.c
SYMODEL  = admin/sysmodel.c

APPS     = bd-files bd-notes bd-tasks bd-clock bd-calc bd-recorder \
           bd-player bd-pkg bd-settings bd-admin bd-alarmd
TESTS    = test_files test_files_ui test_notes test_tasks test_tasks_ui \
           test_clock test_clock_ui test_calc test_calc_ui \
           test_recorder test_recorder_ui test_player test_player_ui \
           test_pkg test_pkg_ui test_settings test_settings_ui \
           test_admin test_admin_ui test_alarmd

all: $(APPS) tests

$(TKLIB):
	$(MAKE) -C $(ROOT) libbrainui2.a

$(SHELLIB):
	$(MAKE) -C $(ROOT)/shell libbdshell.a

$(IMELIB):
	$(MAKE) -C $(ROOT)/libbrainime libbrainime.a

# ---------------- bd-files ----------------
bd-files: files/bd_files.c $(FMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_FILES_MAIN files/bd_files.c \
	    $(FMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_files: files/tests/test_files.c $(FMODEL) $(COMMON) $(SHELLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) files/tests/test_files.c \
	    $(FMODEL) $(COMMON) -o $@ $(SHELLIB) -lm

test_files_ui: files/tests/test_files_ui.c files/bd_files.c \
               $(FMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) files/tests/test_files_ui.c \
	    files/bd_files.c $(FMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

# ---------------- bd-notes ----------------
bd-notes: notes/bd_notes.c $(NMODEL) $(COMMON) $(SHELLIB) $(TKLIB) $(IMELIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_NOTES_MAIN notes/bd_notes.c \
	    $(NMODEL) $(COMMON) $(ROOT)/osk/osk_brainime.c \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) $(IMELIB) -lm

test_notes: notes/tests/test_notes.c notes/bd_notes.c \
            $(NMODEL) $(COMMON) $(SHELLIB) $(TKLIB) $(IMELIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) notes/tests/test_notes.c \
	    notes/bd_notes.c $(NMODEL) $(COMMON) \
	    $(ROOT)/osk/osk_brainime.c \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) $(IMELIB) -lm

# ---------------- bd-tasks ----------------
bd-tasks: tasks/bd_tasks.c $(TMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_TASKS_MAIN tasks/bd_tasks.c \
	    $(TMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_tasks: tasks/tests/test_tasks.c $(TMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) tasks/tests/test_tasks.c \
	    $(TMODEL) $(COMMON) -o $@ -lm

test_tasks_ui: tasks/tests/test_tasks_ui.c tasks/bd_tasks.c \
               $(TMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) tasks/tests/test_tasks_ui.c \
	    tasks/bd_tasks.c $(TMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

# ---------------- bd-clock ----------------
bd-clock: clock/bd_clock.c $(CKMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_CLOCK_MAIN clock/bd_clock.c \
	    $(CKMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_clock: clock/tests/test_clock.c $(CKMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) clock/tests/test_clock.c \
	    $(CKMODEL) $(COMMON) -o $@ -lm

test_clock_ui: clock/tests/test_clock_ui.c clock/bd_clock.c \
               $(CKMODEL) $(COMMON) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) clock/tests/test_clock_ui.c \
	    clock/bd_clock.c $(CKMODEL) $(COMMON) \
	    -o $@ $(TKLIB) $(TEXT_LIBS) -lm

# ---------------- bd-alarmd ----------------
ALARMDCORE = alarmd/alarmd.c alarmd/alarmd_wake.c

bd-alarmd: alarmd/bd_alarmd.c $(ALARMDCORE) $(CKMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) alarmd/bd_alarmd.c \
	    $(ALARMDCORE) $(CKMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_alarmd: alarmd/tests/test_alarmd.c $(ALARMDCORE) $(CKMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) alarmd/tests/test_alarmd.c \
	    $(ALARMDCORE) $(CKMODEL) $(COMMON) -o $@ -lm

# ---------------- bd-calc ----------------
bd-calc: calc/bd_calc.c $(KCMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_CALC_MAIN calc/bd_calc.c \
	    $(KCMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_calc: calc/tests/test_calc.c $(KCMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) calc/tests/test_calc.c \
	    $(KCMODEL) $(COMMON) -o $@ -lm

test_calc_ui: calc/tests/test_calc_ui.c calc/bd_calc.c \
              $(KCMODEL) $(COMMON) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) calc/tests/test_calc_ui.c \
	    calc/bd_calc.c $(KCMODEL) $(COMMON) \
	    -o $@ $(TKLIB) $(TEXT_LIBS) -lm

# ---------------- bd-recorder ----------------
bd-recorder: recorder/bd_recorder.c $(RCMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_RECORDER_MAIN recorder/bd_recorder.c \
	    $(RCMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lasound -lm

test_recorder: recorder/tests/test_recorder.c $(RCMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) recorder/tests/test_recorder.c \
	    $(RCMODEL) $(COMMON) -o $@ -lasound -lm

test_recorder_ui: recorder/tests/test_recorder_ui.c recorder/bd_recorder.c \
                  $(RCMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) recorder/tests/test_recorder_ui.c \
	    recorder/bd_recorder.c $(RCMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lasound -lm

# ---------------- bd-player ----------------
bd-player: player/bd_player.c $(PLMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_PLAYER_MAIN player/bd_player.c \
	    $(PLMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lasound -ldl -lm

test_player: player/tests/test_player.c $(PLMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) player/tests/test_player.c \
	    $(PLMODEL) $(COMMON) -o $@ -lasound -ldl -lm

test_player_ui: player/tests/test_player_ui.c player/bd_player.c \
                $(PLMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) player/tests/test_player_ui.c \
	    player/bd_player.c $(PLMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lasound -ldl -lm

# ---------------- bd-pkg ----------------
bd-pkg: pkg/bd_pkg.c $(PKMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_PKG_MAIN pkg/bd_pkg.c \
	    $(PKMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_pkg: pkg/tests/test_pkg.c $(PKMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) pkg/tests/test_pkg.c \
	    $(PKMODEL) $(COMMON) -o $@ -lm

test_pkg_ui: pkg/tests/test_pkg_ui.c pkg/bd_pkg.c \
             $(PKMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) pkg/tests/test_pkg_ui.c \
	    pkg/bd_pkg.c $(PKMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

# ---------------- bd-settings / bd-admin ----------------
STMODEL  = settings/setmodel.c $(SYMODEL)

bd-settings: settings/bd_settings.c $(STMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_SETTINGS_MAIN settings/bd_settings.c \
	    $(STMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_settings: settings/tests/test_settings.c $(STMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) settings/tests/test_settings.c \
	    $(STMODEL) $(COMMON) -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_settings_ui: settings/tests/test_settings_ui.c settings/bd_settings.c \
                  $(STMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) settings/tests/test_settings_ui.c \
	    settings/bd_settings.c $(STMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

bd-admin: admin/bd_admin.c $(SYMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBD_ADMIN_MAIN admin/bd_admin.c \
	    $(SYMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

test_admin: admin/tests/test_admin.c $(SYMODEL) $(COMMON)
	$(CC) $(CPPFLAGS) $(CFLAGS) admin/tests/test_admin.c \
	    $(SYMODEL) $(COMMON) -o $@ -lm

test_admin_ui: admin/tests/test_admin_ui.c admin/bd_admin.c \
               $(SYMODEL) $(COMMON) $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) admin/tests/test_admin_ui.c \
	    admin/bd_admin.c $(SYMODEL) $(COMMON) \
	    -o $@ $(SHELLIB) $(TKLIB) $(TEXT_LIBS) -lm

tests: $(TESTS)

DICT ?= $(ROOT)/../ime-dict/brainux2.bidf
BISC ?= $(ROOT)/libbrainime/data/brainux2.bisc

check: tests $(APPS)
	./test_files
	./test_files_ui
	./test_tasks
	./test_tasks_ui
	BI_SIDECAR=$(abspath $(BISC)) ./test_notes $(DICT) $(BISC)
	./test_clock
	./test_clock_ui
	./test_calc
	./test_calc_ui
	./test_recorder
	./test_recorder_ui
	./test_player player/tests/fixtures
	./test_player_ui player/tests/fixtures
	./test_pkg
	./test_pkg_ui
	./test_settings
	./test_settings_ui
	./test_admin
	./test_admin_ui
	./test_alarmd

clean:
	rm -f $(APPS) $(TESTS)

.PHONY: all tests check clean
