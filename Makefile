PKG_CONFIG ?= pkg-config

# Note: Use "-C .git" to avoid ascending to parent dirs if .git not present
GIT_REVISION_ID = $(shell git -C .git rev-parse --short HEAD 2>/dev/null)
PLUGIN_VERSION ?= $(shell cat VERSION)~git$(GIT_REVISION_ID)
PKG_DEPS ?= purple glib-2.0 json-glib-1.0

CFLAGS	?= -O2 -g -ggdb -Wall
LDFLAGS ?= 
LIBS ?= 

ifdef SUPPORT_EXTERNAL_ATTACHMENTS
LIBS += -lmagic
PKG_DEPS += gio-unix-2.0
CFLAGS += -DSUPPORT_EXTERNAL_ATTACHMENTS
endif

CFLAGS  += -DSIGNALD_PLUGIN_VERSION='"$(PLUGIN_VERSION)"' -DMARKDOWN_PIDGIN
CFLAGS  += -std=c99 -fPIC
LDFLAGS += -Wl,-z,relro
CFLAGS  += -Ipurple2compat `$(PKG_CONFIG) $(PKG_DEPS) --cflags`
LIBS += `$(PKG_CONFIG) $(PKG_DEPS) --libs`

CC ?= gcc

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

PURPLE_COMPAT_FILES := purple_compat.h json_compat.h
PURPLE_H_FILES := comms.h contacts.h direct.h groups.h link.h message.h libsignald.h
PURPLE_C_FILES := comms.c contacts.c direct.c groups.c link.c message.c libsignald.c $(C_FILES)
PURPLE_OBJ_FILES:=$(PURPLE_C_FILES:.c=.o)

.PHONY:	all FAILNOPURPLE clean install

LOCALES = $(patsubst %.po, %.mo, $(wildcard po/*.po))

all: $(TARGET)

$(PURPLE_OBJ_FILES): %.o: %.c Makefile $(PURPLE_H_FILES) $(PURPLE_COMPAT_FILES)
	$(CC) -c $< $(CFLAGS)

libsignald.so: $(PURPLE_OBJ_FILES)
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
	install -Dm644 icons/48/signal.png "$(PIXMAPDIR)/48/signal.png"
	
