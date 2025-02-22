/* pinentry.c - The PIN entry support library
   Copyright (C) 2002, 2003, 2007, 2008, 2010, 2015 g10 Code GmbH

   This file is part of PINENTRY.

   PINENTRY is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   PINENTRY is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>

#include <assuan.h>

#include "memory.h"
#include "secmem-util.h"
#include "argparse.h"
#include "pinentry.h"
#include "password-cache.h"

/* Keep the name of our program here. */
static char this_pgmname[50];

struct pinentry pinentry;

static void
pinentry_reset (int use_defaults)
{
  /* GPG Agent sets these options once when it starts the pinentry.
     Don't reset them.  */
  int grab = pinentry.grab;
  char *ttyname = pinentry.ttyname;
  char *ttytype = pinentry.ttytype;
  char *lc_ctype = pinentry.lc_ctype;
  char *lc_messages = pinentry.lc_messages;
  int allow_external_password_cache = pinentry.allow_external_password_cache;
  char *default_ok = pinentry.default_ok;
  char *default_cancel = pinentry.default_cancel;
  char *default_prompt = pinentry.default_prompt;
  char *default_pwmngr = pinentry.default_pwmngr;
  char *touch_file = pinentry.touch_file;

  /* These options are set from the command line.  Don't reset
     them.  */
  int debug = pinentry.debug;
  char *display = pinentry.display;
  int parent_wid = pinentry.parent_wid;

  pinentry_color_t color_fg = pinentry.color_fg;
  int color_fg_bright = pinentry.color_fg_bright;
  pinentry_color_t color_bg = pinentry.color_bg;
  pinentry_color_t color_so = pinentry.color_so;
  int color_so_bright = pinentry.color_so_bright;

  int timout = pinentry.timeout;

  /* Free any allocated memory.  */
  if (use_defaults)
    {
      free (pinentry.ttyname);
      free (pinentry.ttytype);
      free (pinentry.lc_ctype);
      free (pinentry.lc_messages);
      free (pinentry.default_ok);
      free (pinentry.default_cancel);
      free (pinentry.default_prompt);
      free (pinentry.default_pwmngr);
      free (pinentry.touch_file);
      free (pinentry.display);
    }

  free (pinentry.title);
  free (pinentry.description);
  free (pinentry.error);
  free (pinentry.prompt);
  free (pinentry.ok);
  free (pinentry.notok);
  free (pinentry.cancel);
  secmem_free (pinentry.pin);
  free (pinentry.repeat_passphrase);
  free (pinentry.repeat_error_string);
  free (pinentry.quality_bar);
  free (pinentry.quality_bar_tt);
  free (pinentry.keyinfo);

  /* Reset the pinentry structure.  */
  memset (&pinentry, 0, sizeof (pinentry));

  if (use_defaults)
    {
      /* Pinentry timeout in seconds.  */
      pinentry.timeout = 60;

      /* Global grab.  */
      pinentry.grab = 1;

      pinentry.color_fg = PINENTRY_COLOR_DEFAULT;
      pinentry.color_fg_bright = 0;
      pinentry.color_bg = PINENTRY_COLOR_DEFAULT;
      pinentry.color_so = PINENTRY_COLOR_DEFAULT;
      pinentry.color_so_bright = 0;
    }
  else
    /* Restore the options.  */
    {
      pinentry.grab = grab;
      pinentry.ttyname = ttyname;
      pinentry.ttytype = ttytype;
      pinentry.lc_ctype = lc_ctype;
      pinentry.lc_messages = lc_messages;
      pinentry.allow_external_password_cache = allow_external_password_cache;
      pinentry.default_ok = default_ok;
      pinentry.default_cancel = default_cancel;
      pinentry.default_prompt = default_prompt;
      pinentry.default_pwmngr = default_pwmngr;
      pinentry.touch_file = touch_file;

      pinentry.debug = debug;
      pinentry.display = display;
      pinentry.parent_wid = parent_wid;

      pinentry.color_fg = color_fg;
      pinentry.color_fg_bright = color_fg_bright;
      pinentry.color_bg = color_bg;
      pinentry.color_so = color_so;
      pinentry.color_so_bright = color_so_bright;

      pinentry.timeout = timout;
    }
}

static gpg_error_t
pinentry_assuan_reset_handler (assuan_context_t ctx, char *line)
{
  (void)ctx;
  (void)line;

  pinentry_reset (0);

  return 0;
}


