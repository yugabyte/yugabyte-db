#include "postgres.h"

#include "fmgr.h"

PG_FUNCTION_INFO_V1(cypher);

Datum cypher(PG_FUNCTION_ARGS)
{
    const char *s;

    s = PG_ARGISNULL(0) ? "NULL" : PG_GETARG_CSTRING(0);

    ereport(ERROR, (errmsg_internal("unhandled cypher(cstring) function call"),
                    errdetail_internal("%s", s)));

    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(_cypher_create_clause);

Datum _cypher_create_clause(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
            (errmsg_internal("unhandled _cypher_create_clause(internal) function call")));

    PG_RETURN_NULL();
}
