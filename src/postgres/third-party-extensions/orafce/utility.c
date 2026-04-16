/*
  This code implements one part of functonality of
  free available library PL/Vision. Please look www.quest.com

  Original author: Steven Feuerstein, 1996 - 2002
  PostgreSQL implementation author: Pavel Stehule, 2006-2023

  This module is under BSD Licence

  History:
    1.0. first public version 22. September 2006

*/

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "postgres.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "utils/pg_locale.h"
#include "mb/pg_wchar.h"
#include "lib/stringinfo.h"

#include "catalog/pg_type.h"
#include "libpq/pqformat.h"
#include "utils/array.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"
#include "access/tupmacs.h"
#include "orafce.h"
#include "builtins.h"

#include "utils/elog.h"

PG_FUNCTION_INFO_V1(dbms_utility_format_call_stack0);
PG_FUNCTION_INFO_V1(dbms_utility_format_call_stack1);
PG_FUNCTION_INFO_V1(dbms_utility_get_time);

static char*
dbms_utility_format_call_stack(char mode)
{
	MemoryContext oldcontext = CurrentMemoryContext;
	ErrorData *edata;
	ErrorContextCallback *econtext;
	StringInfo   sinfo;


#if PG_VERSION_NUM >= 130000

	errstart(ERROR, TEXTDOMAIN);

#else

	errstart(ERROR, __FILE__, __LINE__, PG_FUNCNAME_MACRO, TEXTDOMAIN);

#endif

	MemoryContextSwitchTo(oldcontext);

	for (econtext = error_context_stack;
		 econtext != NULL;
		 econtext = econtext->previous)
		(*econtext->callback) (econtext->arg);

	edata = CopyErrorData();

	FlushErrorState();

	/* Now I wont to parse edata->context to more traditional format */
	/* I am not sure about order */

	sinfo = makeStringInfo();

	switch (mode)
	{
		case 'o':
			appendStringInfoString(sinfo, "----- PL/pgSQL Call Stack -----\n");
			appendStringInfoString(sinfo, "  object     line  object\n");
			appendStringInfoString(sinfo, "  handle   number  name\n");
			break;
	}

	if (edata->context)
	{
		char *start = edata->context;
		while (*start)
		{
			char *oname =  "anonymous object";
			char *line  = "";
			char *eol = strchr(start, '\n');
			Oid fnoid = InvalidOid;

			/* first, solve multilines */
			if (eol)
				*eol = '\0';

			/* first know format */
			if (strncmp(start, "PL/pgSQL function ",18) == 0)
			{
				char *p1, *p2;

				if ((p1 = strstr(start, "function \"")))
				{
					p1 += strlen("function \"");

					if ((p2 = strchr(p1, '"')))
					{
						*p2++ = '\0';
						oname = p1;
						start = p2;
					}
				}
				else if ((p1 = strstr(start, "function ")))
				{
					p1 += strlen("function ");

					if ((p2 = strchr(p1, ')')))
					{
						char c = *++p2;
						*p2 = '\0';

						oname = pstrdup(p1);
						fnoid = DatumGetObjectId(DirectFunctionCall1(regprocedurein,
							CStringGetDatum(oname)));
						*p2 = c;
						start = p2;
					}
				}


				if ((p1 = strstr(start, "line ")))
				{
					size_t p2i;
					char c;

					p1 += strlen("line ");
					p2i = strspn(p1, "0123456789");

					/* safe separator */
					c = p1[p2i];

					p1[p2i] = '\0';
					line = pstrdup(p1);
					p1[p2i] = c;
				}
			}

			switch (mode)
			{
				case 'o':
					appendStringInfo(sinfo, "%8x    %5s  function %s", (int)fnoid, line, oname);
					break;

				case 'p':
					appendStringInfo(sinfo, "%8d    %5s  function %s", (int)fnoid, line, oname);
					break;

				case 's':
					appendStringInfo(sinfo, "%d,%s,%s", (int)fnoid, line, oname);
					break;
			}

			if (eol)
			{
				start = eol + 1;
				appendStringInfoChar(sinfo, '\n');
			}
			else
				break;
		}

	}

	return sinfo->data;
}


Datum
dbms_utility_format_call_stack0(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(dbms_utility_format_call_stack('o')));
}

Datum
dbms_utility_format_call_stack1(PG_FUNCTION_ARGS)
{
	text *arg = PG_GETARG_TEXT_P(0);
	char mode;

	if ((1 != VARSIZE(arg) - VARHDRSZ))
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("invalid parameter"),
			 errdetail("Allowed only chars [ops].")));

	mode = *VARDATA(arg);
	switch (mode)
	{
		case 'o':
		case 'p':
		case 's':
			break;
		default:
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid parameter"),
				 errdetail("Allowed only chars [ops].")));
	}

	PG_RETURN_TEXT_P(cstring_to_text(dbms_utility_format_call_stack(mode)));
}

/*
 * Returns the number of hundredths of seconds that have elapsed
 * since a point in time in the past.
 */
Datum
dbms_utility_get_time(PG_FUNCTION_ARGS)
{
	struct timeval tv;

	gettimeofday(&tv,NULL);
	PG_RETURN_INT32((int32) ((int64) tv.tv_sec * 100 + tv.tv_usec / 10000));
}