/* Copy TEXT or TEXTLEN to BUFFER and escape as required.  Return a
   pointer to the end of the new buffer.  Note that BUFFER must be
   large enough to keep the entire text; allocataing it 3 times of
   TEXTLEN is sufficient.  */
static char *
copy_and_escape (char *buffer, const void *text, size_t textlen)
{
  int i;
  const unsigned char *s = (unsigned char *)text;
  char *p = buffer;

  for (i=0; i < textlen; i++)
    {
      if (s[i] < ' ' || s[i] == '+')
        {
          snprintf (p, 4, "%%%02X", s[i]);
          p += 3;
        }
      else if (s[i] == ' ')
        *p++ = '+';
      else
        *p++ = s[i];
    }
  return p;
}



/* Run a quality inquiry for PASSPHRASE of LENGTH.  (We need LENGTH
   because not all backends might be able to return a proper
   C-string.).  Returns: A value between -100 and 100 to give an
   estimate of the passphrase's quality.  Negative values are use if
   the caller won't even accept that passphrase.  Note that we expect
   just one data line which should not be escaped in any represent a
   numeric signed decimal value.  Extra data is currently ignored but
   should not be send at all.  */
int
pinentry_inq_quality (pinentry_t pin, const char *passphrase, size_t length)
{
  assuan_context_t ctx = pin->ctx_assuan;
  const char prefix[] = "INQUIRE QUALITY ";
  char *command;
  char *line;
  size_t linelen;
  int gotvalue = 0;
  int value = 0;
  int rc;

  if (!ctx)
    return 0; /* Can't run the callback.  */

  if (length > 300)
    length = 300;  /* Limit so that it definitely fits into an Assuan
                      line.  */

  command = secmem_malloc (strlen (prefix) + 3*length + 1);
  if (!command)
    return 0;
  strlcpy (command, prefix, strlen(prefix));
  copy_and_escape (command + strlen(command), passphrase, length);
  rc = assuan_write_line (ctx, command);
  secmem_free (command);
  if (rc)
    {
      fprintf (stderr, "ASSUAN WRITE LINE failed: rc=%d\n", rc);
      return 0;
    }

  for (;;)
    {
      do
        {
          rc = assuan_read_line (ctx, &line, &linelen);
          if (rc)
            {
              fprintf (stderr, "ASSUAN READ LINE failed: rc=%d\n", rc);
              return 0;
            }
        }
      while (*line == '#' || !linelen);
      if (line[0] == 'E' && line[1] == 'N' && line[2] == 'D'
          && (!line[3] || line[3] == ' '))
        break; /* END command received*/
      if (line[0] == 'C' && line[1] == 'A' && line[2] == 'N'
          && (!line[3] || line[3] == ' '))
        break; /* CAN command received*/
      if (line[0] == 'E' && line[1] == 'R' && line[2] == 'R'
          && (!line[3] || line[3] == ' '))
        break; /* ERR command received*/
      if (line[0] != 'D' || line[1] != ' ' || linelen < 3 || gotvalue)
        continue;
      gotvalue = 1;
      value = atoi (line+2);
    }
  if (value < -100)
    value = -100;
  else if (value > 100)
    value = 100;

  return value;
}



/* Try to make room for at least LEN bytes in the pinentry.  Returns
   new buffer on success and 0 on failure or when the old buffer is
   sufficient.  */
char *
pinentry_setbufferlen (pinentry_t pin, int len)
{
  char *newp;

  if (pin->pin_len)
    assert (pin->pin);
  else
    assert (!pin->pin);

  if (len < 2048)
    len = 2048;

  if (len <= pin->pin_len)
    return pin->pin;

  newp = secmem_realloc (pin->pin, len);
  if (newp)
    {
      pin->pin = newp;
      pin->pin_len = len;
    }
  else
    {
      secmem_free (pin->pin);
      pin->pin = 0;
      pin->pin_len = 0;
    }
  return newp;
}

static void
pinentry_setbuffer_clear (pinentry_t pin)
{
  if (! pin->pin)
    {
      assert (pin->pin_len == 0);
      return;
    }

  assert (pin->pin_len > 0);

  secmem_free (pin->pin);
  pin->pin = NULL;
  pin->pin_len = 0;
}

static void
pinentry_setbuffer_init (pinentry_t pin)
{
  pinentry_setbuffer_clear (pin);
  pinentry_setbufferlen (pin, 0);
}

