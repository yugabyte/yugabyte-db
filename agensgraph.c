#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(cypher);

Datum cypher(PG_FUNCTION_ARGS)
{
    const char *s;

    s = PG_ARGISNULL(0) ? "NULL" : text_to_cstring(PG_GETARG_TEXT_PP(0));

    ereport(ERROR, (errmsg_internal("unhandled cypher(text) function call"),
                    errdetail_internal("%s", s)));

    PG_RETURN_NULL();
}
