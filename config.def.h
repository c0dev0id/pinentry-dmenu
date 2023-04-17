/* See LICENSE file for copyright and license details. */

/* minimum length to use for displaying the pw field */
static int minpwlen = 16;

/* character to be used as a replacement for typed characters */
static const char *asterisk = "*";

/* if 0, pinentry-dmenu appears at bottom */
static int topbar = 1;

/* default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=10"
};

static const char *prompt = NULL;
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemePrompt] = { "#bbbbbb", "#222222" },
	[SchemeNormal] = { "#bbbbbb", "#222222" },
	[SchemeSelect] = { "#eeeeee", "#005577" },
	[SchemeDesc]   = { "#bbbbbb", "#222222" }
};
