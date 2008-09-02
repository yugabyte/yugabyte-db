/*
 * PostgreSQL utility functions for pgTAP.
 */

#include "postgres.h"
#include "utils/builtins.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

extern Datum pg_typeof (PG_FUNCTION_ARGS);

/*
 * pg_typeof()
 * Returns the OID for the data type of its first argument. The SQL function
 * returns regtype, which magically makes it return text.
 */

PG_FUNCTION_INFO_V1(pg_typeof);

Datum
pg_typeof(PG_FUNCTION_ARGS)
{
    PG_RETURN_OID( get_fn_expr_argtype(fcinfo->flinfo, 0) );
}
