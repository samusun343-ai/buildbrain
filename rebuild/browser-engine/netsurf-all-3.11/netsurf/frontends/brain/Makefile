# Makefile for NetSurf Brainux frontend (frontends/brain)
#
# This file is part of NetSurf. It is the build description for the
# "brain" frontend: the browser chrome is drawn with the Brainux
# toolkit v2 (libbrainui2) and the page is rendered through the
# libnsfb "brain" surface, either straight into the framebuffer
# (standalone) or into shared memory as a braindesk2 WM client.
#
# The toolkit and shell client sources live outside the NetSurf
# tree, in the Brainux rebuild workspace:
#
#   REBUILD_ROOT/include/      toolkit + OSK public headers
#   REBUILD_ROOT/shell/include WM client + brainreg headers
#   REBUILD_ROOT/shell/src/    wmclient.c, reg.c (compiled in, the
#                              WM protocol is not re-implemented)
#   REBUILD_ROOT/libbrainui2.a toolkit static library
#
# This file is included from the buildsystem "instance" makefile
# after all common sources have been included.

# The BRAIN budget options (brain_budget_mb etc.) and the cache
# derivation live in frontends/brain/brainreg.c; the memory budget
# model itself is the M0 one (REPORT-browser-m0.md).

CFLAGS += -Dnsbrain -Dsmall
# the shell sources compiled in below use memfd_create & friends
CFLAGS += -D_GNU_SOURCE
CFLAGS += -I$(REBUILD_ROOT)/include -I$(REBUILD_ROOT)/shell/include
CFLAGS += $(shell $(PKG_CONFIG) --cflags libnsfb)
CFLAGS += $(shell $(PKG_CONFIG) --cflags freetype2)
CFLAGS += -DFB_USE_FREETYPE

# toolkit static library (built by the rebuild workspace); the
# link rule is $(OBJECTS) $(LDFLAGS), so it goes to LDFLAGS where
# it lands after the objects that reference it
LDFLAGS += $(REBUILD_ROOT)/libbrainui2.a

# Workspace sysroot with development files the build host may be
# missing (libcurl/libjpeg/libwebp headers and .so symlinks,
# extracted from the distro packages into
# browser-engine/.sysroot; see REPORT-brain-frontend.md).  The
# pkg-config files in there are found through PKG_CONFIG_PATH at
# make launch; the include/lib paths are added here directly so
# the non-pkg-config JPEG switch works too.  Absent on hosts that
# provide the packages natively.
BRAIN_SYSROOT := $(REBUILD_ROOT)/browser-engine/.sysroot
ifneq ($(wildcard $(BRAIN_SYSROOT)/usr/include/jpeglib.h),)
CFLAGS += -I$(BRAIN_SYSROOT)/usr/include
LDFLAGS += -L$(BRAIN_SYSROOT)/usr/lib/x86_64-linux-gnu
endif

LDFLAGS += $(shell $(PKG_CONFIG) --libs libnsfb)
LDFLAGS += $(shell $(PKG_CONFIG) --libs freetype2)
# libbrainui2.a (M24+) also references HarfBuzz symbols at link time
LDFLAGS += $(shell $(PKG_CONFIG) --libs harfbuzz)
LDFLAGS += -lm
# the libnsfb brain surface registers itself from a constructor
# in an archive member nothing else references; force the member
# in or nsfb_type_from_name("brain") finds no surface
LDFLAGS += -Wl,-u,brain_rtns

# resource paths: the frontend's own res/ first, then the toolkit
# fonts directory so the page font can fall back to the bundled
# subset font by name as well as by explicit option.  The
# source-tree entries simply do not exist on the device and are
# skipped there; the installed locations follow them.
BRAIN_RESPATH := frontends/brain/res/:../../../fonts/:$${HOME}/.netsurf/:$${NETSURFRES}:/usr/share/netsurf:/usr/local/share/fonts/libbrainui2/

CFLAGS += -DNETSURF_BRAIN_RESPATH='"$(BRAIN_RESPATH)"'
CFLAGS += -DNETSURF_FB_FONTPATH='"$(BRAIN_RESPATH)"'
CFLAGS += -DNETSURF_FB_FONT_SANS_SERIF='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_SANS_SERIF_BOLD='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_SANS_SERIF_ITALIC='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_SANS_SERIF_ITALIC_BOLD='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_SERIF='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_SERIF_BOLD='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_MONOSPACE='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_MONOSPACE_BOLD='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_CURSIVE='"brainui-jp-subset.ttf"'
CFLAGS += -DNETSURF_FB_FONT_FANTASY='"brainui-jp-subset.ttf"'

# SOURCES lists all the files to be compiled for the browser.
# The BRAIN_SOURCES name matches the pattern the top-level
# Makefile expects for the $(TARGET) frontend.

# generic NetSurf sources adapted from the framebuffer frontend
# (bitmap.c keeps the M0 image-pixel cap from REPORT-browser-m0.md)
S_FRONTEND := schedule.c fetch.c findfile.c clipboard.c bitmap.c \
	font_freetype.c

# brain frontend sources
S_FRONTEND += gui.c window.c toolbar.c pageview.c surface.c brainreg.c

# shell client + registry, compiled in from the rebuild workspace
# (read-only use of the shell tree: the WM protocol implementation
# is shared, not duplicated).  Paths are relative to this
# directory: frontends/brain -> frontends -> netsurf ->
# netsurf-all-3.11 -> browser-engine -> rebuild.
S_FRONTEND += ../../../../../shell/src/wmclient.c \
	../../../../../shell/src/reg.c \
	../../../../../shell/src/thememode.c \
	../../../../../shell/src/wm.c

# complete source file list
SOURCES = $(S_COMMON) $(S_IMAGE) $(S_BROWSER) $(S_FRONTEND)

# Target for executable
EXETARGET := nsbrain

# The resources are in the source repository, so the messages are
# split into the frontend res directory.  The message catalogue
# has no brain-specific platform section; the brain frontend is
# in the framebuffer class for messages, like the fb frontend.
MESSAGES_FILTER = fb
MESSAGES_TARGET = frontends/brain/res

# ----------------------------------------------------------------------------
# Package target
# ----------------------------------------------------------------------------

install-brain:

package-brain:
