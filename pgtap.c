/*
 * PostgreSQL utility functions for pgTAP.
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

extern Datum pg_typeof (PG_FUNCTION_ARGS);

/*
 * pg_typeof()
 * Returns a string for the data type of an anyelement argument.
 */

PG_FUNCTION_INFO_V1(pg_regtypeof);

Datum
pg_typeof(PG_FUNCTION_ARGS)
{
    Oid    typeoid;

    typeoid = get_fn_expr_argtype(fcinfo->flinfo, 0);
    if (typeoid == InvalidOid) {
        ereport(
            ERROR, (
                errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("could not determine data type of argument to pg_typeof()")
            )
        );
    }

	PG_RETURN_DATUM(
        DirectFunctionCall1(textin, CStringGetDatum(format_type_be(typeoid)))
    );
}
