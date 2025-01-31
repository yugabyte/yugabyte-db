/*-------------------------------------------------------------------------
 *
 *	  GBK <--> UTF8
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/mb/conversion_procs/utf8_and_gbk/utf8_and_gbk.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "../../Unicode/gbk_to_utf8.map"
#include "../../Unicode/utf8_to_gbk.map"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(gbk_to_utf8);
PG_FUNCTION_INFO_V1(utf8_to_gbk);

/* ----------
 * conv_proc(
 *		INTEGER,	-- source encoding id
 *		INTEGER,	-- destination encoding id
 *		CSTRING,	-- source string (null terminated C string)
 *		CSTRING,	-- destination string (null terminated C string)
 *		INTEGER,	-- source string length
 *		BOOL		-- if true, don't throw an error if conversion fails
 * ) returns INTEGER;
 *
 * Returns the number of bytes successfully converted.
 * ----------
 */
Datum
gbk_to_utf8(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_GBK, PG_UTF8);

	converted = LocalToUtf(src, len, dest,
						   &gbk_to_unicode_tree,
						   NULL, 0,
						   NULL,
						   PG_GBK,
						   noError);

	PG_RETURN_INT32(converted);
}

Datum
utf8_to_gbk(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_UTF8, PG_GBK);

	converted = UtfToLocal(src, len, dest,
						   &gbk_from_unicode_tree,
						   NULL, 0,
						   NULL,
						   PG_GBK,
						   noError);

	PG_RETURN_INT32(converted);
}
