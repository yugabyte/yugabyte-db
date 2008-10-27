/*
 * PostgreSQL utility functions for pgTAP.
 */

#include "postgres.h"
#include "utils/builtins.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

extern Datum pg_typeof (PG_FUNCTION_ARGS);
/* Switched to pure SQL.
extern Datum pg_version (PG_FUNCTION_ARGS);
extern Datum pg_version_num (PG_FUNCTION_ARGS);
*/

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

/*
 * pg_version()
 * Returns the version number as output by version(), but without all the
 * other crap. Code borrowed from version.c.
 */

/* Switched to pure SQL. Kept here for posterity.

PG_FUNCTION_INFO_V1(pg_version);

Datum
pg_version(PG_FUNCTION_ARGS)
{
	int			n = strlen(PG_VERSION);
	text	   *ret = (text *) palloc(n + VARHDRSZ);

#ifdef SET_VARSIZE
	SET_VARSIZE(ret, n + VARHDRSZ);
#else
	VARATT_SIZEP(ret) = n + VARHDRSZ;
#endif
	memcpy(VARDATA(ret), PG_VERSION, n);

	PG_RETURN_TEXT_P(ret);
}

*/

/*
 * pg_version_num()
 * Returns the version number as an integer. Support for pre-8.2 borrowed from
 * dumputils.c.
 */

/* Switched to pure SQL. Ketp here for posterity.
PG_FUNCTION_INFO_V1(pg_version_num);

Datum
pg_version_num(PG_FUNCTION_ARGS)
{
#ifdef PG_VERSION_NUM
    PG_RETURN_INT32(PG_VERSION_NUM);
#else
	int			cnt;
	int			vmaj,
				vmin,
				vrev;

	cnt = sscanf(PG_VERSION, "%d.%d.%d", &vmaj, &vmin, &vrev);

	if (cnt < 2)
		return -1;

	if (cnt == 2)
		vrev = 0;

	PG_RETURN_INT32( (100 * vmaj + vmin) * 100 + vrev );
#endif
}

*/
