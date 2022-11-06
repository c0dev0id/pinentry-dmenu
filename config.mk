# Pinentry settings
VERSION   = 0.1
BUGREPORT = https:\/\/github.com\/0x766F6964\/pinentry-dmenu

# Paths
PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# Xinerama, comment if you don't want it
XINERAMALIBS  = -lXinerama
XINERAMAFLAGS = -DXINERAMA

# Freetype
FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = /usr/include/freetype2
# OpenBSD (uncomment)
#FREETYPEINC = $(X11INC)/freetype2

# Includes and libs
INCS = -I$(X11INC) -I$(FREETYPEINC)
LIBS = -lassuan -lgpg-error -L$(X11LIB) -lX11 $(XINERAMALIBS) $(FREETYPELIBS)

# Flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -DVERSION=\"$(VERSION)\" $(XINERAMAFLAGS) -DPACKAGE_VERSION=\"$(VERSION)\" -DPACKAGE_BUGREPORT=\"$(BUGREPORT)\" -DHAVE_MLOCK
CFLAGS   = -std=c99 -pedantic -Wall -Os
LDFLAGS  = -s

# Compiler and linker
CC = cc
