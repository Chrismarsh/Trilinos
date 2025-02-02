
/*
 * Copyright (C) 1991, 1992, 1993, 2020, 2021 by Chris Thewalt (thewalt@ce.berkeley.edu)
 *
 * Permission to use, copy, modify, and distribute this software
 * for any purpose and without fee is hereby granted, provided
 * that the above copyright notices appear in all copies and that both the
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 *
 * Thanks to the following people who have provided enhancements and fixes:
 *   Ron Ueberschaer, Christoph Keller, Scott Schwartz, Steven List,
 *   DaviD W. Sanderson, Goran Bostrom, Michael Gleason, Glenn Kasten,
 *   Edin Hodzic, Eric J Bivona, Kai Uwe Rommel, Danny Quah, Ulrich Betzler
 */

/*
 * Note:  This version has been updated by Mike Gleason <mgleason@ncftp.com>
 */

#if defined(_WIN64) || defined(WIN32) || defined(_WINDOWS) || defined(_MSC_VER)

#define __windows__ 1
#include <conio.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#define NOMINMAX
#include <windows.h>
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define sleep(a) Sleep(a * 1000)
#ifndef S_ISREG
#define S_ISREG(m) (((m)&_S_IFMT) == _S_IFREG)
#define S_ISDIR(m) (((m)&_S_IFMT) == _S_IFDIR)
#endif
#ifndef open
#define open _open
#define write _write
#define read _read
#define close _close
#define lseek _lseek
#define stat _stat
#define lstat _stat
#define fstat _fstat
#define dup _dup
#define utime _utime
#define utimbuf _utimbuf
#endif
#ifndef unlink
#define unlink remove
#endif
#define NO_SIGNALS 1
#define LOCAL_PATH_DELIM '\\'
#define LOCAL_PATH_DELIM_STR "\\"
#define LOCAL_PATH_ALTDELIM '/'
#define IsLocalPathDelim(c) ((c == LOCAL_PATH_DELIM) || (c == LOCAL_PATH_ALTDELIM))
#define UNC_PATH_PREFIX "\\\\"
#define IsUNCPrefixed(s) (IsLocalPathDelim(s[0]) && IsLocalPathDelim(s[1]))
#define pid_t int

#else

#define __unix__ 1
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#define HAVE_TERMIOS_H 1
#ifdef HAVE_TERMIOS_H /* use HAVE_TERMIOS_H interface */
#include <termios.h>
struct termios new_termios, old_termios;
#else /* not HAVE_TERMIOS_H */
#include <sys/ioctl.h>
#ifdef TIOCSETN /* use BSD interface */
#include <sgtty.h>
struct sgttyb  new_tty, old_tty;
struct tchars  tch;
struct ltchars ltch;
#else /* use SYSV interface */
#include <termio.h>
struct termio new_termio, old_termio;
#endif /* TIOCSETN */
#endif /* HAVE_TERMIOS_H */
#define LOCAL_PATH_DELIM '/'
#define LOCAL_PATH_DELIM_STR "/"
#define _StrFindLocalPathDelim(a) strchr(a, LOCAL_PATH_DELIM)
#define _StrRFindLocalPathDelim(a) strrchr(a, LOCAL_PATH_DELIM)
#define IsLocalPathDelim(c) (c == LOCAL_PATH_DELIM)
#endif

/********************* C library headers ********************************/
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern int kill(pid_t pid, int sig);

#define _ap_getline_c_ 1
#include "apr_getline_int.h"

static int ap_gl_tab(char *buf, int offset, int *loc, size_t bufsize);

/******************** external interface *********************************/

ap_gl_in_hook_proc        ap_gl_in_hook                    = NULL;
ap_gl_out_hook_proc       ap_gl_out_hook                   = NULL;
ap_gl_tab_hook_proc       ap_gl_tab_hook                   = ap_gl_tab;
ap_gl_strlen_proc         ap_gl_strlen                     = (ap_gl_strlen_proc)strlen;
ap_gl_tab_completion_proc ap_gl_completion_proc            = NULL;
int                       ap_gl_filename_quoting_desired   = -1; /* default to unspecified */
const char *              ap_gl_filename_quote_characters  = " \t*?<>|;&()[]$`";
int                       ap_gl_ellipses_during_completion = 1;
int                       ap_gl_completion_exact_match_extra_char;
char                      ap_gl_buf[AP_GL_BUF_SIZE]; /* input buffer */

/******************** internal interface *********************************/

static int         ap_gl_init_done = -1;               /* terminal mode flag  */
static int         ap_gl_termw     = 80;               /* actual terminal width */
static int         ap_gl_scroll    = 27;               /* width of EOL scrolling region */
static int         ap_gl_width     = 0;                /* net size available for input */
static int         ap_gl_extent    = 0;                /* how far to redraw, 0 means all */
static int         ap_gl_overwrite = 0;                /* overwrite mode */
static int         ap_gl_pos, ap_gl_cnt = 0;           /* position and size of input */
static char        ap_gl_killbuf[AP_GL_BUF_SIZE] = ""; /* killed text */
static const char *ap_gl_prompt;                       /* to save the prompt string */
static char        ap_gl_intrc        = 0;             /* keyboard SIGINT char */
static char        ap_gl_quitc        = 0;             /* keyboard SIGQUIT char */
static char        ap_gl_suspc        = 0;             /* keyboard SIGTSTP char */
static char        ap_gl_dsuspc       = 0;             /* delayed SIGTSTP char */
static int         ap_gl_search_mode  = 0;             /* search mode flag */
static char **     ap_gl_matchlist    = NULL;
static char *      ap_gl_home_dir     = NULL;
static int         ap_gl_vi_preferred = -1;
static int         ap_gl_vi_mode      = 0;

static void ap_gl_init(void);         /* prepare to edit a line */
static void ap_gl_cleanup(void);      /* to undo ap_gl_init */
static void ap_gl_char_init(void);    /* get ready for no echo input */
static void ap_gl_char_cleanup(void); /* undo ap_gl_char_init */
                                      /* returns printable prompt width */

static void ap_gl_addchar(int c);               /* install specified char */
static void ap_gl_del(int loc, int);            /* del, either left (-1) or cur (0) */
static void ap_gl_error(const char *const buf); /* write error msg and die */
static void ap_gl_fixup(const char *prompt, int change,
                        int cursor); /* fixup state variables and screen */
static int  ap_gl_getc(void);        /* read one char from terminal */
static int  ap_gl_getcx(int);        /* read one char from terminal, if available before timeout */
static void ap_gl_kill(int pos);     /* delete to EOL */
static void ap_gl_newline(void);     /* handle \n or \r */
static void ap_gl_putc(int c);       /* write one char to terminal */
static void ap_gl_puts(const char *const buf); /* write a line to terminal */
static void ap_gl_redraw(void);                /* issue \n and redraw all */
static void ap_gl_transpose(void);             /* transpose two chars */
static void ap_gl_yank(void);                  /* yank killed text */
static void ap_gl_word(int direction);         /* move a word */
static void ap_gl_killword(int direction);

static void  hist_init(void);    /* initializes hist pointers */
static char *hist_next(void);    /* return ptr to next item */
static char *hist_prev(void);    /* return ptr to prev item */
static char *hist_save(char *p); /* makes copy of a string, without NL */

static void search_addchar(int c);       /* increment search string */
static void search_term(void);           /* reset with current contents */
static void search_back(int new_search); /* look back for current string */
static void search_forw(int new_search); /* look forw for current string */
static void ap_gl_beep(void);            /* try to play a system beep sound */

static int ap_gl_do_tab_completion(char *buf, int *loc, size_t bufsize, int tabtab);

static char *copy_string(char *dest, char const *source, long int elements)
{
  char *d;
  for (d = dest; d + 1 < dest + elements && *source; d++, source++) {
    *d = *source;
  }
  *d = '\0';
  return d;
}

/************************ nonportable part *********************************/

#ifdef MSDOS
#include <bios.h>
#endif

static void ap_gl_char_init(void) /* turn off input echo */
{
#ifdef __unix__
#ifdef HAVE_TERMIOS_H /* Use POSIX */
  tcgetattr(0, &old_termios);
  ap_gl_intrc = old_termios.c_cc[VINTR];
  ap_gl_quitc = old_termios.c_cc[VQUIT];
#ifdef VSUSP
  ap_gl_suspc = old_termios.c_cc[VSUSP];
#endif
#ifdef VDSUSP
  ap_gl_dsuspc = old_termios.c_cc[VDSUSP];
#endif
  new_termios = old_termios;
  new_termios.c_iflag &= ~(BRKINT | ISTRIP | IXON | IXOFF);
  new_termios.c_iflag |= (IGNBRK | IGNPAR);
  new_termios.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
  new_termios.c_cc[VMIN]  = 1;
  new_termios.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &new_termios);
#elif defined(TIOCSETN) /* BSD */
  ioctl(0, TIOCGETC, &tch);
  ioctl(0, TIOCGLTC, &ltch);
  ap_gl_intrc  = tch.t_intrc;
  ap_gl_quitc  = tch.t_quitc;
  ap_gl_suspc  = ltch.t_suspc;
  ap_gl_dsuspc = ltch.t_dsuspc;
  ioctl(0, TIOCGETP, &old_tty);
  new_tty = old_tty;
  new_tty.sg_flags |= RAW;
  new_tty.sg_flags &= ~ECHO;
  ioctl(0, TIOCSETN, &new_tty);
#else                   /* SYSV */
  ioctl(0, TCGETA, &old_termio);
  ap_gl_intrc = old_termio.c_cc[VINTR];
  ap_gl_quitc = old_termio.c_cc[VQUIT];
  new_termio  = old_termio;
  new_termio.c_iflag &= ~(BRKINT | ISTRIP | IXON | IXOFF);
  new_termio.c_iflag |= (IGNBRK | IGNPAR);
  new_termio.c_lflag &= ~(ICANON | ISIG | ECHO);
  new_termio.c_cc[VMIN]  = 1;
  new_termio.c_cc[VTIME] = 0;
  ioctl(0, TCSETA, &new_termio);
#endif
#endif /* __unix__ */
}

static void ap_gl_char_cleanup(void) /* undo effects of ap_gl_char_init */
{
#ifdef __unix__
#ifdef HAVE_TERMIOS_H
  tcsetattr(0, TCSANOW, &old_termios);
#elif defined(TIOCSETN) /* BSD */
  ioctl(0, TIOCSETN, &old_tty);
#else                   /* SYSV */
  ioctl(0, TCSETA, &old_termio);
#endif
#endif /* __unix__ */
}

#if defined(MSDOS) || defined(__windows__)

#define K_UP 0x48
#define K_DOWN 0x50
#define K_LEFT 0x4B
#define K_RIGHT 0x4D
#define K_DELETE 0x53
#define K_INSERT 0x52
#define K_HOME 0x47
#define K_END 0x4F
#define K_PGUP 0x49
#define K_PGDN 0x51

int pc_keymap(int c)
{
  switch (c) {
  case K_UP:
  case K_PGUP:
    c = 16; /* up -> ^P */
    break;
  case K_DOWN:
  case K_PGDN:
    c = 14; /* down -> ^N */
    break;
  case K_LEFT:
    c = 2; /* left -> ^B */
    break;
  case K_RIGHT:
    c = 6; /* right -> ^F */
    break;
  case K_END:
    c = 5; /* end -> ^E */
    break;
  case K_HOME:
    c = 1; /* home -> ^A */
    break;
  case K_INSERT:
    c = 15; /* insert -> ^O */
    break;
  case K_DELETE:
    c = 4; /* del -> ^D */
    break;
  default: c = 0; /* make it garbage */
  }
  return c;
}
#endif /* defined(MSDOS) || defined(__windows__) */

static int ap_gl_getc(void)
/* get a character without echoing it to screen */
{
  int c;
#ifdef __unix__
  char ch;
  while ((c = read(0, &ch, 1)) == -1) {
    if (errno != EINTR) {
      break;
    }
  }
  c = (ch <= 0) ? -1 : ch;
#endif /* __unix__ */
#ifdef MSDOS
  c = _bios_keybrd(_NKEYBRD_READ);
  if ((c & 0377) == 224) {
    c = pc_keymap((c >> 8) & 0377);
  }
  else {
    c &= 0377;
  }
#endif /* MSDOS */
#ifdef __windows__
  c = (int)_getch();
  if ((c == 0) || (c == 0xE0)) {
    /* Read key code */
    c = (int)_getch();
    c = pc_keymap(c);
  }
  else if (c == '\r') {
    /* Note: we only get \r from the console,
     * and not a matching \n.
     */
    c = '\n';
  }
#endif
  return c;
}

#ifdef __unix__

static int ap_gl_getcx(int tlen)
/* Get a character without echoing it to screen, timing out
 * after tlen tenths of a second.
 */
{
  for (errno = 0;;) {
    fd_set ss;
    FD_ZERO(&ss);
    FD_SET(0, &ss); /* set STDIN_FILENO */

    struct timeval tv;
    tv.tv_sec  = tlen / 10;
    tv.tv_usec = (tlen % 10) * 100000L;

    int result = select(1, &ss, NULL, NULL, &tv);
    if (result == 1) {
      /* ready */
      break;
    }
    if (result == 0) {
      errno = ETIMEDOUT;
      return (-2);
    }
    else if (errno != EINTR) {
      return (-1);
    }
  }

  for (errno = 0;;) {
    char ch;
    int  c = read(0, &ch, 1);
    if (c == 1) {
      return ((int)ch);
    }
    if (errno != EINTR) {
      break;
    }
  }

  return (-1);
} /* ap_gl_getcx */

#endif /* __unix__ */

#ifdef __windows__

static int ap_gl_getcx(int tlen)
{
  int i, c;

  c = (-2);
  tlen -= 2; /* Adjust for 200ms overhead */
  if (tlen < 1)
    tlen = 1;
  for (i = 0; i < tlen; i++) {
    if (_kbhit()) {
      c = (int)_getch();
      if ((c == 0) || (c == 0xE0)) {
        /* Read key code */
        c = (int)_getch();
        c = pc_keymap(c);
      }
    }
    (void)SleepEx((DWORD)(tlen * 100), FALSE);
  }
  return (c);
} /* ap_gl_getcx */

#endif /* __windows__ */

static void ap_gl_putc(int c)
{
  char ch = (char)(unsigned char)c;

  write(1, &ch, 1);
  if (ch == '\n') {
    ch = '\r';
    write(1, &ch, 1); /* RAW mode needs '\r', does not hurt */
  }
}

/******************** fairly portable part *********************************/

static void ap_gl_puts(const char *const buf)
{
  if (buf) {
    int len = strlen(buf);
    write(1, buf, len);
  }
}

static void ap_gl_error(const char *const buf)
{
  ap_gl_cleanup();
  int len = strlen(buf);
  write(2, buf, len);
  exit(1);
}

static void ap_gl_init(void)
/* set up variables and terminal */
{
  if (ap_gl_init_done < 0) { /* -1 only on startup */
    const char *cp = (const char *)getenv("COLUMNS");
    if (cp != NULL) {
      int w = atoi(cp);
      if (w > 20)
        ap_gl_setwidth(w);
    }
    hist_init();
    ap_gl_completion_proc = ap_gl_local_filename_completion_proc;
  }
  if (isatty(0) == 0 || isatty(1) == 0)
    ap_gl_error("\n*** Error: getline(): not interactive, use stdio.\n");
  ap_gl_char_init();
  ap_gl_init_done = 1;
}

static void ap_gl_cleanup(void)
/* undo effects of ap_gl_init, as necessary */
{
  if (ap_gl_init_done > 0)
    ap_gl_char_cleanup();
  ap_gl_init_done = 0;
#ifdef __windows__
  Sleep(40);
  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#endif
}

void ap_gl_setwidth(int w)
{
  if (w > 250)
    w = 250;
  if (w > 20) {
    ap_gl_termw  = w;
    ap_gl_scroll = w / 3;
  }
  else {
    ap_gl_error("\n*** Error: minimum screen width is 21\n");
  }
}

char *ap_getline_int(char *prompt)
{
  int   c, loc, tmp, lastch;
  int   vi_count, count;
  int   vi_delete;
  char  vi_countbuf[32];
  char *cp;

#ifdef __unix__
  int sig;
#endif

  /* Even if it appears that "vi" is preferred, we
   * don't start in ap_gl_vi_mode.  They need to hit
   * ESC to go into vi command mode.
   */
  ap_gl_vi_mode = 0;
  vi_count      = 0;
  vi_delete     = 0;
  if (ap_gl_vi_preferred < 0) {
    ap_gl_vi_preferred = 0;
    cp                 = (char *)getenv("EDITOR");
    if (cp != NULL)
      ap_gl_vi_preferred = (strstr(cp, "vi") != NULL);
  }

  ap_gl_init();
  ap_gl_prompt = (prompt) ? prompt : "";
  ap_gl_buf[0] = '\0';
  if (ap_gl_in_hook)
    ap_gl_in_hook(ap_gl_buf);
  ap_gl_fixup(ap_gl_prompt, -2, AP_GL_BUF_SIZE);
  lastch = 0;

#ifdef __windows__
  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#endif

  while ((c = ap_gl_getc()) >= 0) {
    ap_gl_extent = 0; /* reset to full extent */
    if (isprint(c)) {
      if (ap_gl_vi_mode > 0) {
      /* "vi" emulation -- far from perfect,
       * but reasonably functional.
       */
      vi:
        for (count = 0;;) {
          if (isdigit(c)) {
            if (vi_countbuf[sizeof(vi_countbuf) - 2] == '\0')
              vi_countbuf[strlen(vi_countbuf)] = (char)c;
          }
          else if (vi_countbuf[0] != '\0') {
            vi_count = atoi(vi_countbuf);
            memset(vi_countbuf, 0, sizeof(vi_countbuf));
          }
          switch (c) {
          case 'b': ap_gl_word(-1); break;
          case 'w':
            if (vi_delete) {
              ap_gl_killword(1);
            }
            else {
              ap_gl_word(1);
            }
            break;
          case 'h': /* left */
            if (vi_delete) {
              if (ap_gl_pos > 0) {
                ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos - 1);
                ap_gl_del(0, 1);
              }
            }
            else {
              ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos - 1);
            }
            break;
          case ' ':
          case 'l': /* right */
            if (vi_delete) {
              ap_gl_del(0, 1);
            }
            else {
              ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos + 1);
            }
            break;
          case 'k': /* up */
            copy_string(ap_gl_buf, hist_prev(), AP_GL_BUF_SIZE);
            if (ap_gl_in_hook)
              ap_gl_in_hook(ap_gl_buf);
            ap_gl_fixup(ap_gl_prompt, 0, AP_GL_BUF_SIZE);
            break;
          case 'j': /* down */
            copy_string(ap_gl_buf, hist_next(), AP_GL_BUF_SIZE);
            if (ap_gl_in_hook)
              ap_gl_in_hook(ap_gl_buf);
            ap_gl_fixup(ap_gl_prompt, 0, AP_GL_BUF_SIZE);
            break;
          case 'd':
            if (vi_delete == 1) {
              ap_gl_kill(0);
              vi_count      = 1;
              vi_delete     = 0;
              ap_gl_vi_mode = 0;
              goto vi_break;
            }
            else {
              vi_delete = 1;
              goto vi_break;
            }
            break;
          case '^': /* start of line */
            if (vi_delete) {
              vi_count = ap_gl_pos;
              ap_gl_fixup(ap_gl_prompt, -1, 0);
              for (c = 0; c < vi_count; c++) {
                if (ap_gl_cnt > 0)
                  ap_gl_del(0, 0);
              }
              vi_count  = 1;
              vi_delete = 0;
            }
            else {
              ap_gl_fixup(ap_gl_prompt, -1, 0);
            }
            break;
          case '$': /* end of line */
            if (vi_delete) {
              ap_gl_kill(ap_gl_pos);
            }
            else {
              loc = (int)strlen(ap_gl_buf);
              if (loc > 1)
                loc--;
              ap_gl_fixup(ap_gl_prompt, -1, loc);
            }
            break;
          case 'p': /* paste after */
            ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos + 1);
            ap_gl_yank();
            break;
          case 'P': /* paste before */ ap_gl_yank(); break;
          case 'r': /* replace character */
            ap_gl_buf[ap_gl_pos] = (char)ap_gl_getc();
            ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos);
            vi_count = 1;
            break;
          case 'R':
            ap_gl_overwrite = 1;
            ap_gl_vi_mode   = 0;
            break;
          case 'i':
          case 'I':
            ap_gl_overwrite = 0;
            ap_gl_vi_mode   = 0;
            break;
          case 'o':
          case 'O':
          case 'a':
          case 'A':
            ap_gl_overwrite = 0;
            ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos + 1);
            ap_gl_vi_mode = 0;
            break;
          }
          count++;
          if (count >= vi_count)
            break;
        }
        vi_count  = 1;
        vi_delete = 0;
      vi_break:
        continue;
      }
      else if (ap_gl_search_mode) {
        search_addchar(c);
      }
      else {
        ap_gl_addchar(c);
      }
    }
    else {
      if (ap_gl_search_mode) {
        if (c == '\033' || c == '\016' || c == '\020') {
          search_term();
          c = 0; /* ignore the character */
        }
        else if (c == '\010' || c == '\177') {
          search_addchar(-1); /* unwind search string */
          c = 0;
        }
        else if (c != '\022' && c != '\023') {
          search_term(); /* terminate and handle char */
        }
      }
      switch (c) {
      case '\n':
      case '\r': /* newline */
        ap_gl_newline();
        ap_gl_cleanup();
        return ap_gl_buf;
        /*NOTREACHED*/
        break;
      case '\001':
        ap_gl_fixup(ap_gl_prompt, -1, 0); /* ^A */
        break;
      case '\002':
        ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos - 1); /* ^B */
        break;
      case '\004': /* ^D */
        if (ap_gl_cnt == 0) {
          ap_gl_buf[0] = '\0';
          ap_gl_cleanup();
          ap_gl_putc('\n');
          return ap_gl_buf;
        }
        else {
          ap_gl_del(0, 1);
        }
        break;
      case '\005':
        ap_gl_fixup(ap_gl_prompt, -1, ap_gl_cnt); /* ^E */
        break;
      case '\006':
        ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos + 1); /* ^F */
        break;
      case '\010':
      case '\177':
        ap_gl_del(-1, 0); /* ^H and DEL */
        break;
      case '\t': /* TAB */
        if (ap_gl_completion_proc) {
          tmp                              = ap_gl_pos;
          ap_gl_buf[sizeof(ap_gl_buf) - 1] = '\0';
          loc = ap_gl_do_tab_completion(ap_gl_buf, &tmp, sizeof(ap_gl_buf), (lastch == '\t'));
          ap_gl_buf[sizeof(ap_gl_buf) - 1] = '\0';
          if (loc >= 0 || tmp != ap_gl_pos)
            ap_gl_fixup(ap_gl_prompt, /* loc */ -2, tmp);
          if (lastch == '\t') {
            c      = 0;
            lastch = 0;
          }
        }
        else if (ap_gl_tab_hook) {
          tmp                              = ap_gl_pos;
          ap_gl_buf[sizeof(ap_gl_buf) - 1] = '\0';
          loc = ap_gl_tab_hook(ap_gl_buf, (int)ap_gl_strlen(ap_gl_prompt), &tmp, sizeof(ap_gl_buf));
          ap_gl_buf[sizeof(ap_gl_buf) - 1] = '\0';
          if (loc >= 0 || tmp != ap_gl_pos)
            ap_gl_fixup(ap_gl_prompt, loc, tmp);
        }
        break;
      case '\013':
        ap_gl_kill(ap_gl_pos); /* ^K */
        break;
      case '\014':
        ap_gl_redraw(); /* ^L */
        break;
      case '\016': /* ^N */
        copy_string(ap_gl_buf, hist_next(), AP_GL_BUF_SIZE);
        if (ap_gl_in_hook)
          ap_gl_in_hook(ap_gl_buf);
        ap_gl_fixup(ap_gl_prompt, 0, AP_GL_BUF_SIZE);
        break;
      case '\017':
        ap_gl_overwrite = !ap_gl_overwrite; /* ^O */
        break;
      case '\020': /* ^P */
        copy_string(ap_gl_buf, hist_prev(), AP_GL_BUF_SIZE);
        if (ap_gl_in_hook)
          ap_gl_in_hook(ap_gl_buf);
        ap_gl_fixup(ap_gl_prompt, 0, AP_GL_BUF_SIZE);
        break;
      case '\022':
        search_back(1); /* ^R */
        break;
      case '\023':
        search_forw(1); /* ^S */
        break;
      case '\024':
        ap_gl_transpose(); /* ^T */
        break;
      case '\025':
        ap_gl_kill(0); /* ^U */
        break;
      case '\031':
        ap_gl_yank(); /* ^Y */
        break;
      case '\033': /* ansi arrow keys */
        c = ap_gl_getcx(3);
        if (c == '[') {
          switch (c = ap_gl_getc()) {
          case 'A': /* up */
            copy_string(ap_gl_buf, hist_prev(), AP_GL_BUF_SIZE);
            if (ap_gl_in_hook)
              ap_gl_in_hook(ap_gl_buf);
            ap_gl_fixup(ap_gl_prompt, 0, AP_GL_BUF_SIZE);
            break;
          case 'B': /* down */
            copy_string(ap_gl_buf, hist_next(), AP_GL_BUF_SIZE);
            if (ap_gl_in_hook)
              ap_gl_in_hook(ap_gl_buf);
            ap_gl_fixup(ap_gl_prompt, 0, AP_GL_BUF_SIZE);
            break;
          case 'C':
            ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos + 1); /* right */
            break;
          case 'D':
            ap_gl_fixup(ap_gl_prompt, -1, ap_gl_pos - 1); /* left */
            break;
          default:
            ap_gl_beep(); /* who knows */
            break;
          }
        }
        else if ((ap_gl_vi_preferred == 0) && ((c == 'f') || (c == 'F'))) {
          ap_gl_word(1);
        }
        else if ((ap_gl_vi_preferred == 0) && ((c == 'b') || (c == 'B'))) {
          ap_gl_word(-1);
        }
        else {
          /* enter vi command mode */
          if (ap_gl_vi_mode == 0) {
            ap_gl_vi_mode = 1;
            vi_count      = 1;
            vi_delete     = 0;
            memset(vi_countbuf, 0, sizeof(vi_countbuf));
            if (ap_gl_pos > 0)
              ap_gl_fixup(ap_gl_prompt, -2, ap_gl_pos - 1); /* left 1 char */
            /* Don't bother if the line is empty. */
            if (ap_gl_cnt > 0) {
              /* We still have to use the char read! */
              goto vi;
            }
            ap_gl_vi_mode = 0;
          }
          ap_gl_beep();
        }
        break;
      default: /* check for a terminal signal */
