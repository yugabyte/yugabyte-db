/*
  This code implements one part of functonality of 
  free available library PL/Vision. Please look www.quest.com

  Original author: Steven Feuerstein, 1996 - 2002
  PostgreSQL implementation author: Pavel Stehule, 2006

  This module is under BSD Licence

  History:
    1.0. first public version 22. September 2006
    
*/

#include "postgres.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "string.h"
#include "stdlib.h"
#include "utils/pg_locale.h"
#include "mb/pg_wchar.h"
#include "lib/stringinfo.h"

#include "catalog/pg_type.h"
#include "libpq/pqformat.h"
#include "utils/array.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"
#include "access/tupmacs.h"
#include "orafunc.h"

#include "plvstr.h"
#include "utils/elog.h"


Datum dbms_utility_format_call_stack(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(dbms_utility_format_call_stack);

Datum 
dbms_utility_format_call_stack(PG_FUNCTION_ARGS)
{
	MemoryContext oldcontext = CurrentMemoryContext;
	ErrorData *edata;
	ErrorContextCallback *econtext;
	StringInfo   sinfo;


	errstart(ERROR, __FILE__, __LINE__, PG_FUNCNAME_MACRO);
	
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

	appendStringInfoString(sinfo, "-----  Call Stack  -----\n");
	appendStringInfoString(sinfo, "  line             object\n");
	appendStringInfoString(sinfo, "number  statement  name\n");

	if (edata->context)
	{
		char *start = edata->context;
	    
		while (*start)
		{
			char *oname =  "unamed object";
			char *stmt  = pstrdup("         ");
			char *line  = pstrdup("      ");
			char *eol = strchr(start, '\n');

			/* first, solve multilines */
			if (eol)
				*eol = '\0';
    
			/* first know format */
			if (strncmp(start, "PL/pgSQL function \"",19) == 0)
			{

		    		char *p1, *p2;

				if ((p1 = strstr(start, "function \"")))
				{
					p1 += strlen("function \"");

					if ((p2 = strchr(p1, '"')))
					{
						*p2 = '\0';
						start = p2 + 1;
						oname = p1;
					}
				}

				if ((p1 = strstr(start, "line ")))
				{
					int p2i;
					char c;

					p1 += strlen("line ");
					p2i = strspn(p1, "0123456789");
					
					/* safe separator */
					c = p1[p2i]; 
										
					p1[p2i] = '\0';
					strcpy(line + (6 - strlen(p1)), p1);
					p1[p2i] = c;
					
					start = p1 + p2i;									
				} 


				if ((p1 = strstr(start, "at ")))
				{
					int l;

					p1 += strlen("at ");
					l = strlen(p1);

					l = l > 9 ? 9 : l;
					strncpy(stmt, p1, l);
				}
			} 

			appendStringInfoString(sinfo, line);
			appendStringInfoString(sinfo, "  ");
			appendStringInfoString(sinfo, stmt);
			appendStringInfoString(sinfo, "  function ");
			appendStringInfoString(sinfo, oname);

			pfree(line);
			pfree(stmt);

			if (eol)
			{
				start = eol + 1;							
				appendStringInfoChar(sinfo, '\n');
			}
			else
				break;
		}
		    
	}

	PG_RETURN_TEXT_P(ora_make_text(sinfo->data));
}

