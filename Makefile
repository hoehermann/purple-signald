PKG_CONFIG ?= pkg-config

# Note: Use "-C .git" to avoid ascending to parent dirs if .git not present
GIT_REVISION_ID = $(shell git -C .git rev-parse --short HEAD 2>/dev/null)
PLUGIN_VERSION ?= $(shell cat VERSION)~git$(GIT_REVISION_ID)
PKG_DEPS ?= purple glib-2.0 json-glib-1.0

CFLAGS	?= -O0 -g -ggdb -Wall
LDFLAGS ?= 
LIBS ?= 
CC ?= gcc

CFLAGS  += -DSIGNALD_PLUGIN_VERSION='"$(PLUGIN_VERSION)"' -DMARKDOWN_PIDGIN
CFLAGS  += -std=c99 -fPIC
LDFLAGS += -Wl,-z,relro
CFLAGS  += -Ipurple2compat `$(PKG_CONFIG) $(PKG_DEPS) --cflags`
CFLAGS  += -Isubmodules/MegaMimes/src -Isubmodules/QR-Code-generator/c
LIBS += `$(PKG_CONFIG) $(PKG_DEPS) --libs`

ifeq ($(shell $(PKG_CONFIG) --exists purple 2>/dev/null && echo "true"),)
  TARGET = FAILNOPURPLE
  DEST =
else
  TARGET = libsignald.so
  DEST = $(DESTDIR)$(shell $(PKG_CONFIG) --variable=plugindir purple)/
  LOCALEDIR = $(DESTDIR)$(shell $(PKG_CONFIG) --variable=datadir purple)/locale
  PIXMAPDIR = $(DESTDIR)$(shell $(PKG_CONFIG) --variable=datadir pidgin)/pixmaps/pidgin/protocols
endif

CFLAGS += -DLOCALEDIR=\"$(LOCALEDIR)\"

COMPAT_FILES := purple_compat.h json_compat.h
H_FILES := comms.h contacts.h direct.h groups.h link.h message.h libsignald.h submodules/MegaMimes/src/MegaMimes.h submodules/QR-Code-generator/c/qrcodegen.h
C_FILES := comms.c contacts.c direct.c groups.c link.c message.c libsignald.c
OBJ_FILES:=$(C_FILES:.c=.o)

.PHONY:	all FAILNOPURPLE clean install

LOCALES = $(patsubst %.po, %.mo, $(wildcard po/*.po))

all: $(TARGET)

$(OBJ_FILES): %.o: %.c Makefile $(H_FILES) $(COMPAT_FILES) 
	$(CC) -c $< $(CFLAGS)

libsignald.so: $(OBJ_FILES) MegaMimes.o qrcodegen.o
	$(CC) $(LDFLAGS) -shared $^ $(LIBS) -o $@

FAILNOPURPLE:
	echo "You need libpurple development headers installed to be able to compile this plugin"

clean:
	rm -f $(TARGET) *.o

gdb:
	gdb --args pidgin -c ~/.fake_purple -n -m

install:
	install -Dm644 "$(TARGET)" "$(DEST)$(TARGET)" 
	install -Dm644 icons/16/signal.png "$(PIXMAPDIR)/16/signal.png"
	install -Dm644 icons/22/signal.png "$(PIXMAPDIR)/22/signal.png"
	install -Dm644 icons/48/signal.png "$(PIXMAPDIR)/48/signal.png"
	