/* passphrase better be alloced with secmem_alloc.  */
void
pinentry_setbuffer_use (pinentry_t pin, char *passphrase, int len)
{
  if (! passphrase)
    {
      assert (len == 0);
      pinentry_setbuffer_clear (pin);

      return;
    }

  if (passphrase && len == 0)
    len = strlen (passphrase) + 1;

  if (pin->pin)
    secmem_free (pin->pin);

  pin->pin = passphrase;
  pin->pin_len = len;
}

static struct assuan_malloc_hooks assuan_malloc_hooks = {
  secmem_malloc, secmem_realloc, secmem_free
};

/* Initialize the secure memory subsystem, drop privileges and return.
   Must be called early. */
void
pinentry_init (const char *pgmname)
{
  /* Store away our name. */
  if (strlen (pgmname) > sizeof this_pgmname - 2)
    abort ();
  strlcpy (this_pgmname, pgmname, strlen(pgmname));

  gpgrt_check_version (NULL);

  /* Initialize secure memory.  1 is too small, so the default size
     will be used.  */
  secmem_init (1);
  secmem_set_flags (SECMEM_WARN);
  drop_privs ();

  if (atexit (secmem_term))
    {
      /* FIXME: Could not register at-exit function, bail out.  */
    }

  assuan_set_malloc_hooks (&assuan_malloc_hooks);
}

/* Simple test to check whether DISPLAY is set or the option --display
   was given.  Used to decide whether the GUI or curses should be
   initialized.  */
int
pinentry_have_display (int argc, char **argv)
{
  for (; argc; argc--, argv++)
    if (!strcmp (*argv, "--display") || !strncmp (*argv, "--display=", 10))
      return 1;
  return 0;
}



/* Print usage information and and provide strings for help. */
static const char *
my_strusage( int level )
{
  const char *p;

  switch (level)
    {
    case 11: p = this_pgmname; break;
    case 12: p = "pinentry"; break;
    case 13: p = PACKAGE_VERSION; break;
    case 14: p = "Copyright (C) 2015 g10 Code GmbH"; break;
    case 19: p = "Please report bugs to <" PACKAGE_BUGREPORT ">.\n"; break;
    case 1:
    case 40:
      {
        static char *str;

        if (!str)
          {
            size_t n = 50 + strlen (this_pgmname);
            str = malloc (n);
            if (str)
              snprintf (str, n, "Usage: %s [options] (-h for help)",
                        this_pgmname);
          }
        p = str;
      }
      break;
    case 41:
      p = "Ask securely for a secret and print it to stdout.";
      break;

    case 42:
      p = "1"; /* Flag print 40 as part of 41. */
      break;

    default: p = NULL; break;
    }
  return p;
}


char *
parse_color (char *arg, pinentry_color_t *color_p, int *bright_p)
{
  static struct
  {
    const char *name;
    pinentry_color_t color;
  } colors[] = { { "none", PINENTRY_COLOR_NONE },
		 { "default", PINENTRY_COLOR_DEFAULT },
		 { "black", PINENTRY_COLOR_BLACK },
		 { "red", PINENTRY_COLOR_RED },
		 { "green", PINENTRY_COLOR_GREEN },
		 { "yellow", PINENTRY_COLOR_YELLOW },
		 { "blue", PINENTRY_COLOR_BLUE },
		 { "magenta", PINENTRY_COLOR_MAGENTA },
		 { "cyan", PINENTRY_COLOR_CYAN },
		 { "white", PINENTRY_COLOR_WHITE } };

  int i;
  char *new_arg;
  pinentry_color_t color = PINENTRY_COLOR_DEFAULT;

  if (!arg)
    return NULL;

  new_arg = strchr (arg, ',');
  if (new_arg)
    new_arg++;

  if (bright_p)
    {
      const char *bname[] = { "bright-", "bright", "bold-", "bold" };

      *bright_p = 0;
      for (i = 0; i < sizeof (bname) / sizeof (bname[0]); i++)
	if (!strncasecmp (arg, bname[i], strlen (bname[i])))
	  {
	    *bright_p = 1;
	    arg += strlen (bname[i]);
	  }
    }

  for (i = 0; i < sizeof (colors) / sizeof (colors[0]); i++)
    if (!strncasecmp (arg, colors[i].name, strlen (colors[i].name)))
      color = colors[i].color;

  *color_p = color;
  return new_arg;
}

/* Parse the command line options.  May exit the program if only help
   or version output is requested.  */
