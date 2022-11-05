pinentry-dmenu
==============

pinentry-dmenu is a pinentry program with the charm of [dmenu](https://tools.suckless.org/dmenu).

This program is a fork from [spine](https://gitgud.io/zavok/spine.git) which is also a fork from [dmenu](https://tools.suckless.org/dmenu).

This is a further fork of pinentry-dmenu with many of the upstream [dmenu](https://tools.suckless.org/dmenu) changes merged and config file support removed.


Requirements
------------
In order to build dmenu you need the Xlib header files.


Installation
------------
Edit config.mk to match your local setup (dmenu is installed into the /usr/local namespace by default).

Afterwards enter the following command to build and install dmenu
(if necessary as root):

	make clean install


Config
------
To use pinentry-dmenu add in `~/.gnupg/gpg-agent.conf`:

	pinentry-program <absolut path to pinentry-dmenu>
