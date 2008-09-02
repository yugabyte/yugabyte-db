/*
 * PostgreSQL utility functions for pgTAP.
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

extern Datum type_of (PG_FUNCTION_ARGS);

/*
 * type_of()
 * Returns a string for the data type of an anyelement argument.
 */

PG_FUNCTION_INFO_V1(type_of);

Datum
type_of(PG_FUNCTION_ARGS)
{
    Oid    typeoid;
    Datum  result;
    char   *typename;

    typeoid = get_fn_expr_argtype(fcinfo->flinfo, 0);
    if (typeoid == InvalidOid) {
        ereport(
            ERROR, (
                errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("could not determine data type of argument to type_of()")
            )
        );
    }

    typename = format_type_be(typeoid);
    result = DirectFunctionCall1(textin, CStringGetDatum(typename));
	PG_RETURN_DATUM(result);
}