void
pinentry_parse_opts (int argc, char *argv[])
{
  static ARGPARSE_OPTS opts[] = {
    ARGPARSE_s_n('d', "debug",    "Turn on debugging output"),
    ARGPARSE_s_s('D', "display",  "|DISPLAY|Set the X display"),
    ARGPARSE_s_s('T', "ttyname",  "|FILE|Set the tty terminal node name"),
    ARGPARSE_s_s('N', "ttytype",  "|NAME|Set the tty terminal type"),
    ARGPARSE_s_s('C', "lc-ctype", "|STRING|Set the tty LC_CTYPE value"),
    ARGPARSE_s_s('M', "lc-messages", "|STRING|Set the tty LC_MESSAGES value"),
    ARGPARSE_s_i('o', "timeout",
                 "|SECS|Timeout waiting for input after this many seconds"),
    ARGPARSE_s_n('g', "no-global-grab",
                 "Grab keyboard only while window is focused"),
    ARGPARSE_s_u('W', "parent-wid", "Parent window ID (for positioning)"),
    ARGPARSE_s_s('c', "colors", "|STRING|Set custom colors for ncurses"),
    ARGPARSE_end()
  };
  ARGPARSE_ARGS pargs = { &argc, &argv, 0 };

  set_strusage (my_strusage);

  pinentry_reset (1);

  while (arg_parse  (&pargs, opts))
    {
      switch (pargs.r_opt)
        {
        case 'd':
          pinentry.debug = 1;
          break;
        case 'g':
          pinentry.grab = 0;
          break;

	case 'D':
          /* Note, this is currently not used because the GUI engine
             has already been initialized when parsing these options. */
	  pinentry.display = strdup (pargs.r.ret_str);
	  if (!pinentry.display)
	    {
	      exit (EXIT_FAILURE);
	    }
	  break;
	case 'T':
	  pinentry.ttyname = strdup (pargs.r.ret_str);
	  if (!pinentry.ttyname)
	    {
	      exit (EXIT_FAILURE);
	    }
	  break;
	case 'N':
	  pinentry.ttytype = strdup (pargs.r.ret_str);
	  if (!pinentry.ttytype)
	    {
	      exit (EXIT_FAILURE);
	    }
	  break;
	case 'C':
	  pinentry.lc_ctype = strdup (pargs.r.ret_str);
	  if (!pinentry.lc_ctype)
	    {
	      exit (EXIT_FAILURE);
	    }
	  break;
	case 'M':
	  pinentry.lc_messages = strdup (pargs.r.ret_str);
	  if (!pinentry.lc_messages)
	    {
	      exit (EXIT_FAILURE);
	    }
	  break;
	case 'W':
	  pinentry.parent_wid = pargs.r.ret_ulong;
	  break;

	case 'c':
          {
            char *tmpstr = pargs.r.ret_str;

            tmpstr = parse_color (tmpstr, &pinentry.color_fg,
                                  &pinentry.color_fg_bright);
            tmpstr = parse_color (tmpstr, &pinentry.color_bg, NULL);
            tmpstr = parse_color (tmpstr, &pinentry.color_so,
                                  &pinentry.color_so_bright);
          }
	  break;

	case 'o':
	  pinentry.timeout = pargs.r.ret_int;
	  break;

        default:
          pargs.err = ARGPARSE_PRINT_WARNING;
	  break;
        }
    }
}


