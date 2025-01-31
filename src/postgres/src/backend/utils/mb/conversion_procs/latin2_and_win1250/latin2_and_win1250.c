/*-------------------------------------------------------------------------
 *
 *	  LATIN2 and WIN1250
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/mb/conversion_procs/latin2_and_win1250/latin2_and_win1250.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "mb/pg_wchar.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(latin2_to_mic);
PG_FUNCTION_INFO_V1(mic_to_latin2);
PG_FUNCTION_INFO_V1(win1250_to_mic);
PG_FUNCTION_INFO_V1(mic_to_win1250);
PG_FUNCTION_INFO_V1(latin2_to_win1250);
PG_FUNCTION_INFO_V1(win1250_to_latin2);

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

/* WIN1250 to ISO-8859-2 */
static const unsigned char win1250_2_iso88592[] = {
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0xA9, 0x8B, 0xA6, 0xAB, 0xAE, 0xAC,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0xB9, 0x9B, 0xB6, 0xBB, 0xBE, 0xBC,
	0xA0, 0xB7, 0xA2, 0xA3, 0xA4, 0xA1, 0x00, 0xA7,
	0xA8, 0x00, 0xAA, 0x00, 0x00, 0xAD, 0x00, 0xAF,
	0xB0, 0x00, 0xB2, 0xB3, 0xB4, 0x00, 0x00, 0x00,
	0xB8, 0xB1, 0xBA, 0x00, 0xA5, 0xBD, 0xB5, 0xBF,
	0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
	0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
	0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
	0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
	0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
	0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

/* ISO-8859-2 to WIN1250 */
static const unsigned char iso88592_2_win1250[] = {
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0x00, 0x9B, 0x00, 0x00, 0x00, 0x00,
	0xA0, 0xA5, 0xA2, 0xA3, 0xA4, 0xBC, 0x8C, 0xA7,
	0xA8, 0x8A, 0xAA, 0x8D, 0x8F, 0xAD, 0x8E, 0xAF,
	0xB0, 0xB9, 0xB2, 0xB3, 0xB4, 0xBE, 0x9C, 0xA1,
	0xB8, 0x9A, 0xBA, 0x9D, 0x9F, 0xBD, 0x9E, 0xBF,
	0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
	0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
	0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
	0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
	0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
	0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};


Datum
latin2_to_mic(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_LATIN2, PG_MULE_INTERNAL);

	converted = latin2mic(src, dest, len, LC_ISO8859_2, PG_LATIN2, noError);

	PG_RETURN_INT32(converted);
}

Datum
mic_to_latin2(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_MULE_INTERNAL, PG_LATIN2);

	converted = mic2latin(src, dest, len, LC_ISO8859_2, PG_LATIN2, noError);

	PG_RETURN_INT32(converted);
}

Datum
win1250_to_mic(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_WIN1250, PG_MULE_INTERNAL);

	converted = latin2mic_with_table(src, dest, len, LC_ISO8859_2, PG_WIN1250,
									 win1250_2_iso88592, noError);

	PG_RETURN_INT32(converted);
}

Datum
mic_to_win1250(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_MULE_INTERNAL, PG_WIN1250);

	converted = mic2latin_with_table(src, dest, len, LC_ISO8859_2, PG_WIN1250,
									 iso88592_2_win1250, noError);

	PG_RETURN_INT32(converted);
}

Datum
latin2_to_win1250(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_LATIN2, PG_WIN1250);

	converted = local2local(src, dest, len, PG_LATIN2, PG_WIN1250,
							iso88592_2_win1250, noError);

	PG_RETURN_INT32(converted);
}

Datum
win1250_to_latin2(PG_FUNCTION_ARGS)
{
	unsigned char *src = (unsigned char *) PG_GETARG_CSTRING(2);
	unsigned char *dest = (unsigned char *) PG_GETARG_CSTRING(3);
	int			len = PG_GETARG_INT32(4);
	bool		noError = PG_GETARG_BOOL(5);
	int			converted;

	CHECK_ENCODING_CONVERSION_ARGS(PG_WIN1250, PG_LATIN2);

	converted = local2local(src, dest, len, PG_WIN1250, PG_LATIN2,
							win1250_2_iso88592, noError);

	PG_RETURN_INT32(converted);
}
