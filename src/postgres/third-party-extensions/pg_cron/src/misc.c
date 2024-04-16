/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 */

/* marco 07nov16 [removed code not needed by pg_cron]
 * marco 04sep16 [integrated into pg_cron]
 * vix 26jan87 [RCS has the rest of the log]
 * vix 30dec86 [written]
 */


#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include "cron.h"


/* get_char(file) : like getc() but increment LineNumber on newlines
 */
int
get_char(FILE *file)
{
	int	ch;

	/*
	 * Sneaky hack: we wrapped an in-memory buffer into a FILE*
	 * to minimize changes to cron.c.
	 *
	 * This code replaces:
	 * ch = getc(file);
	 */
	file_buffer *buffer = (file_buffer *) file;

	if (buffer->unget_count > 0)
	{
		ch = buffer->unget_data[--buffer->unget_count];
	}
	else if (buffer->pointer == buffer->length)
	{
		ch = '\0';
		buffer->pointer++;
	}
	else if (buffer->pointer > buffer->length)
	{
		ch = EOF;
	}
	else
	{
		ch = buffer->data[buffer->pointer++];
	}

	if (ch == '\n')
		Set_LineNum(LineNumber + 1);
	return ch;
}


/* unget_char(ch, file) : like ungetc but do LineNumber processing
 */
void
unget_char(int ch, FILE *file)
{

	/*
	 * Sneaky hack: we wrapped an in-memory buffer into a FILE*
	 * to minimize changes to cron.c.
	 *
	 * This code replaces:
	 * ungetc(ch, file);
	 */
	file_buffer *buffer = (file_buffer *) file;

	if (buffer->unget_count >= 1024)
	{	
		perror("ungetc limit exceeded");
		exit(ERROR_EXIT);
	}

	buffer->unget_data[buffer->unget_count++] = ch;

	if (ch == '\n')
	       Set_LineNum(LineNumber - 1);
}


/* get_string(str, max, file, termstr) : like fgets() but
 *		(1) has terminator string which should include \n
 *		(2) will always leave room for the null
 *		(3) uses get_char() so LineNumber will be accurate
 *		(4) returns EOF or terminating character, whichever
 */
int
get_string(char *string, int size, FILE *file, char *terms)
{
	int	ch;

	while (EOF != (ch = get_char(file)) && !strchr(terms, ch)) {
		if (size > 1) {
			*string++ = (char) ch;
			size--;
		}
	}

	if (size > 0)
		*string = '\0';

	return ch;
}


/* skip_comments(file) : read past comment (if any)
 */
void
skip_comments(FILE *file)
{
	int	ch;

	while (EOF != (ch = get_char(file))) {
		/* ch is now the first character of a line.
		 */

		while (ch == ' ' || ch == '\t')
			ch = get_char(file);

		if (ch == EOF)
			break;

		/* ch is now the first non-blank character of a line.
		 */

		if (ch != '\n' && ch != '#')
			break;

		/* ch must be a newline or comment as first non-blank
		 * character on a line.
		 */

		while (ch != '\n' && ch != EOF)
			ch = get_char(file);

		/* ch is now the newline of a line which we're going to
		 * ignore.
		 */
	}
	if (ch != EOF)
		unget_char(ch, file);
}