static gpg_error_t
option_handler (assuan_context_t ctx, const char *key, const char *value)
{
  (void)ctx;

  if (!strcmp (key, "no-grab") && !*value)
    pinentry.grab = 0;
  else if (!strcmp (key, "grab") && !*value)
    pinentry.grab = 1;
  else if (!strcmp (key, "debug-wait"))
    {
    }
  else if (!strcmp (key, "display"))
    {
      if (pinentry.display)
	free (pinentry.display);
      pinentry.display = strdup (value);
      if (!pinentry.display)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "ttyname"))
    {
      if (pinentry.ttyname)
	free (pinentry.ttyname);
      pinentry.ttyname = strdup (value);
      if (!pinentry.ttyname)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "ttytype"))
    {
      if (pinentry.ttytype)
	free (pinentry.ttytype);
      pinentry.ttytype = strdup (value);
      if (!pinentry.ttytype)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "lc-ctype"))
    {
      if (pinentry.lc_ctype)
	free (pinentry.lc_ctype);
      pinentry.lc_ctype = strdup (value);
      if (!pinentry.lc_ctype)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "lc-messages"))
    {
      if (pinentry.lc_messages)
	free (pinentry.lc_messages);
      pinentry.lc_messages = strdup (value);
      if (!pinentry.lc_messages)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "parent-wid"))
    {
      pinentry.parent_wid = atoi (value);
      /* FIXME: Use strtol and add some error handling.  */
    }
  else if (!strcmp (key, "touch-file"))
    {
      if (pinentry.touch_file)
        free (pinentry.touch_file);
      pinentry.touch_file = strdup (value);
      if (!pinentry.touch_file)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "default-ok"))
    {
      pinentry.default_ok = strdup (value);
      if (!pinentry.default_ok)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "default-cancel"))
    {
      pinentry.default_cancel = strdup (value);
      if (!pinentry.default_cancel)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "default-prompt"))
    {
      pinentry.default_prompt = strdup (value);
      if (!pinentry.default_prompt)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "default-pwmngr"))
    {
      pinentry.default_pwmngr = strdup (value);
      if (!pinentry.default_pwmngr)
	return gpg_error_from_syserror ();
    }
  else if (!strcmp (key, "allow-external-password-cache") && !*value)
    {
      pinentry.allow_external_password_cache = 1;
      pinentry.tried_password_cache = 0;
    }
  else if (!strcmp (key, "allow-emacs-prompt") && !*value)
    {
      return gpg_error (GPG_ERR_NOT_SUPPORTED);
    }
  else
    return gpg_error (GPG_ERR_UNKNOWN_OPTION);
  return 0;
}


/* Note, that it is sufficient to allocate the target string D as
   long as the source string S, i.e.: strlen(s)+1; */
static void
strcpy_escaped (char *d, const char *s)
{
  while (*s)
    {
      if (*s == '%' && s[1] && s[2])
        {
          s++;
          *d++ = xtoi_2 ( s);
          s += 2;
        }
      else
        *d++ = *s++;
    }
  *d = 0;
}


static gpg_error_t
cmd_setdesc (assuan_context_t ctx, char *line)
{
  char *newd;

  (void)ctx;

  newd = malloc (strlen (line) + 1);
  if (!newd)
    return gpg_error_from_syserror ();

  strcpy_escaped (newd, line);
  if (pinentry.description)
    free (pinentry.description);
  pinentry.description = newd;
  return 0;
}


static gpg_error_t
cmd_setprompt (assuan_context_t ctx, char *line)
{
  char *newp;

  (void)ctx;

  newp = malloc (strlen (line) + 1);
  if (!newp)
    return gpg_error_from_syserror ();

  strcpy_escaped (newp, line);
  if (pinentry.prompt)
    free (pinentry.prompt);
  pinentry.prompt = newp;
  return 0;
}


/* The data provided at LINE may be used by pinentry implementations
   to identify a key for caching strategies of its own.  The empty
   string and --clear mean that the key does not have a stable
   identifier.  */
static gpg_error_t
cmd_setkeyinfo (assuan_context_t ctx, char *line)
{
  (void)ctx;

  if (pinentry.keyinfo)
    free (pinentry.keyinfo);

  if (*line && strcmp(line, "--clear") != 0)
    pinentry.keyinfo = strdup (line);
  else
    pinentry.keyinfo = NULL;

  return 0;
}


static gpg_error_t
cmd_setrepeat (assuan_context_t ctx, char *line)
{
  char *p;

  (void)ctx;

  p = malloc (strlen (line) + 1);
  if (!p)
    return gpg_error_from_syserror ();

  strcpy_escaped (p, line);
  free (pinentry.repeat_passphrase);
  pinentry.repeat_passphrase = p;
  return 0;
}


static gpg_error_t
cmd_setrepeaterror (assuan_context_t ctx, char *line)
{
  char *p;

  (void)ctx;

  p = malloc (strlen (line) + 1);
  if (!p)
    return gpg_error_from_syserror ();

  strcpy_escaped (p, line);
  free (pinentry.repeat_error_string);
  pinentry.repeat_error_string = p;
  return 0;
}


static gpg_error_t
cmd_seterror (assuan_context_t ctx, char *line)
{
  char *newe;

  (void)ctx;

  newe = malloc (strlen (line) + 1);
  if (!newe)
    return gpg_error_from_syserror ();

  strcpy_escaped (newe, line);
  if (pinentry.error)
    free (pinentry.error);
  pinentry.error = newe;
  return 0;
}