#ifdef __unix__
        if (c > 0) { /* ignore 0 (reset above) */
          sig = 0;
#ifdef SIGINT
          if (c == ap_gl_intrc)
            sig = SIGINT;
#endif
#ifdef SIGQUIT
          if (c == ap_gl_quitc)
            sig = SIGQUIT;
#endif
#ifdef SIGTSTP
          if (c == ap_gl_suspc || c == ap_gl_dsuspc)
            sig = SIGTSTP;
#endif
          if (sig != 0) {
            ap_gl_cleanup();
            kill(0, sig);
            ap_gl_init();
            ap_gl_redraw();
            c = 0;
          }
        }
#endif /* __unix__ */
        if (c > 0)
          ap_gl_beep();
        break;
      }
    }
    if (c > 0)
      lastch = c;
  }
  ap_gl_cleanup();
  ap_gl_buf[0] = '\0';
  return ap_gl_buf;
}

static void ap_gl_addchar(int c)

/* adds the character c to the input buffer at current location */
{
  if (ap_gl_cnt >= AP_GL_BUF_SIZE - 1)
    ap_gl_error("\n*** Error: getline(): input buffer overflow\n");
  if (ap_gl_overwrite == 0 || ap_gl_pos == ap_gl_cnt) {
    int i;
    for (i = ap_gl_cnt; i >= ap_gl_pos; i--)
      ap_gl_buf[i + 1] = ap_gl_buf[i];
    ap_gl_buf[ap_gl_pos] = (char)c;
    ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos + 1);
  }
  else {
    ap_gl_buf[ap_gl_pos] = (char)c;
    ap_gl_extent         = 1;
    ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos + 1);
  }
}

static void ap_gl_yank(void)
/* adds the kill buffer to the input buffer at current location */
{
  int len = strlen(ap_gl_killbuf);
  if (len > 0) {
    if (ap_gl_overwrite == 0) {
      if (ap_gl_cnt + len >= AP_GL_BUF_SIZE - 1)
        ap_gl_error("\n*** Error: getline(): input buffer overflow\n");
      for (int i = ap_gl_cnt; i >= ap_gl_pos; i--)
        ap_gl_buf[i + len] = ap_gl_buf[i];
      for (int i = 0; i < len; i++)
        ap_gl_buf[ap_gl_pos + i] = ap_gl_killbuf[i];
      ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos + len);
    }
    else {
      if (ap_gl_pos + len > ap_gl_cnt) {
        if (ap_gl_pos + len >= AP_GL_BUF_SIZE - 1)
          ap_gl_error("\n*** Error: getline(): input buffer overflow\n");
        ap_gl_buf[ap_gl_pos + len] = 0;
      }
      for (int i = 0; i < len; i++)
        ap_gl_buf[ap_gl_pos + i] = ap_gl_killbuf[i];
      ap_gl_extent = len;
      ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos + len);
    }
  }
  else
    ap_gl_beep();
}

static void ap_gl_transpose(void)
/* switch character under cursor and to left of cursor */
{
  if (ap_gl_pos > 0 && ap_gl_cnt > ap_gl_pos) {
    int c                    = ap_gl_buf[ap_gl_pos - 1];
    ap_gl_buf[ap_gl_pos - 1] = ap_gl_buf[ap_gl_pos];
    ap_gl_buf[ap_gl_pos]     = (char)c;
    ap_gl_extent             = 2;
    ap_gl_fixup(ap_gl_prompt, ap_gl_pos - 1, ap_gl_pos);
  }
  else
    ap_gl_beep();
}

static void ap_gl_newline(void)
/*
 * Cleans up entire line before returning to caller. A \n is appended.
 * If line longer than screen, we redraw starting at beginning
 */
{
  int change = ap_gl_cnt;
  int len    = ap_gl_cnt;
  int loc    = ap_gl_width - 5; /* shifts line back to start position */

  if (ap_gl_cnt >= AP_GL_BUF_SIZE - 1)
    ap_gl_error("\n*** Error: getline(): input buffer overflow\n");
  if (ap_gl_out_hook) {
    change = ap_gl_out_hook(ap_gl_buf);
    len    = strlen(ap_gl_buf);
  }
  if (loc > len)
    loc = len;
  ap_gl_fixup(ap_gl_prompt, change, loc); /* must do this before appending \n */
  ap_gl_buf[len]     = '\n';
  ap_gl_buf[len + 1] = '\0';
  ap_gl_putc('\n');
}

static void ap_gl_del(int loc, int killsave)

/*
 * Delete a character.  The loc variable can be:
 *    -1 : delete character to left of cursor
 *     0 : delete character under cursor
 */
{
  if ((loc == -1 && ap_gl_pos > 0) || (loc == 0 && ap_gl_pos < ap_gl_cnt)) {
    for (int j = 0, i = ap_gl_pos + loc; i < ap_gl_cnt; i++) {
      if ((j == 0) && (killsave != 0) && (ap_gl_vi_mode != 0)) {
        ap_gl_killbuf[0] = ap_gl_buf[i];
        ap_gl_killbuf[1] = '\0';
        j                = 1;
      }
      ap_gl_buf[i] = ap_gl_buf[i + 1];
    }
    ap_gl_fixup(ap_gl_prompt, ap_gl_pos + loc, ap_gl_pos + loc);
  }
  else
    ap_gl_beep();
}

static void ap_gl_kill(int pos)

/* delete from pos to the end of line */
{
  if (pos < ap_gl_cnt) {
    copy_string(ap_gl_killbuf, ap_gl_buf + pos, AP_GL_BUF_SIZE);
    ap_gl_buf[pos] = '\0';
    ap_gl_fixup(ap_gl_prompt, pos, pos);
  }
  else
    ap_gl_beep();
}

static void ap_gl_killword(int direction)
{
  int pos = ap_gl_pos;
  if (direction > 0) { /* forward */
    while (pos < ap_gl_cnt && !isspace(ap_gl_buf[pos]))
      pos++;
    while (pos < ap_gl_cnt && isspace(ap_gl_buf[pos]))
      pos++;
  }
  else { /* backward */
    if (pos > 0)
      pos--;
    while (pos > 0 && isspace(ap_gl_buf[pos]))
      pos--;
    while (pos > 0 && !isspace(ap_gl_buf[pos]))
      pos--;
    if (pos < ap_gl_cnt && isspace(ap_gl_buf[pos])) /* move onto word */
      pos++;
  }

  int startpos = ap_gl_pos;
  if (pos < startpos) {
    int tmp  = pos;
    pos      = startpos;
    startpos = tmp;
  }
  memcpy(ap_gl_killbuf, ap_gl_buf + startpos, (size_t)(pos - startpos));
  ap_gl_killbuf[pos - startpos] = '\0';
  if (isspace(ap_gl_killbuf[pos - startpos - 1]))
    ap_gl_killbuf[pos - startpos - 1] = '\0';
  ap_gl_fixup(ap_gl_prompt, -1, startpos);
  for (int i = 0, tmp = pos - startpos; i < tmp; i++)
    ap_gl_del(0, 0);
} /* ap_gl_killword */

static void ap_gl_word(int direction)

/* move forward or backward one word */
{
  int pos = ap_gl_pos;

  if (direction > 0) { /* forward */
    while (pos < ap_gl_cnt && !isspace(ap_gl_buf[pos]))
      pos++;
    while (pos < ap_gl_cnt && isspace(ap_gl_buf[pos]))
      pos++;
  }
  else { /* backward */
    if (pos > 0)
      pos--;
    while (pos > 0 && isspace(ap_gl_buf[pos]))
      pos--;
    while (pos > 0 && !isspace(ap_gl_buf[pos]))
      pos--;
    if (pos < ap_gl_cnt && isspace(ap_gl_buf[pos])) /* move onto word */
      pos++;
  }
  ap_gl_fixup(ap_gl_prompt, -1, pos);
}

