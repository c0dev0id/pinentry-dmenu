# Pinentry settings
VERSION   = 0.2
BUGREPORT = https:\/\/github.com\/c0dev0id\/pinentry-dmenu

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
FREETYPEINC = $(X11INC)/freetype2

# Includes and libs
INCS = -I$(X11INC) -I$(FREETYPEINC) -I/usr/local/include
LIBS = -lassuan -lgpg-error -L$(X11LIB) -lX11 $(XINERAMALIBS) $(FREETYPELIBS) -L/usr/local/lib

# Flags
CPPFLAGS = -DVERSION=\"$(VERSION)\" $(XINERAMAFLAGS) -DPACKAGE_VERSION=\"$(VERSION)\" -DPACKAGE_BUGREPORT=\"$(BUGREPORT)\" -DHAVE_MLOCK
CFLAGS   = -std=c99 -pedantic -Wall