static gpg_error_t
cmd_setok (assuan_context_t ctx, char *line)
{
  char *newo;

  (void)ctx;

  newo = malloc (strlen (line) + 1);
  if (!newo)
    return gpg_error_from_syserror ();

  strcpy_escaped (newo, line);
  if (pinentry.ok)
    free (pinentry.ok);
  pinentry.ok = newo;
  return 0;
}


static gpg_error_t
cmd_setnotok (assuan_context_t ctx, char *line)
{
  char *newo;

  (void)ctx;

  newo = malloc (strlen (line) + 1);
  if (!newo)
    return gpg_error_from_syserror ();

  strcpy_escaped (newo, line);
  if (pinentry.notok)
    free (pinentry.notok);
  pinentry.notok = newo;
  return 0;
}


static gpg_error_t
cmd_setcancel (assuan_context_t ctx, char *line)
{
  char *newc;

  (void)ctx;

  newc = malloc (strlen (line) + 1);
  if (!newc)
    return gpg_error_from_syserror ();

  strcpy_escaped (newc, line);
  if (pinentry.cancel)
    free (pinentry.cancel);
  pinentry.cancel = newc;
  return 0;
}


static gpg_error_t
cmd_settimeout (assuan_context_t ctx, char *line)
{
  (void)ctx;

  if (line && *line)
    pinentry.timeout = atoi (line);

  return 0;
}

static gpg_error_t
cmd_settitle (assuan_context_t ctx, char *line)
{
  char *newt;

  (void)ctx;

  newt = malloc (strlen (line) + 1);
  if (!newt)
    return gpg_error_from_syserror ();

  strcpy_escaped (newt, line);
  if (pinentry.title)
    free (pinentry.title);
  pinentry.title = newt;
  return 0;
}

static gpg_error_t
cmd_setqualitybar (assuan_context_t ctx, char *line)
{
  char *newval;

  (void)ctx;

  if (!*line)
    line = "Quality:";

  newval = malloc (strlen (line) + 1);
  if (!newval)
    return gpg_error_from_syserror ();

  strcpy_escaped (newval, line);
  if (pinentry.quality_bar)
    free (pinentry.quality_bar);
  pinentry.quality_bar = newval;
  return 0;
}

/* Set the tooltip to be used for a quality bar.  */
static gpg_error_t
cmd_setqualitybar_tt (assuan_context_t ctx, char *line)
{
  char *newval;

  (void)ctx;

  if (*line)
    {
      newval = malloc (strlen (line) + 1);
      if (!newval)
        return gpg_error_from_syserror ();

      strcpy_escaped (newval, line);
    }
  else
    newval = NULL;
  if (pinentry.quality_bar_tt)
    free (pinentry.quality_bar_tt);
  pinentry.quality_bar_tt = newval;
  return 0;
}