static void ap_gl_redraw(void)
/* emit a newline, reset and redraw prompt and current input line */
{
  if (ap_gl_init_done > 0) {
    ap_gl_putc('\n');
    ap_gl_fixup(ap_gl_prompt, -2, ap_gl_pos);
  }
}

static void ap_gl_fixup(const char *prompt, int change, int cursor)

/*
 * This function is used both for redrawing when input changes or for
 * moving within the input line.  The parameters are:
 *   prompt:  compared to last_prompt[] for changes;
 *   change : the index of the start of changes in the input buffer,
 *            with -1 indicating no changes, -2 indicating we're on
 *            a new line, redraw everything.
 *   cursor : the desired location of the cursor after the call.
 *            A value of AP_GL_BUF_SIZE can be used  to indicate the cursor should
 *            move just past the end of the input line.
 */
{
  static int  ap_gl_shift; /* index of first on screen character */
  static int  off_right;   /* true if more text right of screen */
  static int  off_left;    /* true if more text left of screen */
  static char last_prompt[80] = "";
  int         left = 0, right = -1; /* bounds for redraw */
  int         pad;                  /* how much to erase at end of line */
  int         backup;               /* how far to backup before fixing */
  int         new_shift;            /* value of shift based on cursor */
  int         extra;                /* adjusts when shift (scroll) happens */
  int         i;
  int         new_right = -1; /* alternate right bound, using ap_gl_extent */
  int         l1, l2;

  if (change == -2) { /* reset */
    ap_gl_pos = ap_gl_cnt = ap_gl_shift = off_right = off_left = 0;
    ap_gl_putc('\r');
    ap_gl_puts(prompt);
    copy_string(last_prompt, prompt, 80);
    change      = 0;
    ap_gl_width = ap_gl_termw - ap_gl_strlen(prompt);
  }
  else if (strcmp(prompt, last_prompt) != 0) {
    l1        = ap_gl_strlen(last_prompt);
    l2        = ap_gl_strlen(prompt);
    ap_gl_cnt = ap_gl_cnt + l1 - l2;
    copy_string(last_prompt, prompt, 80);
    ap_gl_putc('\r');
    ap_gl_puts(prompt);
    ap_gl_pos   = ap_gl_shift;
    ap_gl_width = ap_gl_termw - l2;
    change      = 0;
  }
  pad    = (off_right) ? ap_gl_width - 1 : ap_gl_cnt - ap_gl_shift; /* old length */
  backup = ap_gl_pos - ap_gl_shift;
  if (change >= 0) {
    ap_gl_cnt = strlen(ap_gl_buf);
    if (change > ap_gl_cnt)
      change = ap_gl_cnt;
  }
  if (cursor > ap_gl_cnt) {
    if (cursor != AP_GL_BUF_SIZE) { /* AP_GL_BUF_SIZE means end of line */
      if (ap_gl_ellipses_during_completion == 0) {
        ap_gl_beep();
      }
    }
    cursor = ap_gl_cnt;
  }
  if (cursor < 0) {
    ap_gl_beep();
    cursor = 0;
  }
  if (off_right || (off_left && cursor < ap_gl_shift + ap_gl_width - ap_gl_scroll / 2))
    extra = 2; /* shift the scrolling boundary */
  else
    extra = 0;
  new_shift = cursor + extra + ap_gl_scroll - ap_gl_width;
  if (new_shift > 0) {
    new_shift /= ap_gl_scroll;
    new_shift *= ap_gl_scroll;
  }
  else
    new_shift = 0;
  if (new_shift != ap_gl_shift) { /* scroll occurs */
    ap_gl_shift = new_shift;
    off_left    = (ap_gl_shift) ? 1 : 0;
    off_right   = (ap_gl_cnt > ap_gl_shift + ap_gl_width - 1) ? 1 : 0;
    left        = ap_gl_shift;
    new_right = right = (off_right) ? ap_gl_shift + ap_gl_width - 2 : ap_gl_cnt;
  }
  else if (change >= 0) { /* no scroll, but text changed */
    if (change < ap_gl_shift + off_left) {
      left = ap_gl_shift;
    }
    else {
      left   = change;
      backup = ap_gl_pos - change;
    }
    off_right = (ap_gl_cnt > ap_gl_shift + ap_gl_width - 1) ? 1 : 0;
    right     = (off_right) ? ap_gl_shift + ap_gl_width - 2 : ap_gl_cnt;
    new_right = (ap_gl_extent && (right > left + ap_gl_extent)) ? left + ap_gl_extent : right;
  }
  pad -= (off_right) ? ap_gl_width - 1 : ap_gl_cnt - ap_gl_shift;
  pad = (pad < 0) ? 0 : pad;
  if (left <= right) { /* clean up screen */
    for (i = 0; i < backup; i++)
      ap_gl_putc('\b');
    if (left == ap_gl_shift && off_left) {
      ap_gl_putc('$');
      left++;
    }
    for (i = left; i < new_right; i++)
      ap_gl_putc(ap_gl_buf[i]);
    ap_gl_pos = new_right;
    if (off_right && new_right == right) {
      ap_gl_putc('$');
      ap_gl_pos++;
    }
    else {
      for (i = 0; i < pad; i++) /* erase remains of prev line */
        ap_gl_putc(' ');
      ap_gl_pos += pad;
    }
  }
  i = ap_gl_pos - cursor; /* move to final cursor location */
  if (i > 0) {
    while (i--)
      ap_gl_putc('\b');
  }
  else {
    for (i = ap_gl_pos; i < cursor; i++)
      ap_gl_putc(ap_gl_buf[i]);
  }
  ap_gl_pos = cursor;
}

static int ap_gl_tab(char *buf, int offset, int *loc, size_t bufsize)
/* default tab handler, acts like tabstops every 8 cols */
{
  int len   = strlen(buf);
  int count = 8 - (offset + *loc) % 8;
  int i;
  for (i = len; i >= *loc; i--)
    if (i + count < (int)bufsize)
      buf[i + count] = buf[i];
  for (i = 0; i < count; i++)
    if (*loc + i < (int)bufsize)
      buf[*loc + i] = ' ';
  i    = *loc;
  *loc = i + count;
  return i;
}

/******************* History stuff **************************************/

#ifndef HIST_SIZE
#define HIST_SIZE 100
#endif

static int   hist_pos = 0, hist_last = 0;
static char *hist_buf[HIST_SIZE];
static char  hist_empty_elem[2] = "";

static void hist_init(void)
{
  hist_buf[0] = hist_empty_elem;
  for (int i = 1; i < HIST_SIZE; i++)
    hist_buf[i] = (char *)0;
}

void ap_gl_histadd(char *buf)
{
  static char *prev = NULL;

  /* in case we call ap_gl_histadd() before we call getline() */
  if (ap_gl_init_done < 0) { /* -1 only on startup */
    hist_init();
    ap_gl_init_done = 0;
  }
  char *p = buf;
  while (*p == ' ' || *p == '\t' || *p == '\n')
    p++;
  if (*p) {
    int len = strlen(buf);
    if (strchr(p, '\n')) /* previously line already has NL stripped */
      len--;
    if ((prev == NULL) || ((int)strlen(prev) != len) || strncmp(prev, buf, (size_t)len) != 0) {
      hist_buf[hist_last] = hist_save(buf);
      prev                = hist_buf[hist_last];
      hist_last           = (hist_last + 1) % HIST_SIZE;
      if (hist_buf[hist_last] && *hist_buf[hist_last]) {
        free(hist_buf[hist_last]);
      }
      hist_buf[hist_last] = hist_empty_elem;
    }
  }
  hist_pos = hist_last;
}

static char *hist_prev(void)
/* loads previous hist entry into input buffer, sticks on first */
{
  char *p    = NULL;
  int   next = (hist_pos - 1 + HIST_SIZE) % HIST_SIZE;

  if (hist_buf[hist_pos] != NULL && next != hist_last) {
    hist_pos = next;
    p        = hist_buf[hist_pos];
  }
  if (p == NULL) {
    p = hist_empty_elem;
    ap_gl_beep();
  }
  return p;
}

static char *hist_next(void)
/* loads next hist entry into input buffer, clears on last */
{
  char *p = NULL;

  if (hist_pos != hist_last) {
    hist_pos = (hist_pos + 1) % HIST_SIZE;
    p        = hist_buf[hist_pos];
  }
  if (p == NULL) {
    p = hist_empty_elem;
    ap_gl_beep();
  }
  return p;
}

