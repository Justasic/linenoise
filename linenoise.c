/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#include "linenoise.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
static char *						unsupported_term[]	 = {"dumb", "cons25", "emacs", NULL};
static LinenoiseCompletionCallback *l_CompletionCallback = NULL;
static LinenoiseHintsCallback *		l_HintsCallback		 = NULL;
static LinenoiseFreeHintsCallback * l_FreeHintsCallback	 = NULL;

enum KEY_ACTION
{
	KEY_NULL  = 0,	/* NULL */
	CTRL_A	  = 1,	/* Ctrl+a */
	CTRL_B	  = 2,	/* Ctrl-b */
	CTRL_C	  = 3,	/* Ctrl-c */
	CTRL_D	  = 4,	/* Ctrl-d */
	CTRL_E	  = 5,	/* Ctrl-e */
	CTRL_F	  = 6,	/* Ctrl-f */
	CTRL_H	  = 8,	/* Ctrl-h */
	TAB		  = 9,	/* Tab */
	CTRL_K	  = 11, /* Ctrl+k */
	CTRL_L	  = 12, /* Ctrl+l */
	ENTER	  = 13, /* Enter */
	CTRL_N	  = 14, /* Ctrl-n */
	CTRL_P	  = 16, /* Ctrl-p */
	CTRL_T	  = 20, /* Ctrl-t */
	CTRL_U	  = 21, /* Ctrl+u */
	CTRL_W	  = 23, /* Ctrl+w */
	ESC		  = 27, /* Escape */
	BACKSPACE = 127 /* Backspace */
};

static void RefreshLine(struct LinenoiseState *l);

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#	define lndebug(...)                                                               \
		do                                                                             \
		{                                                                              \
			if (lndebug_fp == NULL)                                                    \
			{                                                                          \
				lndebug_fp = fopen("/tmp/lndebug.txt", "a");                           \
				fprintf(lndebug_fp,                                                    \
						"[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
						(int)l->len,                                                   \
						(int)l->pos,                                                   \
						(int)l->oldpos,                                                \
						plen,                                                          \
						rows,                                                          \
						rpos,                                                          \
						(int)l->maxrows,                                               \
						old_rows);                                                     \
			}                                                                          \
			fprintf(lndebug_fp, ", " __VA_ARGS__);                                     \
			fflush(lndebug_fp);                                                        \
		} while (0)
#else
#	define lndebug(fmt, ...)
#endif

/* ======================= Low level terminal handling ====================== */

/* Set if to use or not the multi line mode. */
void LinenoiseSetMultiLine(LinenoiseState *ls, int ml) { ls->mlmode = ml; }

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int IsUnsupportedTerm(void)
{
	char *term = getenv("TERM");
	int	  j;

	if (term == NULL)
		return 0;

	for (j = 0; unsupported_term[j]; j++)
	{
		if (!strcasecmp(term, unsupported_term[j]))
			return 1;
	}

	return 0;
}

/* Raw mode: 1960 magic shit. */
static int EnableRawMode(LinenoiseState *ls, int fd)
{
	struct termios raw;

	if (!isatty(fd))
		goto fatal;

	if (tcgetattr(fd, &ls->orig_termios) == -1)
		goto fatal;

	memcpy(&raw, &ls->orig_termios, sizeof(struct termios)); /* modify the original mode */
	/* input modes: no break, no CR to NL, no parity check, no strip char,
	 * no start/stop output control. */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/* output modes - disable post processing */
	raw.c_oflag &= ~(OPOST);
	/* control modes - set 8 bit chars */
	raw.c_cflag |= (CS8);
	/* local modes - choing off, canonical off, no extended functions,
	 * no signal chars (^Z,^C) */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* control chars - set return condition: min number of bytes and timer.
	 * We want read to return every single byte, without timeout. */
	raw.c_cc[VMIN]	= 1;
	raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

	/* put terminal in raw mode after flushing */
	if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
		goto fatal;

	ls->rawmode = false;
	return 0;

fatal:
	errno = ENOTTY;
	return -1;
}

