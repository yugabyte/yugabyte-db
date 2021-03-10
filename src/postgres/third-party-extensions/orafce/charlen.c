/*
 * charlen.c
 *
 * provides a modified version of bpcharlen() that does not
 * ignore trailing spaces of CHAR arguments to provide an
 * Oracle compatible length() function
 */

#include "postgres.h"

#include "utils/builtins.h"
#include "access/hash.h"
#include "libpq/pqformat.h"
#include "nodes/nodeFuncs.h"
#include "utils/array.h"
#include "utils/formatting.h"
#include "mb/pg_wchar.h"
#include "fmgr.h"

#include "orafce.h"
#include "builtins.h"

PG_FUNCTION_INFO_V1(orafce_bpcharlen);

Datum
orafce_bpcharlen(PG_FUNCTION_ARGS)
{
	BpChar     *arg = PG_GETARG_BPCHAR_PP(0);
	int         len;

	/* byte-length of the argument (trailing spaces not ignored) */
	len = VARSIZE_ANY_EXHDR(arg);

	/* in multibyte encoding, convert to number of characters */
	if (pg_database_encoding_max_length() != 1)
		len = pg_mbstrlen_with_len(VARDATA_ANY(arg), len);

	PG_RETURN_INT32(len);
}