static char *hist_save(char *p)

/* makes a copy of the string */
{
  char * s   = NULL;
  size_t len = strlen(p);
  char * nl  = strpbrk(p, "\n\r");

  if (nl) {
    if ((s = (char *)malloc(len)) != NULL) {
      copy_string(s, p, len);
      s[len - 1] = '\0';
    }
  }
  else {
    if ((s = (char *)malloc(len + 1)) != NULL) {
      copy_string(s, p, len + 1);
    }
  }
  if (s == NULL)
    ap_gl_error("\n*** Error: hist_save() failed on malloc\n");
  return s;
}

void ap_gl_histsavefile(const char *const path)
{
  FILE *fp = fopen(path,
#if defined(__windows__) || defined(MSDOS)
                   "wt"
#else
                   "w"
#endif
  );
  if (fp != NULL) {
    for (int i = 2; i < HIST_SIZE; i++) {
      int         j = (hist_pos + i) % HIST_SIZE;
      const char *p = hist_buf[j];
      if ((p == NULL) || (*p == '\0'))
        continue;
      fprintf(fp, "%s\n", p);
    }
    fclose(fp);
  }
} /* ap_gl_histsavefile */

void ap_gl_histloadfile(const char *const path)
{
  FILE *fp = fopen(path,
#if defined(__windows__) || defined(MSDOS)
                   "rt"
#else
                   "r"
#endif
  );
  if (fp != NULL) {
    char line[256];
    memset(line, 0, sizeof(line));
    while (fgets(line, sizeof(line) - 2, fp) != NULL) {
      ap_gl_histadd(line);
    }
    fclose(fp);
  }
} /* ap_gl_histloadfile */

/******************* Search stuff **************************************/

static char search_prompt[101]; /* prompt includes search string */
static char search_string[100];
static int  search_pos      = 0; /* current location in search_string */
static int  search_forw_flg = 0; /* search direction flag */
static int  search_last     = 0; /* last match found */

static void search_update(int c)
{
  if (c == 0) {
    search_pos       = 0;
    search_string[0] = 0;
    search_prompt[0] = '?';
    search_prompt[1] = ' ';
    search_prompt[2] = 0;
  }
  else if (c > 0) {
    search_string[search_pos]     = (char)c;
    search_string[search_pos + 1] = (char)0;
    search_prompt[search_pos]     = (char)c;
    search_prompt[search_pos + 1] = (char)'?';
    search_prompt[search_pos + 2] = (char)' ';
    search_prompt[search_pos + 3] = (char)0;
    search_pos++;
  }
  else {
    if (search_pos > 0) {
      search_pos--;
      search_string[search_pos]     = (char)0;
      search_prompt[search_pos]     = (char)'?';
      search_prompt[search_pos + 1] = (char)' ';
      search_prompt[search_pos + 2] = (char)0;
    }
    else {
      ap_gl_beep();
      hist_pos = hist_last;
    }
  }
}

static void search_addchar(int c)
{
  search_update(c);
  if (c < 0) {
    if (search_pos > 0) {
      hist_pos = search_last;
    }
    else {
      ap_gl_buf[0] = '\0';
      hist_pos     = hist_last;
    }
    copy_string(ap_gl_buf, hist_buf[hist_pos], AP_GL_BUF_SIZE);
  }
  char *loc;
  if ((loc = strstr(ap_gl_buf, search_string)) != NULL) {
    ap_gl_fixup(search_prompt, 0, loc - ap_gl_buf);
  }
  else if (search_pos > 0) {
    if (search_forw_flg) {
      search_forw(0);
    }
    else {
      search_back(0);
    }
  }
  else {
    ap_gl_fixup(search_prompt, 0, 0);
  }
}

static void search_term(void)
{
  ap_gl_search_mode = 0;
  if (ap_gl_buf[0] == '\0') /* not found, reset hist list */
    hist_pos = hist_last;
  if (ap_gl_in_hook)
    ap_gl_in_hook(ap_gl_buf);
  ap_gl_fixup(ap_gl_prompt, 0, ap_gl_pos);
}

static void search_back(int new_search)
{
  int   found = 0;
  char *loc;

  search_forw_flg = 0;
  if (ap_gl_search_mode == 0) {
    search_last = hist_pos = hist_last;
    search_update(0);
    ap_gl_search_mode = 1;
    ap_gl_buf[0]      = '\0';
    ap_gl_fixup(search_prompt, 0, 0);
  }
  else if (search_pos > 0) {
    while (!found) {
      char *p = hist_prev();
      if (*p == 0) { /* not found, done looking */
        ap_gl_buf[0] = '\0';
        ap_gl_fixup(search_prompt, 0, 0);
        found = 1;
      }
      else if ((loc = strstr(p, search_string)) != NULL) {
        copy_string(ap_gl_buf, p, AP_GL_BUF_SIZE);
        ap_gl_fixup(search_prompt, 0, loc - p);
        if (new_search)
          search_last = hist_pos;
        found = 1;
      }
    }
  }
  else {
    ap_gl_beep();
  }
}

static void search_forw(int new_search)
{
  int   found = 0;
  char *loc;

  search_forw_flg = 1;
  if (ap_gl_search_mode == 0) {
    search_last = hist_pos = hist_last;
    search_update(0);
    ap_gl_search_mode = 1;
    ap_gl_buf[0]      = '\0';
    ap_gl_fixup(search_prompt, 0, 0);
  }
  else if (search_pos > 0) {
    while (!found) {
      char *p = hist_next();
      if (*p == 0) { /* not found, done looking */
        ap_gl_buf[0] = '\0';
        ap_gl_fixup(search_prompt, 0, 0);
        found = 1;
      }
      else if ((loc = strstr(p, search_string)) != NULL) {
        copy_string(ap_gl_buf, p, AP_GL_BUF_SIZE);
        ap_gl_fixup(search_prompt, 0, loc - p);
        if (new_search)
          search_last = hist_pos;
        found = 1;
      }
    }
  }
  else {
    ap_gl_beep();
  }
}

static void ap_gl_beep(void)
{
#ifdef __windows__
  MessageBeep(MB_OK);
#else
  ap_gl_putc('\007');
#endif
} /* ap_gl_beep */

static int ap_gl_do_tab_completion(char *buf, int *loc, size_t bufsize, int tabtab)
{
  /* Zero out the rest of the buffer, so we can move stuff around
   * and know we'll still be NUL-terminated.
   */
  size_t llen = strlen(buf);
  memset(buf + llen, 0, bufsize - llen);
  bufsize -= 4; /* leave room for a NUL, space, and two quotes. */
  char * curposp        = buf + *loc;
  int    wasateol       = (*curposp == '\0');
  size_t lenaftercursor = llen - (curposp - buf);
  if (ap_gl_ellipses_during_completion != 0) {
    char ellipsessave[4];
    memcpy(ellipsessave, curposp, (size_t)4);
    memcpy(curposp, "... ", (size_t)4);
    ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos + 3);
    memcpy(curposp, ellipsessave, (size_t)4);
  }

  int   qmode          = 0;
  char *qstart         = NULL;
  char *lastspacestart = NULL;
  char *matchpfx       = NULL;

  char *cp = buf;
  while (cp < curposp) {
    int c = (int)*cp++;
    if (c == '\0')
      break;
    if ((c == '"') || (c == '\'')) {
      if (qmode == c) {
        /* closing quote; end it. */
        qstart = NULL;
        qmode  = 0;
      }
      else if (qmode != 0) {
        /* just treat it as a regular char. */
      }
      else {
        /* start new quote group. */
        qmode  = c;
        qstart = cp - 1;
      }
    }
    else if ((isspace(c)) && (qmode == 0)) {
      /* found a non-quoted space. */
      lastspacestart = cp - 1;
    }
    else {
      /* regular char */
    }
  }

  char *startp;
  if (qstart != NULL)
    startp = qstart + 1;
  else if (lastspacestart != NULL)
    startp = lastspacestart + 1;
  else
    startp = buf;

  cp          = startp;
  size_t mlen = (curposp - cp);

  matchpfx = (char *)malloc(mlen + 1);
  memcpy(matchpfx, cp, mlen);
  matchpfx[mlen] = '\0';

