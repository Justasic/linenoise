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

#ifndef __LINENOISE_H
#define __LINENOISE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LinenoiseCompletions {
		size_t len;
		char **cvec;
} LinenoiseCompletions;

typedef void(LinenoiseCompletionCallback)(const char *, LinenoiseCompletions *);
typedef char*(LinenoiseHintsCallback)(const char *, int *color, int *bold);
typedef void(LinenoiseFreeHintsCallback)(void *);
void LinenoiseSetCompletionCallback(LinenoiseCompletionCallback *);
void LinenoiseSetHintsCallback(LinenoiseHintsCallback *);
void LinenoiseSetFreeHintsCallback(LinenoiseFreeHintsCallback *);
void LinenoiseAddCompletion(LinenoiseCompletions *, const char *);

char *linenoise(const char *prompt);
void LinenoiseFree(void *ptr);
int LinenoiseHistoryAdd(const char *line);
int LinenoiseHistorySetMaxLen(int len);
int LinenoiseHistorySave(const char *filename);
int LinenoiseHistoryLoad(const char *filename);
void LinenoiseClearScreen(void);
void LinenoiseSetMultiLine(int ml);
void LinenoisePrintKeyCodes(void);

#ifdef __cplusplus
}
#endif

#endif /* __LINENOISE_H */
