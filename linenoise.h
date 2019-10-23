/* linenoise.h -- VERSION 1.0
 *
 * Guerrilla line editing library against the idea that a line editing lib
 * needs to be 20,000 lines of C code.
 *
 * See linenoise.c for more information.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2014, Salvatore Sanfilippo <antirez at gmail dot com>
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
 */

#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <termios.h>

#ifdef __cplusplus
extern "C"
{
#endif

	typedef struct LinenoiseCompletions
	{
		size_t len;
		char **cvec;
	} LinenoiseCompletions;

	/* The linenoiseState structure represents the state during line editing.
	 * We pass this state to functions implementing specific editing
	 * functionalities. */
	typedef struct LinenoiseState
	{
		int			   ifd;				/* Terminal stdin file descriptor. */
		int			   ofd;				/* Terminal stdout file descriptor. */
		int			   efd;				/* Terminal stderr file descriptor.  */
		char *		   buf;				/* Edited line buffer. */
		size_t		   buflen;			/* Edited line buffer size. */
		const char *   prompt;			/* Prompt to display. */
		size_t		   plen;			/* Prompt length. */
		size_t		   pos;				/* Current cursor position. */
		size_t		   oldpos;			/* Previous refresh cursor position. */
		size_t		   len;				/* Current edited line length. */
		size_t		   cols;			/* Number of columns in terminal. */
		size_t		   maxrows;			/* Maximum num of rows used so far (multiline mode) */
		int			   history_index;	/* The history index we are currently editing. */
		struct termios orig_termios;	/* In order to restore at exit.*/
		bool		   rawmode;			/* For atexit() function to check if restore is needed, false by default. */
		bool		   mlmode;			/* Multi line mode. Default is single line. */
		bool		   nonblock;		/* Non-blocking mode. Default is blocking. */
		int			   history_max_len; /* Maximum length of the history */
		int			   history_len;		/* Current length of the history */
		char **		   history;			/* The history */
	} LinenoiseState;

	typedef void(LinenoiseCompletionCallback)(const char *, LinenoiseCompletions *);
	typedef char *(LinenoiseHintsCallback)(const char *, int *color, int *bold);
	typedef void(LinenoiseFreeHintsCallback)(void *);
	void LinenoiseSetCompletionCallback(LinenoiseCompletionCallback *);
	void LinenoiseSetHintsCallback(LinenoiseHintsCallback *);
	void LinenoiseSetFreeHintsCallback(LinenoiseFreeHintsCallback *);
	void LinenoiseAddCompletion(LinenoiseCompletions *, const char *);

	char *			Linenoise(LinenoiseState *ls);
	void			LinenoiseClearBuffer(LinenoiseState *ls);
	void			LinenoiseFree(void *ptr);
	void			LinenoiseFreeState(LinenoiseState *ls);
	void			LinenoiseRestore(LinenoiseState *ls);
	int				LinenoiseHistoryAdd(LinenoiseState *ls, const char *line);
	int				LinenoiseHistorySetMaxLen(LinenoiseState *ls, int len);
	int				LinenoiseHistorySave(const LinenoiseState *ls, const char *filename);
	int				LinenoiseHistoryLoad(LinenoiseState *ls, const char *filename);
	void			LinenoiseClearScreen(const LinenoiseState *ls);
	void			LinenoiseSetMultiLine(LinenoiseState *ls, int ml);
	void			LinenoisePrintKeyCodes(LinenoiseState *ls);
	LinenoiseState *LinenoiseCreate(int ls_stdin, int ls_stdout, int ls_stderr, const char *prompt);

#ifdef __cplusplus
}
#endif