#define AP_GL_COMPLETE_VECTOR_BLOCK_SIZE 64

  int    nused              = 0;
  int    ntoalloc           = AP_GL_COMPLETE_VECTOR_BLOCK_SIZE;
  char **newap_gl_matchlist = (char **)malloc((size_t)(sizeof(char *) * (ntoalloc + 1)));
  if (newap_gl_matchlist == NULL) {
    free(matchpfx);
    ap_gl_beep();
    return 0;
  }
  ap_gl_matchlist = newap_gl_matchlist;
  int nalloced    = ntoalloc;
  for (int i = nused; i <= nalloced; i++)
    ap_gl_matchlist[i] = NULL;

  ap_gl_completion_exact_match_extra_char = ' ';
  for (;; nused++) {
    if (nused == nalloced) {
      ntoalloc += AP_GL_COMPLETE_VECTOR_BLOCK_SIZE;
      newap_gl_matchlist =
          (char **)realloc((char *)ap_gl_matchlist, (size_t)(sizeof(char *) * (ntoalloc + 1)));
      if (newap_gl_matchlist == NULL) {
        /* not enough memory to expand list -- abort */
        for (int i = 0; i < nused; i++)
          free(ap_gl_matchlist[i]);
        free(ap_gl_matchlist);
        ap_gl_matchlist = NULL;
        ap_gl_beep();
        free(matchpfx);
        return 0;
      }
      ap_gl_matchlist = newap_gl_matchlist;
      nalloced        = ntoalloc;
      for (int i = nused; i <= nalloced; i++)
        ap_gl_matchlist[i] = NULL;
    }
    cp                     = ap_gl_completion_proc(matchpfx, nused);
    ap_gl_matchlist[nused] = cp;
    if (cp == NULL)
      break;
  }

  if (ap_gl_ellipses_during_completion != 0) {
    ap_gl_fixup(ap_gl_prompt, ap_gl_pos, ap_gl_pos);
    ap_gl_puts("    ");
  }

  /* We now have an array strings, whose last element is NULL. */
  char *strtoadd  = NULL;
  char *strtoadd1 = NULL;

  int addquotes = (ap_gl_filename_quoting_desired > 0) ||
                  ((ap_gl_filename_quoting_desired < 0) &&
                   (ap_gl_completion_proc == ap_gl_local_filename_completion_proc));

  if (nused == 1) {
    /* Exactly one match. */
    strtoadd = ap_gl_matchlist[0];
  }
  else if ((nused > 1) && (mlen > 0)) {
    /* Find the greatest amount that matches. */
    size_t glen = 1;
    for (;; glen++) {
      int allmatch = 1;
      for (int i = 1; i < nused; i++) {
        if (ap_gl_matchlist[0][glen] != ap_gl_matchlist[i][glen]) {
          allmatch = 0;
          break;
        }
      }
      if (allmatch == 0)
        break;
    }
    strtoadd1 = (char *)malloc(glen + 1);
    if (strtoadd1 != NULL) {
      memcpy(strtoadd1, ap_gl_matchlist[0], glen);
      strtoadd1[glen] = '\0';
      strtoadd        = strtoadd1;
    }
  }

  if (strtoadd != NULL) {
    if ((qmode == 0) && (addquotes != 0)) {
      if (strpbrk(strtoadd, ap_gl_filename_quote_characters) != NULL) {
        qmode = (strchr(strtoadd, '"') == NULL) ? '"' : '\'';
        memmove(curposp + 1, curposp, lenaftercursor + 1 /* NUL */);
        curposp++;
        *startp++ = (char)qmode;
      }
    }
    size_t startoff = (size_t)(startp - buf);
    size_t amt      = strlen(strtoadd);
    if ((amt + startoff + lenaftercursor) >= bufsize)
      amt = bufsize - (amt + startoff + lenaftercursor);
    memmove(curposp + amt - mlen, curposp, lenaftercursor + 1 /* NUL */);
    curposp += amt - mlen;
    memcpy(startp, strtoadd, amt);
    if (nused == 1) {
      /* Exact match. */
      if (qmode != 0) {
        /* Finish the quoting. */
        memmove(curposp + 1, curposp, lenaftercursor + 1 /* NUL */);
        curposp++;
        buf[amt + startoff] = (char)qmode;
        amt++;
      }
      memmove(curposp + 1, curposp, lenaftercursor + 1 /* NUL */);
      curposp++;
      buf[amt + startoff] = (char)ap_gl_completion_exact_match_extra_char;
      amt++;
    }
    else if ((!wasateol) && (!isspace(*curposp))) {
      /* Not a full match, but insert a
       * space for better readability.
       */
      memmove(curposp + 1, curposp, lenaftercursor + 1 /* NUL */);
      curposp++;
      buf[amt + startoff] = ' ';
    }
    *loc = (int)(startoff + amt);

    if (strtoadd1 != NULL)
      free(strtoadd1);
  }

  /* Don't need this any more. */
  for (int i = 0; i < nused; i++)
    free(ap_gl_matchlist[i]);
  free(ap_gl_matchlist);
  ap_gl_matchlist = NULL;
  free(matchpfx);

  return 0;
} /* ap_gl_do_tab_completion */

void ap_gl_tab_completion(ap_gl_tab_completion_proc proc)
{
  if (proc == NULL)
    proc = ap_gl_local_filename_completion_proc; /* default proc */
  ap_gl_completion_proc = proc;
} /* ap_gl_tab_completion */

#ifndef _StrFindLocalPathDelim
static char *_StrRFindLocalPathDelim(const char *src) /* TODO: optimize */
{
  const char *last = NULL;
  for (;;) {
    int c = *src++;
    if (c == '\0')
      break;
    if (IsLocalPathDelim(c))
      last = src - 1;
  }

  return ((char *)last);
} /* StrRFindLocalPathDelim */
#endif /* Windows */

void ap_gl_set_home_dir(const char *homedir)
{
  if (ap_gl_home_dir != NULL) {
    free(ap_gl_home_dir);
    ap_gl_home_dir = NULL;
  }

  if (homedir == NULL) {
#ifdef __windows__
    const char *homedrive = getenv("HOMEDRIVE");
    const char *homepath  = getenv("HOMEPATH");
    if ((homedrive != NULL) && (homepath != NULL)) {
      size_t len     = strlen(homedrive) + strlen(homepath) + 1;
      ap_gl_home_dir = (char *)malloc(len);
      if (ap_gl_home_dir != NULL) {
        copy_string(ap_gl_home_dir, homedrive, len);
        strcat(ap_gl_home_dir, homepath);
        return;
      }
    }

    char wdir[64];
    wdir[0] = '\0';
    if (GetWindowsDirectory(wdir, sizeof(wdir) - 1) < 1)
      (void)copy_string(wdir, ".", sizeof(wdir));
    else if (wdir[1] == ':') {
      wdir[2] = '\\';
      wdir[3] = '\0';
    }
    homedir = wdir;
#else
    char *cp = (char *)getlogin();
    if (cp == NULL) {
      cp = (char *)getenv("LOGNAME");
      if (cp == NULL)
        cp = (char *)getenv("USER");
    }
    struct passwd *pw = NULL;
    if (cp != NULL)
      pw = getpwnam(cp);
    if (pw == NULL)
      pw = getpwuid(getuid());
    if (pw == NULL)
      return; /* hell with it */
    homedir = pw->pw_dir;
#endif
  }

  size_t len     = strlen(homedir) + /* NUL */ 1;
  ap_gl_home_dir = (char *)malloc(len);
  if (ap_gl_home_dir != NULL) {
    memcpy(ap_gl_home_dir, homedir, len);
  }
} /* ap_gl_set_home_dir */

#ifdef __unix__

