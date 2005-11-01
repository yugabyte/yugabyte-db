#include "postgres.h"

#include "commands/trigger.h"	/* -"- and triggers */

Datum ora_protect_table_fx(PG_FUNCTION_ARGS);


PG_FUNCTION_INFO_V1(ora_protect_table_fx);

Datum
ora_protect_table_fx(PG_FUNCTION_ARGS)
{
	if (!CALLED_AS_TRIGGER(fcinfo))
		/* internal error */
		elog(ERROR, "not fired by trigger manager");

	elog(ERROR, "changes prohibited");

	PG_RETURN_NULL();
}
