#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"

#include "graphid.h"

PG_FUNCTION_INFO_V1(graphid_in);

// graphid type input function
Datum graphid_in(PG_FUNCTION_ARGS)
{
    char *str = PG_GETARG_CSTRING(0);
    char *endptr;
    int64 i;

    errno = 0;
    i = strtol(str, &endptr, 10);
    if (errno != 0 || endptr == str || *endptr != '\0')
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("invalid value for type graphid: \"%s\"", str)));
    }

    AG_RETURN_GRAPHID(i);
}

PG_FUNCTION_INFO_V1(graphid_out);

// graphid type output function
Datum graphid_out(PG_FUNCTION_ARGS)
{
    const int GRAPHID_LEN = 20; // -9223372036854775808
    graphid gid = AG_GETARG_GRAPHID(0);
    char buf[GRAPHID_LEN + 1];
    char *out;

    pg_lltoa((int64)gid, buf);
    out = pstrdup(buf);

    PG_RETURN_CSTRING(out);
}
