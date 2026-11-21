# Screenshots harness - renders real product UI offscreen.
# Build from rebuild/:  make -C screenshots
# Run from rebuild/ (font + fixture relative paths depend on it):
#   ./screenshots/shot_shell
#   ./screenshots/shot_greeter
#   ./screenshots/shot_osk
#   ./screenshots/shot_apps files|notes|settings|calc|player
# Then:  python3 screenshots/convert.py
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
ROOT     = ..
CPPFLAGS += -I$(ROOT)/include -I$(ROOT)/shell/include \
            -I$(ROOT)/shell/greeter -I$(ROOT)/libbrainime/include \
            -I$(ROOT)/osk -I$(ROOT)/apps/common -I$(ROOT)/apps/files \
            -I$(ROOT)/apps/notes -I$(ROOT)/apps/tasks -I$(ROOT)/apps/clock \
            -I$(ROOT)/apps/calc -I$(ROOT)/apps/player \
            -I$(ROOT)/apps/recorder -I$(ROOT)/apps/pkg \
            -I$(ROOT)/apps/settings -I$(ROOT)/apps/admin -I$(ROOT)/apps \
            -Isrc -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

TKLIB    = $(ROOT)/libbrainui2.a
SHELLIB  = $(ROOT)/shell/libbdshell.a
IMELIB   = $(ROOT)/libbrainime/libbrainime.a
APPS     = $(ROOT)/apps

APPSSRC  = $(APPS)/files/bd_files.c $(APPS)/files/fmodel.c \
           $(APPS)/notes/bd_notes.c $(APPS)/notes/nmodel.c \
           $(APPS)/calc/bd_calc.c $(APPS)/calc/calcmodel.c \
           $(APPS)/player/bd_player.c $(APPS)/player/playmodel.c \
           $(APPS)/player/pengine.c \
           $(APPS)/settings/bd_settings.c $(APPS)/settings/setmodel.c \
           $(APPS)/tasks/bd_tasks.c $(APPS)/tasks/tmodel.c \
           $(APPS)/clock/bd_clock.c $(APPS)/clock/clockmodel.c \
           $(APPS)/recorder/bd_recorder.c $(APPS)/recorder/recmodel.c \
           $(APPS)/pkg/bd_pkg.c $(APPS)/pkg/pkgmodel.c \
           $(APPS)/admin/bd_admin.c $(APPS)/admin/sysmodel.c \
           $(APPS)/common/bdstore.c $(APPS)/common/bdmime.c \
           $(ROOT)/osk/osk_brainime.c

all: shot_shell shot_greeter shot_osk shot_apps shot_osk_shell shot_mascot

shot_mascot: src/shot_mascot.c src/shot_common.h $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_mascot.c -o $@ \
	    $(SHELLIB) $(TKLIB) -lm

$(ROOT)/shell/im_client:
	$(MAKE) -C $(ROOT)/shell im_client

shot_osk_shell: src/shot_osk_shell.c src/shot_common.h $(SHELLIB) $(TKLIB) $(IMELIB) $(ROOT)/shell/im_client
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_osk_shell.c \
	    $(ROOT)/osk/osk_brainime.c -o $@ \
	    $(SHELLIB) $(TKLIB) $(IMELIB) -lm

shot_shell: src/shot_shell.c src/shot_common.h $(SHELLIB) $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_shell.c -o $@ \
	    $(SHELLIB) $(TKLIB) -lm

shot_greeter: src/shot_greeter.c src/shot_common.h $(TKLIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_greeter.c \
	    $(ROOT)/shell/greeter/greeter2_auth.c -o $@ $(TKLIB) -lm -lcrypt

shot_osk: src/shot_osk.c src/shot_common.h $(TKLIB) $(IMELIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_osk.c \
	    $(ROOT)/osk/osk_brainime.c -o $@ $(TKLIB) $(IMELIB) -lm

shot_apps: src/shot_apps.c src/shot_common.h $(APPSSRC) $(SHELLIB) $(TKLIB) $(IMELIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_apps.c $(APPSSRC) -o $@ \
	    $(SHELLIB) $(TKLIB) $(IMELIB) -lasound -ldl -lm

shot_apps_envfit: src/shot_apps_envfit.c src/shot_common.h $(APPSSRC) $(SHELLIB) $(TKLIB) $(IMELIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/shot_apps_envfit.c $(APPSSRC) -o $@ \
	    $(SHELLIB) $(TKLIB) $(IMELIB) -lasound -ldl -lm

clean:
	rm -f shot_shell shot_greeter shot_osk shot_apps shot_osk_shell

.PHONY: all clean
