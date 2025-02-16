#include "linenoise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

LinenoiseState *ls = NULL;

void completion(const char *buf, LinenoiseCompletions *lc)
{
	if (!strcasecmp(buf, "hello"))
	{
		LinenoiseAddCompletion(lc, "hello World");
		return;
	}

	if (buf[0] == 'h')
	{
		LinenoiseAddCompletion(lc, "hello");
	}
}

char *hints(const char *buf, int *color, int *bold)
{
	if (!strcasecmp(buf, "hello"))
	{
		*color = 35;
		*bold  = 0;
		return " World";
	}
	return NULL;
}

void AtExit(void)
{
	LinenoiseRestore(ls);
	LinenoiseFreeState(ls);
}

int main(int argc, char **argv)
{
	char *line;
	char *prgname = argv[0];

	ls = LinenoiseCreate(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, "> ");
	atexit(AtExit);

	/* Parse options, with --multiline we enable multi line editing. */
	while (argc > 1)
	{
		argc--;
		argv++;
		if (!strcmp(*argv, "--multiline"))
		{
			LinenoiseSetMultiLine(ls, true);
			printf("Multi-line mode enabled.\n");
		}
		else if (!strcmp(*argv, "--keycodes"))
		{
			LinenoisePrintKeyCodes(ls);
			exit(0);
		}
		else
		{
			fprintf(stderr, "Usage: %s [--multiline] [--keycodes]\n", prgname);
			exit(1);
		}
	}

	/* Set the completion callback. This will be called every time the
	 * user uses the <tab> key. */
	LinenoiseSetCompletionCallback(completion);
	LinenoiseSetHintsCallback(hints);

	/* Load history from file. The history file is just a plain text file
	 * where entries are separated by newlines. */
	LinenoiseHistoryLoad(ls, "history.txt"); /* Load the history at startup */

	/* Now this is the main loop of the typical linenoise-based application.
	 * The call to linenoise() will block as long as the user types something
	 * and presses enter.
	 *
	 * The typed string is returned as a malloc() allocated string by
	 * linenoise, so the user needs to free() it. */
	while ((line = Linenoise(ls)) != NULL)
	{
		/* Do something with the string. */
		if (line[0] != '\0' && line[0] != '/')
		{
			printf("echo: '%s'\n", line);
			LinenoiseHistoryAdd(ls, line);			 /* Add to the history. */
			LinenoiseHistorySave(ls, "history.txt"); /* Save the history on disk. */
		}
		else if (!strncmp(line, "/exit", 5))
			break;
		else if (!strncmp(line, "/historylen", 11))
		{
			/* The "/historylen" command will change the history len. */
			int len = atoi(line + 11);
			LinenoiseHistorySetMaxLen(ls, len);
		}
		else if (line[0] == '/')
			printf("Unreconized command: %s\n", line);

		free(line);
		LinenoiseClearBuffer(ls);
	}

	LinenoiseRestore(ls);

	return 0;
}