static gpg_error_t
cmd_getpin (assuan_context_t ctx, char *line)
{
  int result;
  int set_prompt = 0;
  int just_read_password_from_cache = 0;

  (void)line;

  pinentry_setbuffer_init (&pinentry);
  if (!pinentry.pin)
    return gpg_error (GPG_ERR_ENOMEM);

  /* Try reading from the password cache.  */
  if (/* If repeat passphrase is set, then we don't want to read from
	 the cache.  */
      ! pinentry.repeat_passphrase
      /* Are we allowed to read from the cache?  */
      && pinentry.allow_external_password_cache
      && pinentry.keyinfo
      /* Only read from the cache if we haven't already tried it.  */
      && ! pinentry.tried_password_cache
      /* If the last read resulted in an error, then don't read from
	 the cache.  */
      && ! pinentry.error)
    {
      char *password;

      pinentry.tried_password_cache = 1;

      password = password_cache_lookup (pinentry.keyinfo);
      if (password)
	/* There is a cached password.  Try it.  */
	{
	  int len = strlen(password) + 1;
	  if (len > pinentry.pin_len)
	    len = pinentry.pin_len;

	  memcpy (pinentry.pin, password, len);
	  pinentry.pin[len] = '\0';

	  secmem_free (password);

	  pinentry.pin_from_cache = 1;

	  assuan_write_status (ctx, "PASSWORD_FROM_CACHE", "");

	  /* Result is the length of the password not including the
	     NUL terminator.  */
	  result = len - 1;

	  just_read_password_from_cache = 1;

	  goto out;
	}
    }

  /* The password was not cached (or we are not allowed to / cannot
     use the cache).  Prompt the user.  */
  pinentry.pin_from_cache = 0;

  if (!pinentry.prompt)
    {
      pinentry.prompt = pinentry.default_prompt?pinentry.default_prompt:"PIN:";
      set_prompt = 1;
    }
  pinentry.locale_err = 0;
  pinentry.specific_err = 0;
  pinentry.close_button = 0;
  pinentry.repeat_okay = 0;
  pinentry.one_button = 0;
  pinentry.ctx_assuan = ctx;
  result = (*pinentry_cmd_handler) (&pinentry);
  pinentry.ctx_assuan = NULL;
  if (pinentry.error)
    {
      free (pinentry.error);
      pinentry.error = NULL;
    }
  if (pinentry.repeat_passphrase)
    {
      free (pinentry.repeat_passphrase);
      pinentry.repeat_passphrase = NULL;
    }
  if (set_prompt)
    pinentry.prompt = NULL;

  pinentry.quality_bar = 0;  /* Reset it after the command.  */

  if (pinentry.close_button)
    assuan_write_status (ctx, "BUTTON_INFO", "close");

  if (result < 0)
    {
      pinentry_setbuffer_clear (&pinentry);
      if (pinentry.specific_err)
        return pinentry.specific_err;
      return (pinentry.locale_err
	      ? gpg_error (GPG_ERR_LOCALE_PROBLEM)
	      : gpg_error (GPG_ERR_CANCELED));
    }

 out:
  if (result)
    {
      if (pinentry.repeat_okay)
        assuan_write_status (ctx, "PIN_REPEATED", "");
      result = assuan_send_data (ctx, pinentry.pin, strlen(pinentry.pin));
      if (!result)
	result = assuan_send_data (ctx, NULL, 0);

      if (/* GPG Agent says it's okay.  */
	  pinentry.allow_external_password_cache && pinentry.keyinfo
	  /* We didn't just read it from the cache.  */
	  && ! just_read_password_from_cache
	  /* And the user said it's okay.  */
	  && pinentry.may_cache_password)
	/* Cache the password.  */
	password_cache_save (pinentry.keyinfo, pinentry.pin);
    }

  pinentry_setbuffer_clear (&pinentry);

  return result;
}


/* Note that the option --one-button is a hack to allow the use of old
   pinentries while the caller is ignoring the result.  Given that
   options have never been used or flagged as an error the new option
   is an easy way to enable the messsage mode while not requiring to
   update pinentry or to have the caller test for the message
   command.  New applications which are free to require an updated
   pinentry should use MESSAGE instead. */
static gpg_error_t
cmd_confirm (assuan_context_t ctx, char *line)
{
  int result;

  pinentry.one_button = !!strstr (line, "--one-button");
  pinentry.quality_bar = 0;
  pinentry.close_button = 0;
  pinentry.locale_err = 0;
  pinentry.specific_err = 0;
  pinentry.canceled = 0;
  pinentry_setbuffer_clear (&pinentry);
  result = (*pinentry_cmd_handler) (&pinentry);
  if (pinentry.error)
    {
      free (pinentry.error);
      pinentry.error = NULL;
    }

  if (pinentry.close_button)
    assuan_write_status (ctx, "BUTTON_INFO", "close");

  if (result)
    return 0;

  if (pinentry.specific_err)
    return pinentry.specific_err;

  if (pinentry.locale_err)
    return gpg_error (GPG_ERR_LOCALE_PROBLEM);

  if (pinentry.one_button)
    return 0;

  if (pinentry.canceled)
    return gpg_error (GPG_ERR_CANCELED);
  return gpg_error (GPG_ERR_NOT_CONFIRMED);
}


static gpg_error_t
cmd_message (assuan_context_t ctx, char *line)
{
  (void)line;

  return cmd_confirm (ctx, "--one-button");
}

/* GETINFO <what>

   Multipurpose function to return a variety of information.
   Supported values for WHAT are:

     version     - Return the version of the program.
     pid         - Return the process id of the server.
 */
static gpg_error_t
cmd_getinfo (assuan_context_t ctx, char *line)
{
  int rc;

  if (!strcmp (line, "version"))
    {
      const char *s = VERSION;
      rc = assuan_send_data (ctx, s, strlen (s));
    }
  else if (!strcmp (line, "pid"))
    {
      char numbuf[50];

      snprintf (numbuf, sizeof numbuf, "%lu", (unsigned long)getpid ());
      rc = assuan_send_data (ctx, numbuf, strlen (numbuf));
    }
  else
    rc = gpg_error (GPG_ERR_ASS_PARAMETER);
  return rc;
}