char *ap_gl_local_filename_completion_proc(const char *start, int idx)
{
  static DIR *  dir = NULL;
  static int    filepfxoffset;
  static size_t filepfxlen;

  const char *filepfx;

  if (idx == 0) {
    if (dir != NULL) {
      /* shouldn't get here! */
      closedir(dir);
      dir = NULL;
    }
  }

  if (dir == NULL) {
    char *      dirtoopen1 = NULL;
    char *      cp         = _StrRFindLocalPathDelim(start);
    const char *dirtoopen;
    if (cp == start) {
      dirtoopen     = LOCAL_PATH_DELIM_STR; /* root dir */
      filepfxoffset = 1;
    }
    else if (cp == NULL) {
      dirtoopen     = ".";
      filepfxoffset = 0;
    }
    else {
      size_t len = strlen(start) + 1;
      dirtoopen1 = (char *)malloc(len);
      if (dirtoopen1 == NULL)
        return NULL;
      memcpy(dirtoopen1, start, len);
      len             = (cp - start);
      dirtoopen1[len] = '\0';
      dirtoopen       = dirtoopen1;
      filepfxoffset   = (int)((cp + 1) - start);
    }

    if (strcmp(dirtoopen, "~") == 0) {
      if (ap_gl_home_dir == NULL)
        ap_gl_set_home_dir(NULL);
      if (ap_gl_home_dir == NULL) {
        if (dirtoopen1 != NULL)
          free(dirtoopen1);
        return (NULL);
      }
      dirtoopen = ap_gl_home_dir;
    }

    dir = opendir(dirtoopen);
    if (dirtoopen1 != NULL)
      free(dirtoopen1);

    filepfx    = start + filepfxoffset;
    filepfxlen = strlen(filepfx);
  }

  if (dir != NULL) {
    /* assumes "start" is same for each iteration. */
    filepfx = start + filepfxoffset;

    for (;;) {
      struct dirent *dent = readdir(dir);
      if (dent == NULL) {
        /* no more items */
        closedir(dir);
        dir = NULL;

        if (idx == 1) {
          /* There was exactly one match.
           * In this special case, we
           * want to append a / instead
           * of a space.
           */
          char *cp = ap_gl_matchlist[0];
          if ((cp[0] == '~') && ((cp[1] == '\0') || (IsLocalPathDelim(cp[1])))) {
            size_t len  = strlen(cp + 1) + /* NUL */ 1;
            size_t len2 = strlen(ap_gl_home_dir);
            if (IsLocalPathDelim(ap_gl_home_dir[len2 - 1]))
              len2--;
            cp = (char *)realloc(ap_gl_matchlist[0], len + len2);
            if (cp == NULL) {
              cp = ap_gl_matchlist[0];
            }
            else {
              memmove(cp + len2, cp + 1, len);
              memcpy(cp, ap_gl_home_dir, len2);
              ap_gl_matchlist[0] = cp;
            }
          }
          struct stat st;
          if ((stat(cp, &st) == 0) && (S_ISDIR(st.st_mode)))
            ap_gl_completion_exact_match_extra_char = LOCAL_PATH_DELIM;
        }
        return NULL;
      }

      const char *name = dent->d_name;
      if ((name[0] == '.') && ((name[1] == '\0') || ((name[1] == '.') && (name[2] == '\0'))))
        continue; /* Skip . and .. */

      if ((filepfxlen == 0) || (strncmp(name, filepfx, filepfxlen) == 0)) {
        /* match */
        size_t len = strlen(name);
        char * cp  = (char *)malloc(filepfxoffset + len + 1 /* spare */ + 1 /* NUL */);
        *cp        = '\0';
        if (filepfxoffset > 0)
          memcpy(cp, start, (size_t)filepfxoffset);
        memcpy(cp + filepfxoffset, name, len + 1);
        return (cp);
      }
    }
  }

  return NULL;
} /* ap_gl_local_filename_completion_proc */

#endif /* __unix__ */

#ifdef __windows__

char *ap_gl_local_filename_completion_proc(const char *start, int idx)
{
  static HANDLE searchHandle = NULL;
  static int    filepfxoffset;
  static size_t filepfxlen;

  WIN32_FIND_DATA ffd;
  DWORD           dwErr;
  char *          cp, *c2, ch;
  const char *    filepfx;
  const char *    dirtoopen, *name;
  char *          dirtoopen1, *dirtoopen2;
  size_t          len, len2;

  if (idx == 0) {
    if (searchHandle != NULL) {
      /* shouldn't get here! */
      FindClose(searchHandle);
      searchHandle = NULL;
    }
  }

  if (searchHandle == NULL) {
    dirtoopen1 = NULL;
    dirtoopen2 = NULL;
    cp         = _StrRFindLocalPathDelim(start);
    if (cp == start) {
      dirtoopen     = LOCAL_PATH_DELIM_STR; /* root dir */
      filepfxoffset = 1;
    }
    else if (cp == NULL) {
      dirtoopen     = ".";
      filepfxoffset = 0;
    }
    else {
      len        = strlen(start) + 1;
      dirtoopen1 = (char *)malloc(len);
      if (dirtoopen1 == NULL)
        return NULL;
      memcpy(dirtoopen1, start, len);
      len             = (cp - start);
      dirtoopen1[len] = '\0';
      dirtoopen       = dirtoopen1;
      filepfxoffset   = (int)((cp + 1) - start);
    }

    if (strcmp(dirtoopen, "~") == 0) {
      if (ap_gl_home_dir == NULL)
        ap_gl_set_home_dir(NULL);
      if (ap_gl_home_dir == NULL)
        return (NULL);
      dirtoopen = ap_gl_home_dir;
    }

    len        = strlen(dirtoopen);
    dirtoopen2 = (char *)malloc(len + 8);
    if (dirtoopen2 == NULL) {
      if (dirtoopen1 != NULL)
        free(dirtoopen1);
      return NULL;
    }

    memcpy(dirtoopen2, dirtoopen, len + 1);
    if (dirtoopen2[len - 1] == LOCAL_PATH_DELIM)
      memcpy(dirtoopen2 + len, "*.*", (size_t)4);
    else
      memcpy(dirtoopen2 + len, "\\*.*", (size_t)5);

    /* "Open" the directory. */
    memset(&ffd, 0, sizeof(ffd));
    searchHandle = FindFirstFile(dirtoopen2, &ffd);

    free(dirtoopen2);
    if (dirtoopen1 != NULL)
      free(dirtoopen1);

    if (searchHandle == INVALID_HANDLE_VALUE) {
      return NULL;
    }

    filepfx    = start + filepfxoffset;
    filepfxlen = strlen(filepfx);
  }
  else {
    /* assumes "start" is same for each iteration. */
    filepfx = start + filepfxoffset;
    goto next;
  }

  for (;;) {

    name = ffd.cFileName;
    if ((name[0] == '.') && ((name[1] == '\0') || ((name[1] == '.') && (name[2] == '\0'))))
      goto next; /* Skip . and .. */

    if ((filepfxlen == 0) || (strnicmp(name, filepfx, filepfxlen) == 0)) {
      /* match */
      len = strlen(name);
      cp  = (char *)malloc(filepfxoffset + len + 4 /* spare */ + 1 /* NUL */);
      *cp = '\0';
      if (filepfxoffset > 0)
        memcpy(cp, start, filepfxoffset);
      memcpy(cp + filepfxoffset, name, len + 1);
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        /* Embed file type with name. */
        c2    = cp + filepfxoffset + len + 1;
        *c2++ = '\0';
        *c2++ = 'd';
        *c2   = '\0';
      }
      else {
        c2    = cp + filepfxoffset + len + 1;
        *c2++ = '\0';
        *c2++ = '-';
        *c2   = '\0';
      }
      return (cp);
    }

  next:
    if (!FindNextFile(searchHandle, &ffd)) {
      dwErr = GetLastError();
      if (dwErr != ERROR_NO_MORE_FILES) {
        FindClose(searchHandle);
        searchHandle = NULL;
        return NULL;
      }

      /* no more items */
      FindClose(searchHandle);
      searchHandle = NULL;

      if (idx == 1) {
        /* There was exactly one match.
         * In this special case, we
         * want to append a \ instead
         * of a space.
         */
        cp = ap_gl_matchlist[0];
        ch = (char)cp[strlen(cp) + 2];
        if (ch == (char)'d')
          ap_gl_completion_exact_match_extra_char = LOCAL_PATH_DELIM;

        if ((cp[0] == '~') && ((cp[1] == '\0') || (IsLocalPathDelim(cp[1])))) {
          len  = strlen(cp + 1) + /* NUL */ 1;
          len2 = strlen(ap_gl_home_dir);
          if (IsLocalPathDelim(ap_gl_home_dir[len2 - 1]))
            len2--;
          cp = (char *)realloc(ap_gl_matchlist[0], len + len2 + 4);
          if (cp == NULL) {
            cp = ap_gl_matchlist[0];
          }
          else {
            memmove(cp + len2, cp + 1, len);
            memcpy(cp, ap_gl_home_dir, len2);
            c2                 = cp + len + len2;
            *c2++              = '\0';
            *c2++              = ch;
            *c2                = '\0';
            ap_gl_matchlist[0] = cp;
          }
        }
      }
      break;
    }
  }
  return (NULL);
} /* ap_gl_local_filename_completion_proc */

char *ap_gl_win_getpass(const char *const prompt, char *const pass, int dsize)
{
  char *cp;

  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
  ZeroMemory(pass, (DWORD)sizeof(dsize));
  dsize--;

  if ((prompt != NULL) && (prompt[0] != '\0'))
    _cputs(prompt);

  for (cp = pass;;) {
    int c = (int)_getch();
    if ((c == '\r') || (c == '\n'))
      break;
    if ((c == '\010') || (c == '\177')) {
      /* ^H and DEL */
      if (cp > pass) {
        *--cp = '\0';
        _putch('\010');
        _putch(' ');
        _putch('\010');
      }
    }
    else if (cp < (pass + dsize)) {
      _putch('*');
      *cp++ = c;
    }
  }
  _putch('\r');
  _putch('\n');
  Sleep(40);
  FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));

  *cp = '\0';
  return (pass);
} /* ap_gl_getpass */

#endif /* __windows__ */