static void DisableRawMode(LinenoiseState *ls, int fd)
{

	/* Don't even check the return value as it's too late. */
	if (ls->rawmode && tcsetattr(fd, TCSAFLUSH, &ls->orig_termios) != -1)
		ls->rawmode = 0;
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int GetCursorPosition(int ifd, int ofd)
{
	char		 buf[32];
	int			 cols, rows;
	unsigned int i = 0;

	/* Report cursor location */
	if (write(ofd, "\x1b[6n", 4) != 4)
		return -1;

	/* Read the response: ESC [ rows ; cols R */
	while (i < sizeof(buf) - 1)
	{
		if (read(ifd, buf + i, 1) != 1)
			break;

		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	/* Parse it. */
	if (buf[0] != ESC || buf[1] != '[')
		return -1;

	if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
		return -1;

	return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int GetColumns(int ifd, int ofd)
{
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		/* ioctl() failed. Try to query the terminal itself. */
		int start, cols;

		/* Get the initial position so we can restore it later. */
		start = GetCursorPosition(ifd, ofd);
		if (start == -1)
			goto failed;

		/* Go to right margin and get position. */
		if (write(ofd, "\x1b[999C", 6) != 6)
			goto failed;

		cols = GetCursorPosition(ifd, ofd);
		if (cols == -1)
			goto failed;

		/* Restore position. */
		if (cols > start)
		{
			char seq[32];
			snprintf(seq, 32, "\x1b[%dD", cols - start);
			if (write(ofd, seq, strlen(seq)) == -1)
			{
				/* Can't recover... */
			}
		}
		return cols;
	}
	else
		return ws.ws_col;

failed:
	return 80;
}

/* Clear the screen. Used to handle ctrl+l */
void LinenoiseClearScreen(const LinenoiseState *ls)
{
	if (write(ls->ofd, "\x1b[H\x1b[2J", 7) <= 0)
	{
		/* nothing to do, just to avoid warning. */
	}
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void LinenoiseBeep(const LinenoiseState *ls)
{
	dprintf(ls->efd, "\x7");
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void FreeCompletions(LinenoiseCompletions *lc)
{
	size_t i;
	for (i = 0; i < lc->len; i++)
		free(lc->cvec[i]);

	if (lc->cvec != NULL)
		free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int CompleteLine(struct LinenoiseState *ls)
{
	LinenoiseCompletions lc = {0, NULL};
	int					 nread, nwritten;
	char				 c = 0;

	l_CompletionCallback(ls->buf, &lc);
	if (lc.len == 0)
		LinenoiseBeep(ls);
	else
	{
		size_t stop = 0, i = 0;

		while (!stop)
		{
			/* Show completion or original buffer */
			if (i < lc.len)
			{
				struct LinenoiseState saved = *ls;

				ls->len = ls->pos = strlen(lc.cvec[i]);
				ls->buf			  = lc.cvec[i];
				RefreshLine(ls);
				ls->len = saved.len;
				ls->pos = saved.pos;
				ls->buf = saved.buf;
			}
			else
				RefreshLine(ls);

			nread = read(ls->ifd, &c, 1);
			if (nread <= 0)
			{
				FreeCompletions(&lc);
				return -1;
			}

			switch (c)
			{
				case 9: /* tab */
					i = (i + 1) % (lc.len + 1);
					if (i == lc.len)
						LinenoiseBeep(ls);
					break;
				case 27: /* escape */
					/* Re-show original buffer */
					if (i < lc.len)
						RefreshLine(ls);
					stop = 1;
					break;
				default:
					/* Update buffer and return */
					if (i < lc.len)
					{
						nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
						ls->len = ls->pos = nwritten;
					}
					stop = 1;
					break;
			}
		}
	}

	FreeCompletions(&lc);
	return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void LinenoiseSetCompletionCallback(LinenoiseCompletionCallback *fn) { l_CompletionCallback = fn; }

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void LinenoiseSetHintsCallback(LinenoiseHintsCallback *fn) { l_HintsCallback = fn; }

/* Register a function to free the hints returned by the hints callback
 * registered with LinenoiseSetHintsCallback(). */
void LinenoiseSetFreeHintsCallback(LinenoiseFreeHintsCallback *fn) { l_FreeHintsCallback = fn; }

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void LinenoiseAddCompletion(LinenoiseCompletions *lc, const char *str)
{
	size_t len = strlen(str);
	char * copy, **cvec;

	copy = malloc(len + 1);
	if (copy == NULL)
		return;

	memcpy(copy, str, len + 1);
	cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
	if (cvec == NULL)
	{
		free(copy);
		return;
	}

	lc->cvec			= cvec;
	lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf
{
	char *b;
	int	  len;
};

static void abInit(struct abuf *ab)
{
	ab->b	= NULL;
	ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL)
		return;
	memcpy(new + ab->len, s, len);
	ab->b = new;
	ab->len += len;
}

static void abFree(struct abuf *ab) { free(ab->b); }

/* Helper of refreshSingleLine() and refreshMultiLine() to show hints
 * to the right of the prompt. */
void RefreshShowHints(struct abuf *ab, struct LinenoiseState *l, int plen)
{
	char seq[64];
	if (l_HintsCallback && plen + l->len < l->cols)
	{
		int	  color = -1, bold = 0;
		char *hint = l_HintsCallback(l->buf, &color, &bold);
		if (hint)
		{
			int hintlen	   = strlen(hint);
			int hintmaxlen = l->cols - (plen + l->len);

			if (hintlen > hintmaxlen)
				hintlen = hintmaxlen;
			if (bold == 1 && color == -1)
				color = 37;
			if (color != -1 || bold != 0)
				snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
			else
				seq[0] = '\0';

			abAppend(ab, seq, strlen(seq));
			abAppend(ab, hint, hintlen);

			if (color != -1 || bold != 0)
				abAppend(ab, "\033[0m", 4);
			/* Call the function to free the hint returned. */
			if (l_FreeHintsCallback)
				l_FreeHintsCallback(hint);
		}
	}
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void RefreshSingleLine(struct LinenoiseState *l)
{
	char		seq[64];
	size_t		plen = strlen(l->prompt);
	int			fd	 = l->ofd;
	char *		buf	 = l->buf;
	size_t		len	 = l->len;
	size_t		pos	 = l->pos;
	struct abuf ab;

	while ((plen + pos) >= l->cols)
	{
		buf++;
		len--;
		pos--;
	}

	while (plen + len > l->cols)
	{
		len--;
	}

	abInit(&ab);
	/* Cursor to left edge */
	snprintf(seq, 64, "\r");
	abAppend(&ab, seq, strlen(seq));
	/* Write the prompt and the current buffer content */
	abAppend(&ab, l->prompt, strlen(l->prompt));
	abAppend(&ab, buf, len);
	/* Show hits if any. */
	RefreshShowHints(&ab, l, plen);
	/* Erase to right */
	snprintf(seq, 64, "\x1b[0K");
	abAppend(&ab, seq, strlen(seq));
	/* Move cursor to original position. */
	snprintf(seq, 64, "\r\x1b[%dC", (int)(pos + plen));
	abAppend(&ab, seq, strlen(seq));

	write(fd, ab.b, ab.len);

	abFree(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void RefreshMultiLine(struct LinenoiseState *l)
{
	char		seq[64];
	int			plen = strlen(l->prompt);
	int			rows = (plen + l->len + l->cols - 1) / l->cols; /* rows used by current buf. */
	int			rpos = (plen + l->oldpos + l->cols) / l->cols;	/* cursor relative row. */
	int			rpos2;											/* rpos after refresh. */
	int			col;											/* colum position, zero-based. */
	int			old_rows = l->maxrows;
	int			fd		 = l->ofd, j;
	struct abuf ab;

	/* Update maxrows if needed. */
	if (rows > (int)l->maxrows)
		l->maxrows = rows;

	/* First step: clear all the lines used before. To do so start by
	 * going to the last row. */
	abInit(&ab);
	if (old_rows - rpos > 0)
	{
		lndebug("go down %d", old_rows - rpos);
		snprintf(seq, 64, "\x1b[%dB", old_rows - rpos);
		abAppend(&ab, seq, strlen(seq));
	}

	/* Now for every row clear it, go up. */
	for (j = 0; j < old_rows - 1; j++)
	{
		lndebug("clear+up");
		snprintf(seq, 64, "\r\x1b[0K\x1b[1A");
		abAppend(&ab, seq, strlen(seq));
	}

	/* Clean the top line. */
	lndebug("clear");
	snprintf(seq, 64, "\r\x1b[0K");
	abAppend(&ab, seq, strlen(seq));

	/* Write the prompt and the current buffer content */
	abAppend(&ab, l->prompt, strlen(l->prompt));
	abAppend(&ab, l->buf, l->len);

	/* Show hits if any. */
	RefreshShowHints(&ab, l, plen);

	/* If we are at the very end of the screen with our prompt, we need to
	 * emit a newline and move the prompt to the first column. */
	if (l->pos && l->pos == l->len && (l->pos + plen) % l->cols == 0)
	{
		lndebug("<newline>");
		abAppend(&ab, "\n", 1);
		snprintf(seq, 64, "\r");
		abAppend(&ab, seq, strlen(seq));
		rows++;

		if (rows > (int)l->maxrows)
			l->maxrows = rows;
	}

	/* Move cursor to right position. */
	rpos2 = (plen + l->pos + l->cols) / l->cols; /* current cursor relative row. */
	lndebug("rpos2 %d", rpos2);

	/* Go up till we reach the expected positon. */
	if (rows - rpos2 > 0)
	{
		lndebug("go-up %d", rows - rpos2);
		snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
		abAppend(&ab, seq, strlen(seq));
	}

	/* Set column. */
	col = (plen + (int)l->pos) % (int)l->cols;
	lndebug("set col %d", 1 + col);

	if (col)
		snprintf(seq, 64, "\r\x1b[%dC", col);
	else
		snprintf(seq, 64, "\r");
	abAppend(&ab, seq, strlen(seq));

	lndebug("\n");
	l->oldpos = l->pos;

	if (write(fd, ab.b, ab.len) == -1)
		; /* Can't recover from write error. */

	abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void RefreshLine(struct LinenoiseState *l)
{
	if (l->mlmode)
		RefreshMultiLine(l);
	else
		RefreshSingleLine(l);
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int LinenoiseEditInsert(struct LinenoiseState *l, char c)
{
	if (l->len < l->buflen)
	{
		if (l->len == l->pos)
		{
			l->buf[l->pos] = c;
			l->pos++;
			l->len++;
			l->buf[l->len] = '\0';
			if ((!l->mlmode && l->plen + l->len < l->cols && !l_HintsCallback))
			{
				/* Avoid a full update of the line in the
				 * trivial case. */
				if (write(l->ofd, &c, 1) == -1)
					return -1;
			}
			else
				RefreshLine(l);
		}
		else
		{
			memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
			l->buf[l->pos] = c;
			l->len++;
			l->pos++;
			l->buf[l->len] = '\0';
			RefreshLine(l);
		}
	}
	return 0;
}

/* Move cursor on the left. */
void LinenoiseEditMoveLeft(struct LinenoiseState *l)
{
	if (l->pos > 0)
	{
		l->pos--;
		RefreshLine(l);
	}
}

/* Move cursor on the right. */
void LinenoiseEditMoveRight(struct LinenoiseState *l)
{
	if (l->pos != l->len)
	{
		l->pos++;
		RefreshLine(l);
	}
}

/* Move cursor to the start of the line. */
void LinenoiseEditMoveHome(struct LinenoiseState *l)
{
	if (l->pos != 0)
	{
		l->pos = 0;
		RefreshLine(l);
	}
}

/* Move cursor to the end of the line. */
void LinenoiseEditMoveEnd(struct LinenoiseState *l)
{
	if (l->pos != l->len)
	{
		l->pos = l->len;
		RefreshLine(l);
	}
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void LinenoiseEditHistoryNext(struct LinenoiseState *l, int dir)
{
	if (l->history_len > 1)
	{
		/* Update the current history entry before to
		 * overwrite it with the next one. */
		free(l->history[l->history_len - 1 - l->history_index]);
		l->history[l->history_len - 1 - l->history_index] = strdup(l->buf);
		/* Show the new entry */
		l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
		if (l->history_index < 0)
		{
			l->history_index = 0;
			return;
		}
		else if (l->history_index >= l->history_len)
		{
			l->history_index = l->history_len - 1;
			return;
		}

		strncpy(l->buf, l->history[l->history_len - 1 - l->history_index], l->buflen);
		l->buf[l->buflen - 1] = '\0';
		l->len = l->pos = strlen(l->buf);
		RefreshLine(l);
	}
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void LinenoiseEditDelete(struct LinenoiseState *l)
{
	if (l->len > 0 && l->pos < l->len)
	{
		memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
		l->len--;
		l->buf[l->len] = '\0';
		RefreshLine(l);
	}
}

/* Backspace implementation. */
void LinenoiseEditBackspace(struct LinenoiseState *l)
{
	if (l->pos > 0 && l->len > 0)
	{
		memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
		l->pos--;
		l->len--;
		l->buf[l->len] = '\0';
		RefreshLine(l);
	}
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void LinenoiseEditDeletePrevWord(struct LinenoiseState *l)
{
	size_t old_pos = l->pos;
	size_t diff;

	while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
		l->pos--;
	while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
		l->pos--;

	diff = old_pos - l->pos;
	memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
	l->len -= diff;
	RefreshLine(l);
}

/* Delete the buffer state after running a command */
void LinenoiseClearBuffer(LinenoiseState *ls)
{
	memset(ls->buf, 0, ls->buflen);
	ls->pos = 0;
	RefreshLine(ls);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int LinenoiseEdit(LinenoiseState *ls)
{
	// Write the prompt
	if (write(ls->ofd, ls->prompt, ls->plen) == -1)
		return -1;

	while (true)
	{
		char c;
		int	 nread;
		char seq[3];

		nread = read(ls->ifd, &c, 1);
		if (nread <= 0)
			return ls->len;

		/* Only autocomplete when the callback is set. It returns < 0 when
		 * there was an error reading from fd. Otherwise it will return the
		 * character that should be handled next. */
		if (c == 9 && l_CompletionCallback != NULL)
		{
			c = CompleteLine(ls);
			/* Return on errors */
			if (c < 0)
				return ls->len;
			/* Read next character when 0 */
			if (c == 0)
				continue;
		}

		switch (c)
		{
			case ENTER: /* enter */
				if (ls->history_len > 0)
				{
					ls->history_len--;
					free(ls->history[ls->history_len]);
				}
				if (ls->mlmode)
					LinenoiseEditMoveEnd(ls);
				if (l_HintsCallback)
				{
					/* Force a refresh without hints to leave the previous
					 * line as the user typed it after a newline. */
					LinenoiseHintsCallback *hc = l_HintsCallback;
					l_HintsCallback			   = NULL;
					RefreshLine(ls);
					l_HintsCallback = hc;
				}
				return (int)ls->len;
			case CTRL_C: /* ctrl-c */
				errno = EAGAIN;
				return -1;
			case BACKSPACE: /* backspace */
			case 8:			/* ctrl-h */
				LinenoiseEditBackspace(ls);
				break;
			case CTRL_D: /* ctrl-d, remove char at right of cursor, or if the
							line is empty, act as end-of-file. */
				if (ls->len > 0)
					LinenoiseEditDelete(ls);
				else
				{
					ls->history_len--;
					free(ls->history[ls->history_len]);
					return -1;
				}
				break;
			case CTRL_T: /* ctrl-t, swaps current character with previous. */
				if (ls->pos > 0 && ls->pos < ls->len)
				{
					int aux			 = ls->buf[ls->pos - 1];
					ls->buf[ls->pos - 1] = ls->buf[ls->pos];
					ls->buf[ls->pos]	 = aux;
					if (ls->pos != ls->len - 1)
						ls->pos++;
					RefreshLine(ls);
				}
				break;
			case CTRL_B: /* ctrl-b */
				LinenoiseEditMoveLeft(ls);
				break;
			case CTRL_F: /* ctrl-f */
				LinenoiseEditMoveRight(ls);
				break;
			case CTRL_P: /* ctrl-p */
				LinenoiseEditHistoryNext(ls, LINENOISE_HISTORY_PREV);
				break;
			case CTRL_N: /* ctrl-n */
				LinenoiseEditHistoryNext(ls, LINENOISE_HISTORY_NEXT);
				break;
			case ESC: /* escape sequence */
				/* Read the next two bytes representing the escape sequence.
				 * Use two calls to handle slow terminals returning the two
				 * chars at different times. */
				if (read(ls->ifd, seq, 1) == -1)
					break;
				if (read(ls->ifd, seq + 1, 1) == -1)
					break;

				/* ESC [ sequences. */
				if (seq[0] == '[')
				{
					if (seq[1] >= '0' && seq[1] <= '9')
					{
						/* Extended escape, read additional byte. */
						if (read(ls->ifd, seq + 2, 1) == -1)
							break;
						if (seq[2] == '~')
						{
							switch (seq[1])
							{
								case '3': /* Delete key. */
									LinenoiseEditDelete(ls);
									break;
							}
						}
					}
					else
					{
						switch (seq[1])
						{
							case 'A': /* Up */
								LinenoiseEditHistoryNext(ls, LINENOISE_HISTORY_PREV);
								break;
							case 'B': /* Down */
								LinenoiseEditHistoryNext(ls, LINENOISE_HISTORY_NEXT);
								break;
							case 'C': /* Right */
								LinenoiseEditMoveRight(ls);
								break;
							case 'D': /* Left */
								LinenoiseEditMoveLeft(ls);
								break;
							case 'H': /* Home */
								LinenoiseEditMoveHome(ls);
								break;
							case 'F': /* End*/
								LinenoiseEditMoveEnd(ls);
								break;
						}
					}
				}

				/* ESC O sequences. */
				else if (seq[0] == 'O')
				{
					switch (seq[1])
					{
						case 'H': /* Home */
							LinenoiseEditMoveHome(ls);
							break;
						case 'F': /* End*/
							LinenoiseEditMoveEnd(ls);
							break;
					}
				}
				break;
			default:
				if (LinenoiseEditInsert(ls, c))
					return -1;
				break;
			case CTRL_U: /* Ctrl+u, delete the whole line. */
				ls->buf[0] = '\0';
				ls->pos = ls->len = 0;
				RefreshLine(ls);
				break;
			case CTRL_K: /* Ctrl+k, delete from current to end of line. */
				ls->buf[ls->pos] = '\0';
				ls->len	   = ls->pos;
				RefreshLine(ls);
				break;
			case CTRL_A: /* Ctrl+a, go to the start of the line */
				LinenoiseEditMoveHome(ls);
				break;
			case CTRL_E: /* ctrl+e, go to the end of the line */
				LinenoiseEditMoveEnd(ls);
				break;
			case CTRL_L: /* ctrl+l, clear screen */
				LinenoiseClearScreen(ls);
				RefreshLine(ls);
				break;
			case CTRL_W: /* ctrl+w, delete previous word */
				LinenoiseEditDeletePrevWord(ls);
				break;
		}
	}
	return ls->len;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void LinenoisePrintKeyCodes(LinenoiseState *ls)
{
	char quit[4];

	dprintf(ls->ofd,
			"Linenoise key codes debugging mode.\n"
			"Press keys to see scan codes. Type 'quit' at any time to exit.\n");
	if (EnableRawMode(ls, ls->ifd) == -1)
		return;
	memset(quit, ' ', 4);
	while (true)
	{
		char c;
		int	 nread;

		nread = read(ls->ifd, &c, 1);
		if (nread <= 0)
			continue;

		memmove(quit, quit + 1, sizeof(quit) - 1); /* shift string to left. */
		quit[sizeof(quit) - 1] = c;				   /* Insert current char on the right. */

		if (memcmp(quit, "quit", sizeof(quit)) == 0)
			break;

		dprintf(ls->ofd, "'%c' %02x (%d) (type quit to exit)\n", isprint(c) ? c : '?', (int)c, (int)c);
		dprintf(ls->ofd, "\r"); /* Go left edge manually, we are in raw mode. */
	}
	DisableRawMode(ls, ls->ifd);
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int LinenoiseRaw(LinenoiseState *ls)
{
	int count;

	if (ls->buflen == 0)
	{
		errno = EINVAL;
		return -1;
	}

	if (EnableRawMode(ls, ls->ifd) == -1)
		return -1;
	count = LinenoiseEdit(ls);
	DisableRawMode(ls, ls->ifd);
	dprintf(ls->ofd, "\n");
	return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *LinenoiseNoTTY(void)
{
	char * line = NULL;
	size_t len = 0, maxlen = 0;

	while (true)
	{
		if (len == maxlen)
		{
			if (maxlen == 0)
				maxlen = 16;
			maxlen *= 2;
			char *oldval = line;
			line		 = realloc(line, maxlen);
			if (line == NULL)
			{
				if (oldval)
					free(oldval);
				return NULL;
			}
		}
		int c = fgetc(stdin);
		if (c == EOF || c == '\n')
		{
			if (c == EOF && len == 0)
			{
				free(line);
				return NULL;
			}
			else
			{
				line[len] = '\0';
				return line;
			}
		}
		else
		{
			line[len] = c;
			len++;
		}
	}
}

void LinenoiseSetNonblock(const LinenoiseState *ls)
{
	int flags = 0;
	if ((flags = fcntl(ls->ifd, F_GETFL, 0)) < 0)
	{
		dprintf(ls->efd, "Error calling fcntl in %s: %s\n", __FUNCTION__, strerror(errno));
	}
}

LinenoiseState *LinenoiseCreate(int ls_stdin, int ls_stdout, int ls_stderr, const char *prompt)
{
	LinenoiseState *ls = malloc(sizeof(LinenoiseState));
	if (!ls)
		return NULL;

	memset(ls, 0, sizeof(LinenoiseState));

	/* Populate the linenoise state that we pass to functions implementing
	 * specific editing functionalities. */
	ls->ifd	   = ls_stdin;
	ls->ofd	   = ls_stdout;
	ls->efd	   = ls_stderr;
	ls->buf	   = malloc(LINENOISE_MAX_LINE);
	ls->buflen = LINENOISE_MAX_LINE;
	ls->prompt = strdup(prompt);
	ls->plen   = strlen(prompt);
	ls->oldpos = ls->pos = 0;
	ls->len				 = 0;
	ls->cols			 = GetColumns(ls_stdin, ls_stdout);
	ls->maxrows			 = 0;
	ls->history_index	 = 0;
	ls->mlmode = false;
	ls->rawmode = false;
	ls->nonblock = false;
	ls->history = NULL;
	ls->history_len = 0;
	ls->history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;

	/* Buffer starts empty. */
	memset(ls->buf, 0, ls->buflen);
	ls->buflen--; /* Make sure there is always space for the nulterm */

	/* The latest history entry is always our current buffer, that
	 * initially is just an empty string. */
	LinenoiseHistoryAdd(ls, "");

	return ls;
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *Linenoise(LinenoiseState *ls)
{
	int	 count;
	ls->buf[0] = '\0';

	if (!isatty(ls->ifd))
	{
		/* Not a tty: read from file / pipe. In this mode we don't want any
		 * limit to the line size, so we call a function to handle that. */
		return LinenoiseNoTTY();
	}
	else if (IsUnsupportedTerm())
	{
		size_t len;

		dprintf(ls->ofd, "%s", ls->prompt);
		if (fgets(ls->buf, ls->buflen, stdin) == NULL)
			return NULL;

		len = strlen(ls->buf);
		while (len && (ls->buf[len - 1] == '\n' || ls->buf[len - 1] == '\r'))
		{
			len--;
			ls->buf[len] = '\0';
		}
		dprintf(ls->ofd, "\r");
		return strdup(ls->buf);
	}
	else
	{
		count = LinenoiseRaw(ls);
		if (count == -1)
			return NULL;
		dprintf(ls->ofd, "\r");
		return strdup(ls->buf);
	}
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void LinenoiseFree(void *ptr)
{
	free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void FreeHistory(LinenoiseState *ls)
{
	if (ls->history)
	{
		int j;

		for (j = 0; j < ls->history_len; j++)
			free(ls->history[j]);

		free(ls->history);
	}
	ls->history = NULL;
	ls->history_len = 0;
}

void LinenoiseFreeState(LinenoiseState *ls)
{
	FreeHistory(ls);
	free(ls->buf);
	free((void *)ls->prompt);
	free(ls);
}

/* At exit we'll try to fix the terminal to the initial conditions. */
void LinenoiseRestore(LinenoiseState *ls)
{
	DisableRawMode(ls, ls->ifd);
	tcsetattr(ls->ifd, TCSAFLUSH, &ls->orig_termios);
	FreeHistory(ls);
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int LinenoiseHistoryAdd(LinenoiseState *ls, const char *line)
{
	char *linecopy;

	if (ls->history_max_len == 0)
		return 0;

	/* Initialization on first call. */
	if (ls->history == NULL)
	{
		ls->history = malloc(sizeof(char *) * ls->history_max_len);
		if (ls->history == NULL)
			return 0;
		memset(ls->history, 0, (sizeof(char *) * ls->history_max_len));
	}

	/* Don't add duplicated lines. */
	if (ls->history_len && !strcmp(ls->history[ls->history_len - 1], line))
		return 0;

	/* Add an heap allocated copy of the line in the history.
	 * If we reached the max length, remove the older line. */
	linecopy = strdup(line);
	if (!linecopy)
		return 0;
	if (ls->history_len == ls->history_max_len)
	{
		free(ls->history[0]);
		memmove(ls->history, ls->history + 1, sizeof(char *) * (ls->history_max_len - 1));
		ls->history_len--;
	}

	ls->history[ls->history_len] = linecopy;
	ls->history_len++;
	return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int LinenoiseHistorySetMaxLen(LinenoiseState *ls, int len)
{
	char **new;

	if (len < 1)
		return 0;
	if (ls->history)
	{
		int tocopy = ls->history_len;

		new = malloc(sizeof(char *) * len);
		if (new == NULL)
			return 0;

		/* If we can't copy everything, free the elements we'll not use. */
		if (len < tocopy)
		{
			int j;

			for (j = 0; j < tocopy - len; j++)
				free(ls->history[j]);
			tocopy = len;
		}
		memset(new, 0, sizeof(char *) * len);
		memcpy(new, ls->history + (ls->history_len - tocopy), sizeof(char *) * tocopy);
		free(ls->history);
		ls->history = new;
	}

	ls->history_max_len = len;
	if (ls->history_len > ls->history_max_len)
		ls->history_len = ls->history_max_len;
	return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int LinenoiseHistorySave(const LinenoiseState *ls, const char *filename)
{
	mode_t old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
	FILE * fp;
	int	   j;

	fp = fopen(filename, "w");
	umask(old_umask);
	if (fp == NULL)
		return -1;

	chmod(filename, S_IRUSR | S_IWUSR);
	for (j = 0; j < ls->history_len; j++)
		fprintf(fp, "%s\n", ls->history[j]);

	fclose(fp);
	return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int LinenoiseHistoryLoad(LinenoiseState *ls, const char *filename)
{
	FILE *fp = fopen(filename, "r");
	char  buf[LINENOISE_MAX_LINE];

	if (fp == NULL)
		return -1;

	while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL)
	{
		char *p;

		p = strchr(buf, '\r');
		if (!p)
			p = strchr(buf, '\n');

		if (p)
			*p = '\0';
		LinenoiseHistoryAdd(ls, buf);
	}
	fclose(fp);
	return 0;
}