/* CLEARPASSPHRASE <cacheid>

   Clear the cache passphrase associated with the key identified by
   cacheid.
 */
static gpg_error_t
cmd_clear_passphrase (assuan_context_t ctx, char *line)
{
  (void)ctx;

  if (! line)
    return gpg_error (GPG_ERR_ASS_INV_VALUE);

  /* Remove leading and trailing white space.  */
  while (*line == ' ')
    line ++;
  while (line[strlen (line) - 1] == ' ')
    line[strlen (line) - 1] = 0;

  switch (password_cache_clear (line))
    {
    case 1: return 0;
    case 0: return gpg_error (GPG_ERR_ASS_INV_VALUE);
    default: return gpg_error (GPG_ERR_ASS_GENERAL);
    }
}

/* Tell the assuan library about our commands.  */
static gpg_error_t
register_commands (assuan_context_t ctx)
{
  static struct
  {
    const char *name;
    gpg_error_t (*handler) (assuan_context_t, char *line);
  } table[] =
    {
      { "SETDESC",    cmd_setdesc },
      { "SETPROMPT",  cmd_setprompt },
      { "SETKEYINFO", cmd_setkeyinfo },
      { "SETREPEAT",  cmd_setrepeat },
      { "SETREPEATERROR", cmd_setrepeaterror },
      { "SETERROR",   cmd_seterror },
      { "SETOK",      cmd_setok },
      { "SETNOTOK",   cmd_setnotok },
      { "SETCANCEL",  cmd_setcancel },
      { "GETPIN",     cmd_getpin },
      { "CONFIRM",    cmd_confirm },
      { "MESSAGE",    cmd_message },
      { "SETQUALITYBAR", cmd_setqualitybar },
      { "SETQUALITYBAR_TT", cmd_setqualitybar_tt },
      { "GETINFO",    cmd_getinfo },
      { "SETTITLE",   cmd_settitle },
      { "SETTIMEOUT", cmd_settimeout },
      { "CLEARPASSPHRASE", cmd_clear_passphrase },
      { NULL }
    };
  int i, j;
  gpg_error_t rc;

  for (i = j = 0; table[i].name; i++)
    {
      rc = assuan_register_command (ctx, table[i].name, table[i].handler, NULL);
      if (rc)
        return rc;
    }
  return 0;
}


int
pinentry_loop2 (int infd, int outfd)
{
  gpg_error_t rc;
  assuan_fd_t filedes[2];
  assuan_context_t ctx;

  /* Extra check to make sure we have dropped privs. */
  if (getuid() != geteuid())
    abort ();

  rc = assuan_new (&ctx);
  if (rc)
    {
      fprintf (stderr, "server context creation failed: %s\n",
	       gpg_strerror (rc));
      return -1;
    }

  /* For now we use a simple pipe based server so that we can work
     from scripts.  We will later add options to run as a daemon and
     wait for requests on a Unix domain socket.  */
  filedes[0] = assuan_fdopen (infd);
  filedes[1] = assuan_fdopen (outfd);
  rc = assuan_init_pipe_server (ctx, filedes);
  if (rc)
    {
      fprintf (stderr, "%s: failed to initialize the server: %s\n",
               this_pgmname, gpg_strerror (rc));
      return -1;
    }
  rc = register_commands (ctx);
  if (rc)
    {
      fprintf (stderr, "%s: failed to the register commands with Assuan: %s\n",
               this_pgmname, gpg_strerror (rc));
      return -1;
    }

  assuan_register_option_handler (ctx, option_handler);
  assuan_register_reset_notify (ctx, pinentry_assuan_reset_handler);

  for (;;)
    {
      rc = assuan_accept (ctx);
      if (rc == -1)
          break;
      else if (rc)
        {
          fprintf (stderr, "%s: Assuan accept problem: %s\n",
                   this_pgmname, gpg_strerror (rc));
          break;
        }

      rc = assuan_process (ctx);
      if (rc)
        {
          fprintf (stderr, "%s: Assuan processing failed: %s\n",
                   this_pgmname, gpg_strerror (rc));
          continue;
        }
    }

  assuan_release (ctx);
  return 0;
}


/* Start the pinentry event loop.  The program will start to process
   Assuan commands until it is finished or an error occurs.  If an
   error occurs, -1 is returned.  Otherwise, 0 is returned.  */
int
pinentry_loop (void)
{
  return pinentry_loop2 (STDIN_FILENO, STDOUT_FILENO);
}
