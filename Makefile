# pinentry-dmenu - dmenu-like stupid pin entry
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC = pinentry-dmenu.c drw.c util.c
OBJ = $(SRC:.c=.o)
PIN_SRC = \
	pinentry/argparse.c\
	pinentry/password-cache.c\
	pinentry/pinentry.c\
	pinentry/secmem.c\
	pinentry/util.c
PIN_OBJ = $(PIN_SRC:.c=.o)
PIN_DEP = \
	pinentry/argparse.h\
	pinentry/password-cache.h\
	pinentry/pinentry.h\
	pinentry/memory.h\
	pinentry/util.h

all: pinentry-dmenu

.c.o:
	$(CC) -c $(CFLAGS) $(INCS) $(CPPFLAGS) -o $@ -c $<

config.h:
	cp config.def.h $@

$(OBJ): config.h config.mk drw.h

$(PIN_OBJ): $(PIN_DEP)

pinentry-dmenu: $(OBJ) $(PIN_OBJ)
	$(CC) -o $@ $(OBJ) $(PIN_OBJ) $(LDFLAGS) $(LIBS)

clean:
	rm -f pinentry-dmenu $(OBJ) $(PIN_OBJ)

distclean: clean
	rm -f config.h
	rm -rf pinentry-dmenu-$(VERSION)*

dist: distclean
	mkdir -p pinentry-dmenu-$(VERSION)
	cp -r LICENSE Makefile *.md *.h *.mk *.1 *.c pinentry \
		pinentry-dmenu-$(VERSION)/
	tar -czf pinentry-dmenu-$(VERSION).tgz pinentry-dmenu-$(VERSION)
	rm -rf pinentry-dmenu-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f pinentry-dmenu $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/pinentry-dmenu
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < pinentry-dmenu.1 > $(DESTDIR)$(MANPREFIX)/man1/pinentry-dmenu.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/pinentry-dmenu.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pinentry-dmenu
	rm -f $(DESTDIR)$(MANPREFIX)/man1/pinentry-dmenu.1

.PHONY: clean dist distclean install uninstall
