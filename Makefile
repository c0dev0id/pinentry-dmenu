# pinentry-dmenu - dmenu-like stupid pin entry
# See LICENSE file for copyright and license details.

include config.mk

SRC = pinentry-dmenu.c drw.c util.c
OBJ = $(SRC:.c=.o)
OBJ_PIN = pinentry/pinentry.o pinentry/util.o pinentry/password-cache.o pinentry/argparse.o pinentry/secmem.o

all: options pinentry-dmenu

options:
	@echo pinentry-dmenu build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

.c.o:
	$(CC) -c $(CFLAGS) $(INCS) $<

config.h:
	cp config.def.h $@

$(OBJ): config.h config.mk drw.h

pinentry:
	$(MAKE) -C pinentry

pinentry-dmenu: pinentry pinentry-dmenu.o drw.o util.o
	$(CC) -o $@ $(OBJ) $(OBJ_PIN) $(LDFLAGS) $(LIBS)

clean:
	rm -f pinentry-dmenu $(OBJ)
	$(MAKE) -C pinentry/ clean

dist: clean
	mkdir -p pinentry-dmenu-$(VERSION)
	cp LICENSE Makefile README arg.h config.def.h config.mk dmenu.1 \
		drw.h util.h $(SRC) \
		pinentry-dmenu-$(VERSION)
	tar -cf pinentry-dmenu-$(VERSION).tar pinentry-dmenu-$(VERSION)
	gzip pinentry-dmenu-$(VERSION).tar
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

.PHONY: all options clean dist install pinentry uninstall
