/*-------------------------------------------------------------------------
 *
 *	  EUC_KR <--> UTF8
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/mb/conversion_procs/utf8_and_euc_kr/utf8_and_euc_kr.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "../../Unicode/euc_kr_to_utf8.map"
#include "../../Unicode/utf8_to_euc_kr.map"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(euc_kr_to_utf8);
PG_FUNCTION_INFO_V1(utf8_to_euc_kr);

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
euc_kr_to_utf8(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_EUC_KR, PG_UTF8);

	converted = LocalToUtf(src, len, dest,
						   &euc_kr_to_unicode_tree,
						   NULL, 0,
						   NULL,
						   PG_EUC_KR,
						   noError);

	PG_RETURN_INT32(converted);
}

Datum
utf8_to_euc_kr(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_UTF8, PG_EUC_KR);

	converted = UtfToLocal(src, len, dest,
						   &euc_kr_from_unicode_tree,
						   NULL, 0,
						   NULL,
						   PG_EUC_KR,
						   noError);

	PG_RETURN_INT32(converted);
}
