PROTOC_C ?= protoc-c
PKG_CONFIG ?= pkg-config

# Note: Use "-C .git" to avoid ascending to parent dirs if .git not present
GIT_REVISION_ID = $(shell git -C .git rev-parse --short HEAD 2>/dev/null)
PLUGIN_VERSION ?= 0.1.$(shell date +%Y.%m.%d).git.$(GIT_REVISION_ID)

CFLAGS	?= -O2 -g -pipe -Wall
LDFLAGS ?= -Wl,-z,relro

CFLAGS  += -std=c99 -DDISCORD_PLUGIN_VERSION='"$(PLUGIN_VERSION)"' -DMARKDOWN_PIDGIN

CC ?= gcc

ifeq ($(shell $(PKG_CONFIG) --exists purple 2>/dev/null && echo "true"),)
  DISCORD_TARGET = FAILNOPURPLE
  DISCORD_DEST =
  DISCORD_ICONS_DEST =
else
  DISCORD_TARGET = libdiscord.so
  DISCORD_DEST = $(DESTDIR)`$(PKG_CONFIG) --variable=plugindir purple`
  DISCORD_ICONS_DEST = $(DESTDIR)`$(PKG_CONFIG) --variable=datadir purple`/pixmaps/pidgin/protocols
  LOCALEDIR = $(DESTDIR)$(shell $(PKG_CONFIG) --variable=datadir purple)/locale
endif

CFLAGS += -DLOCALEDIR=\"$(LOCALEDIR)\"

PURPLE_COMPAT_FILES :=
PURPLE_C_FILES := libdiscord.c $(C_FILES)

.PHONY:	all FAILNOPURPLE clean

LOCALES = $(patsubst %.po, %.mo, $(wildcard po/*.po))

all: $(DISCORD_TARGET)

libdiscord.so: $(PURPLE_C_FILES) $(PURPLE_COMPAT_FILES)
	$(CC) -fPIC $(CFLAGS) $(CPPFLAGS) -shared -o $@ $^ $(LDFLAGS) `$(PKG_CONFIG) purple glib-2.0 json-glib-1.0 --libs --cflags`  $(INCLUDES) -Ipurple2compat -g -ggdb


FAILNOPURPLE:
	echo "You need libpurple development headers installed to be able to compile this plugin"

clean:
	rm -f $(DISCORD_TARGET)
	rm -f discord*.png

gdb:
	gdb --args pidgin -c ~/.fake_purple -n -m

