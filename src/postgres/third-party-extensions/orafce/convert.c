#include <float.h>

#include "postgres.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "utils/pg_locale.h"
#include "utils/formatting.h"

#include "orafce.h"
#include "builtins.h"

#if PG_VERSION_NUM < 130000

#include "catalog/namespace.h"
#include "utils/memutils.h"

#endif

#if PG_VERSION_NUM >= 160000

#include "varatt.h"

#endif

PG_FUNCTION_INFO_V1(orafce_to_char_int4);
PG_FUNCTION_INFO_V1(orafce_to_char_int8);
PG_FUNCTION_INFO_V1(orafce_to_char_float4);
PG_FUNCTION_INFO_V1(orafce_to_char_float8);
PG_FUNCTION_INFO_V1(orafce_to_char_numeric);
PG_FUNCTION_INFO_V1(orafce_to_char_timestamp);
PG_FUNCTION_INFO_V1(orafce_to_number);
PG_FUNCTION_INFO_V1(orafce_to_multi_byte);
PG_FUNCTION_INFO_V1(orafce_to_single_byte);
PG_FUNCTION_INFO_V1(orafce_unistr);

static int getindex(const char **map, char *mbchar, int mblen);

#if PG_VERSION_NUM < 130000

static FmgrInfo *orafce_Utf8ToServerConvProc = NULL;

#endif

Datum
orafce_to_char_int4(PG_FUNCTION_ARGS)
{
	int32		arg0 = PG_GETARG_INT32(0);
	StringInfo	buf = makeStringInfo();

	appendStringInfo(buf, "%d", arg0);

	PG_RETURN_TEXT_P(cstring_to_text(buf->data));
}

Datum
orafce_to_char_int8(PG_FUNCTION_ARGS)
{
	int64		arg0 = PG_GETARG_INT64(0);
	StringInfo	buf = makeStringInfo();

	appendStringInfo(buf, INT64_FORMAT, arg0);

	PG_RETURN_TEXT_P(cstring_to_text(buf->data));
}

Datum
orafce_to_char_float4(PG_FUNCTION_ARGS)
{
	char	   *p;
	char	   *result;
	struct lconv *lconv = PGLC_localeconv();

	result = DatumGetCString(DirectFunctionCall1(float4out, PG_GETARG_DATUM(0)));

	for (p = result; *p; p++)
		if (*p == '.')
			*p = lconv->decimal_point[0];

	PG_RETURN_TEXT_P(cstring_to_text(result));
}

Datum
orafce_to_char_float8(PG_FUNCTION_ARGS)
{
	char	   *p;
	char	   *result;
	struct lconv *lconv = PGLC_localeconv();

	result = DatumGetCString(DirectFunctionCall1(float8out, PG_GETARG_DATUM(0)));

	for (p = result; *p; p++)
		if (*p == '.')
			*p = lconv->decimal_point[0];

	PG_RETURN_TEXT_P(cstring_to_text(result));
}

Datum
orafce_to_char_numeric(PG_FUNCTION_ARGS)
{
	Numeric		arg0 = PG_GETARG_NUMERIC(0);
	StringInfo	buf = makeStringInfo();
	struct lconv *lconv = PGLC_localeconv();
	char	   *p;
	char       *decimal = NULL;

	appendStringInfoString(buf, DatumGetCString(DirectFunctionCall1(numeric_out, NumericGetDatum(arg0))));

	for (p = buf->data; *p; p++)
		if (*p == '.')
		{
			*p = lconv->decimal_point[0];
			decimal = p; /* save decimal point position for the next loop */
		}

	/* Simulate the default Oracle to_char template (TM9 - Text Minimum)
	   by removing unneeded digits after the decimal point;
	   if no digits are left, then remove the decimal point too
	*/
	for (p = buf->data + buf->len - 1; decimal && p >= decimal; p--)
	{
		if (*p == '0' || *p == lconv->decimal_point[0])
			*p = 0;
		else
			break; /* non-zero digit found, exit the loop */
	}

	PG_RETURN_TEXT_P(cstring_to_text(buf->data));
}

/********************************************************************
 *
 * orafec_to_char_timestamp
 *
 * Syntax:
 *
 * text to_date(timestamp date_txt)
 *
 * Purpose:
 *
 * Returns date and time format w.r.t NLS_DATE_FORMAT GUC
 *
 *********************************************************************/

Datum
orafce_to_char_timestamp(PG_FUNCTION_ARGS)
{
	Timestamp ts = PG_GETARG_TIMESTAMP(0);
	text *result = NULL;

	if(nls_date_format && strlen(nls_date_format) > 0)
	{
		/* it will return the DATE in nls_date_format*/
		result = DatumGetTextP(DirectFunctionCall2(timestamp_to_char,
												   TimestampGetDatum(ts),
												   CStringGetTextDatum(nls_date_format)));
	}
	else
	{
		result = cstring_to_text(DatumGetCString(DirectFunctionCall1(timestamp_out,
									TimestampGetDatum(ts))));
	}

	PG_RETURN_TEXT_P(result);
}

Datum
orafce_to_number(PG_FUNCTION_ARGS)
{
	text	   *arg0 = PG_GETARG_TEXT_PP(0);
	char	   *buf;
	struct lconv *lconv = PGLC_localeconv();
	Numeric		res;
	char	   *p;

	if (VARSIZE_ANY_EXHDR(arg0) == 0)
		PG_RETURN_NULL();

	buf = text_to_cstring(arg0);

	for (p = buf; *p; p++)
		if (*p == lconv->decimal_point[0] && lconv->decimal_point[0])
			*p = '.';
		else if (*p == lconv->thousands_sep[0] && lconv->thousands_sep[0])
			*p = ',';

	res = DatumGetNumeric(DirectFunctionCall3(numeric_in, CStringGetDatum(buf), 0, -1));

	PG_RETURN_NUMERIC(res);
}

/* 3 is enough, but it is defined as 4 in backend code. */
#ifndef MAX_CONVERSION_GROWTH
#define MAX_CONVERSION_GROWTH  4
#endif

/*
 * Convert a tilde (~) to ...
 *	1: a full width tilde. (same as JA16EUCTILDE in oracle)
 *	0: a full width overline. (same as JA16EUC in oracle)
 *
 * Note - there is a difference with Oracle - it returns \342\210\274
 * what is a tilde char. Orafce returns fullwidth tilde. If it is a
 * problem, fix it for sef in code.
 */
#define JA_TO_FULL_WIDTH_TILDE	1

static const char *
TO_MULTI_BYTE_UTF8[95] =
{
	"\343\200\200",
	"\357\274\201",
	"\342\200\235",
	"\357\274\203",
	"\357\274\204",
	"\357\274\205",
	"\357\274\206",
	"\342\200\231",
	"\357\274\210",
	"\357\274\211",
	"\357\274\212",
	"\357\274\213",
	"\357\274\214",
	"\357\274\215",
	"\357\274\216",
	"\357\274\217",
	"\357\274\220",
	"\357\274\221",
	"\357\274\222",
	"\357\274\223",
	"\357\274\224",
	"\357\274\225",
	"\357\274\226",
	"\357\274\227",
	"\357\274\230",
	"\357\274\231",
	"\357\274\232",
	"\357\274\233",
	"\357\274\234",
	"\357\274\235",
	"\357\274\236",
	"\357\274\237",
	"\357\274\240",
	"\357\274\241",
	"\357\274\242",
	"\357\274\243",
	"\357\274\244",
	"\357\274\245",
	"\357\274\246",
	"\357\274\247",
	"\357\274\250",
	"\357\274\251",
	"\357\274\252",
	"\357\274\253",
	"\357\274\254",
	"\357\274\255",
	"\357\274\256",
	"\357\274\257",
	"\357\274\260",
	"\357\274\261",
	"\357\274\262",
	"\357\274\263",
	"\357\274\264",
	"\357\274\265",
	"\357\274\266",
	"\357\274\267",
	"\357\274\270",
	"\357\274\271",
	"\357\274\272",
	"\357\274\273",
	"\357\274\274",
	"\357\274\275",
	"\357\274\276",
	"\357\274\277",
	"\342\200\230",
	"\357\275\201",
	"\357\275\202",
	"\357\275\203",
	"\357\275\204",
	"\357\275\205",
	"\357\275\206",
	"\357\275\207",
	"\357\275\210",
	"\357\275\211",
	"\357\275\212",
	"\357\275\213",
	"\357\275\214",
	"\357\275\215",
	"\357\275\216",
	"\357\275\217",
	"\357\275\220",
	"\357\275\221",
	"\357\275\222",
	"\357\275\223",
	"\357\275\224",
	"\357\275\225",
	"\357\275\226",
	"\357\275\227",
	"\357\275\230",
	"\357\275\231",
	"\357\275\232",
	"\357\275\233",
	"\357\275\234",
	"\357\275\235",
#if JA_TO_FULL_WIDTH_TILDE
	"\357\275\236"
#else
	"\357\277\243"
#endif
};

static const char *
TO_MULTI_BYTE_EUCJP[95] =
{
	"\241\241",
	"\241\252",
	"\241\311",
	"\241\364",
	"\241\360",
	"\241\363",
	"\241\365",
	"\241\307",
	"\241\312",
	"\241\313",
	"\241\366",
	"\241\334",
	"\241\244",
	"\241\335",
	"\241\245",
	"\241\277",
	"\243\260",
	"\243\261",
	"\243\262",
	"\243\263",
	"\243\264",
	"\243\265",
	"\243\266",
	"\243\267",
	"\243\270",
	"\243\271",
	"\241\247",
	"\241\250",
	"\241\343",
	"\241\341",
	"\241\344",
	"\241\251",
	"\241\367",
	"\243\301",
	"\243\302",
	"\243\303",
	"\243\304",
	"\243\305",
	"\243\306",
	"\243\307",
	"\243\310",
	"\243\311",
	"\243\312",
	"\243\313",
	"\243\314",
	"\243\315",
	"\243\316",
	"\243\317",
	"\243\320",
	"\243\321",
	"\243\322",
	"\243\323",
	"\243\324",
	"\243\325",
	"\243\326",
	"\243\327",
	"\243\330",
	"\243\331",
	"\243\332",
	"\241\316",
	"\241\357",
	"\241\317",
	"\241\260",
	"\241\262",
	"\241\306",		/* Oracle returns different value \241\307 */
	"\243\341",
	"\243\342",
	"\243\343",
	"\243\344",
	"\243\345",
	"\243\346",
	"\243\347",
	"\243\350",
	"\243\351",
	"\243\352",
	"\243\353",
	"\243\354",
	"\243\355",
	"\243\356",
	"\243\357",
	"\243\360",
	"\243\361",
	"\243\362",
	"\243\363",
	"\243\364",
	"\243\365",
	"\243\366",
	"\243\367",
	"\243\370",
	"\243\371",
	"\243\372",
	"\241\320",
	"\241\303",
	"\241\321",
#if JA_TO_FULL_WIDTH_TILDE
	"\241\301"
#else
	"\241\261"
#endif
};

static const char *
TO_MULTI_BYTE__EUCCN[95] =
{
	"\241\241",
	"\243\241",
	"\243\242",
	"\243\243",
	"\243\244",
	"\243\245",
	"\243\246",
	"\243\247",
	"\243\250",
	"\243\251",
	"\243\252",
	"\243\253",
	"\243\254",
	"\243\255",
	"\243\256",
	"\243\257",
	"\243\260",
	"\243\261",
	"\243\262",
	"\243\263",
	"\243\264",
	"\243\265",
	"\243\266",
	"\243\267",
	"\243\270",
	"\243\271",
	"\243\272",
	"\243\273",
	"\243\274",
	"\243\275",
	"\243\276",
	"\243\277",
	"\243\300",
	"\243\301",
	"\243\302",
	"\243\303",
	"\243\304",
	"\243\305",
	"\243\306",
	"\243\307",
	"\243\310",
	"\243\311",
	"\243\312",
	"\243\313",
	"\243\314",
	"\243\315",
	"\243\316",
	"\243\317",
	"\243\320",
	"\243\321",
	"\243\322",
	"\243\323",
	"\243\324",
	"\243\325",
	"\243\326",
	"\243\327",
	"\243\330",
	"\243\331",
	"\243\332",
	"\243\333",
	"\243\334",
	"\243\335",
	"\243\336",
	"\243\337",
	"\243\340",
	"\243\341",
	"\243\342",
	"\243\343",
	"\243\344",
	"\243\345",
	"\243\346",
	"\243\347",
	"\243\350",
	"\243\351",
	"\243\352",
	"\243\353",
	"\243\354",
	"\243\355",
	"\243\356",
	"\243\357",
	"\243\360",
	"\243\361",
	"\243\362",
	"\243\363",
	"\243\364",
	"\243\365",
	"\243\366",
	"\243\367",
	"\243\370",
	"\243\371",
	"\243\372",
	"\243\373",
	"\243\374",
	"\243\375",
	"\243\376",
};

Datum
orafce_to_multi_byte(PG_FUNCTION_ARGS)
{
	text	   *src;
	text	   *dst;
	const char *s;
	char	   *d;
	int			srclen;

#if defined(_MSC_VER) && (defined(_M_X64) || defined(__amd64__))

	__int64			dstlen;

#else

	int			dstlen;

	#endif

	int			i;
	const char **map;

	switch (GetDatabaseEncoding())
	{
		case PG_UTF8:
			map = TO_MULTI_BYTE_UTF8;
			break;
		case PG_EUC_JP:
		case PG_EUC_JIS_2004:
			map = TO_MULTI_BYTE_EUCJP;
			break;
		case PG_EUC_CN:
			map = TO_MULTI_BYTE__EUCCN;
			break;
		/*
		 * TODO: Add converter for encodings.
		 */
		default:	/* no need to convert */
			PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	src = PG_GETARG_TEXT_PP(0);
	s = VARDATA_ANY(src);
	srclen = VARSIZE_ANY_EXHDR(src);
	dst = (text *) palloc(VARHDRSZ + srclen * MAX_CONVERSION_GROWTH);
	d = VARDATA(dst);

	for (i = 0; i < srclen; i++)
	{
		unsigned char	u = (unsigned char) s[i];
		if (0x20 <= u && u <= 0x7e)
		{
			const char *m = map[u - 0x20];
			while (*m)
			{
				*d++ = *m++;
			}
		}
		else
		{
			*d++ = s[i];
		}
	}

	dstlen = d - VARDATA(dst);
	SET_VARSIZE(dst, VARHDRSZ + dstlen);

	PG_RETURN_TEXT_P(dst);
}

static int
getindex(const char **map, char *mbchar, int mblen)
{
	int		i;

	for (i = 0; i < 95; i++)
	{
		if (!memcmp(map[i], mbchar, mblen))
			return i;
	}

	return -1;
}

Datum
orafce_to_single_byte(PG_FUNCTION_ARGS)
{
	text	   *src;
	text	   *dst;
	char	   *s;
	char	   *d;
	int			srclen;

#if defined(_MSC_VER) && (defined(_M_X64) || defined(__amd64__))

	__int64			dstlen;

#else
	
	int			dstlen;

#endif

	const char **map;

	switch (GetDatabaseEncoding())
	{
		case PG_UTF8:
			map = TO_MULTI_BYTE_UTF8;
			break;
		case PG_EUC_JP:
		case PG_EUC_JIS_2004:
			map = TO_MULTI_BYTE_EUCJP;
			break;
		case PG_EUC_CN:
			map = TO_MULTI_BYTE__EUCCN;
			break;
		/*
		 * TODO: Add converter for encodings.
		 */
		default:	/* no need to convert */
			PG_RETURN_DATUM(PG_GETARG_DATUM(0));
	}

	src = PG_GETARG_TEXT_PP(0);
	s = VARDATA_ANY(src);
	srclen = VARSIZE_ANY_EXHDR(src);

	/* XXX - The output length should be <= input length */
	dst = (text *) palloc0(VARHDRSZ + srclen);
	d = VARDATA(dst);

	while (s - VARDATA_ANY(src) < srclen)
	{
		char   *u = s;
		int		clen;
		int		mapindex;

		clen = pg_mblen(u);
		s += clen;

		if (clen == 1)
			*d++ = *u;
		else if ((mapindex = getindex(map, u, clen)) >= 0)
		{
			const char m = 0x20 + mapindex;
			*d++ = m;
		}
		else
		{
			memcpy(d, u, clen);
			d += clen;
		}
	}

	dstlen = d - VARDATA(dst);
	SET_VARSIZE(dst, VARHDRSZ + dstlen);

	PG_RETURN_TEXT_P(dst);
}

/* convert hex digit (caller should have verified that) to value */
static unsigned int
hexval(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 0xA;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 0xA;
	elog(ERROR, "invalid hexadecimal digit");
	return 0;					/* not reached */
}


/*
 * First four chars should be hexnum digits
 */
static bool
isxdigit_four(const char *instr)
{
	return isxdigit((unsigned char)  instr[0]) &&
			isxdigit((unsigned char) instr[1]) &&
			isxdigit((unsigned char) instr[2]) &&
			isxdigit((unsigned char) instr[3]);
}

/*
 * Translate string with hexadecimal digits to number
 */
static long int
hexval_four(const char *instr)
{
	return (hexval(instr[0]) << 12) +
			(hexval(instr[1]) << 8) +
			(hexval(instr[2]) << 4) +
			 hexval(instr[3]);
}

#if PG_VERSION_NUM < 130000


static bool
is_utf16_surrogate_first(pg_wchar c)
{
	return (c >= 0xD800 && c <= 0xDBFF);
}

static bool
is_utf16_surrogate_second(pg_wchar c)
{
	return (c >= 0xDC00 && c <= 0xDFFF);
}

static pg_wchar
surrogate_pair_to_codepoint(pg_wchar first, pg_wchar second)
{
	return ((first & 0x3FF) << 10) + 0x10000 + (second & 0x3FF);
}

static inline bool
is_valid_unicode_codepoint(pg_wchar c)
{
	return (c > 0 && c <= 0x10FFFF);
}

#define MAX_UNICODE_EQUIVALENT_STRING	16

/*
 * Convert a single Unicode code point into a string in the server encoding.
 *
 * The code point given by "c" is converted and stored at *s, which must
 * have at least MAX_UNICODE_EQUIVALENT_STRING+1 bytes available.
 * The output will have a trailing '\0'.  Throws error if the conversion
 * cannot be performed.
 *
 * Note that this relies on having previously looked up any required
 * conversion function.  That's partly for speed but mostly because the parser
 * may call this outside any transaction, or in an aborted transaction.
 */
static void
pg_unicode_to_server(pg_wchar c, unsigned char *s)
{
	unsigned char c_as_utf8[MAX_MULTIBYTE_CHAR_LEN + 1];
	int			c_as_utf8_len;
	int			server_encoding;

	/*
	 * Complain if invalid Unicode code point.  The choice of errcode here is
	 * debatable, but really our caller should have checked this anyway.
	 */
	if (!is_valid_unicode_codepoint(c))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("invalid Unicode code point")));

	/* Otherwise, if it's in ASCII range, conversion is trivial */
	if (c <= 0x7F)
	{
		s[0] = (unsigned char) c;
		s[1] = '\0';
		return;
	}

	/* If the server encoding is UTF-8, we just need to reformat the code */
	server_encoding = GetDatabaseEncoding();
	if (server_encoding == PG_UTF8)
	{
		unicode_to_utf8(c, s);
		s[pg_utf_mblen(s)] = '\0';
		return;
	}

	/* For all other cases, we must have a conversion function available */
	if (orafce_Utf8ToServerConvProc == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("conversion between UTF8 and %s is not supported",
						GetDatabaseEncodingName())));

	/* Construct UTF-8 source string */
	unicode_to_utf8(c, c_as_utf8);
	c_as_utf8_len = pg_utf_mblen(c_as_utf8);
	c_as_utf8[c_as_utf8_len] = '\0';

	/* Convert, or throw error if we can't */
	FunctionCall5(orafce_Utf8ToServerConvProc,
				  Int32GetDatum(PG_UTF8),
				  Int32GetDatum(server_encoding),
				  CStringGetDatum(c_as_utf8),
				  CStringGetDatum(s),
				  Int32GetDatum(c_as_utf8_len));
}

static void
initializeUtf8ToServerConvProc(void)
{
	int			current_server_encoding;

	orafce_Utf8ToServerConvProc = NULL;

	/*
	 * Also look up the UTF8-to-server conversion function if needed.  Since
	 * the server encoding is fixed within any one backend process, we don't
	 * have to do this more than once.
	 */
	current_server_encoding = GetDatabaseEncoding();
	if (current_server_encoding != PG_UTF8 &&
		current_server_encoding != PG_SQL_ASCII)
	{
		Oid			utf8_to_server_proc;

		utf8_to_server_proc =
			FindDefaultConversionProc(PG_UTF8,
									  current_server_encoding);
		/* If there's no such conversion, just leave the pointer as NULL */
		if (OidIsValid(utf8_to_server_proc))
		{
			FmgrInfo   *finfo;

			finfo = (FmgrInfo *) MemoryContextAlloc(TopMemoryContext,
													sizeof(FmgrInfo));
			fmgr_info_cxt(utf8_to_server_proc, finfo,
						  TopMemoryContext);
			/* Set Utf8ToServerConvProc only after data is fully valid */
			orafce_Utf8ToServerConvProc = finfo;
		}
	}
}

#endif

/* is Unicode code point acceptable? */
static void
check_unicode_value(pg_wchar c)
{
	if (!is_valid_unicode_codepoint(c))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("invalid Unicode escape value")));
}

/*
 * Replaces unicode escape sequences by unicode chars
 */
Datum
orafce_unistr(PG_FUNCTION_ARGS)
{
	StringInfoData		str;
	text	   *input_text;
	text	   *result;
	pg_wchar	pair_first = 0;
	char		cbuf[MAX_UNICODE_EQUIVALENT_STRING + 1];
	char	   *instr;
	int			len;

	/* when input string is NULL, then result is NULL too */
	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();

	input_text = PG_GETARG_TEXT_PP(0);
	instr = VARDATA_ANY(input_text);
	len = VARSIZE_ANY_EXHDR(input_text);

	initStringInfo(&str);

#if PG_VERSION_NUM < 130000

	initializeUtf8ToServerConvProc();

#endif

	while (len > 0)
	{
		if (instr[0] == '\\')
		{
			if (len >= 2 &&
				instr[1] == '\\')
			{
				if (pair_first)
					goto invalid_pair;
				appendStringInfoChar(&str, '\\');
				instr += 2;
				len -= 2;
			}
			else if ((len >= 5 && isxdigit_four(&instr[1])) ||
					 (len >= 6 && instr[1] == 'u' && isxdigit_four(&instr[2])))
			{
				pg_wchar	unicode;
				int			offset = instr[1] == 'u' ? 2 : 1;

				unicode = hexval_four(instr + offset);

				check_unicode_value(unicode);

				if (pair_first)
				{
					if (is_utf16_surrogate_second(unicode))
					{
						unicode = surrogate_pair_to_codepoint(pair_first, unicode);
						pair_first = 0;
					}
					else
						goto invalid_pair;
				}
				else if (is_utf16_surrogate_second(unicode))
					goto invalid_pair;

				if (is_utf16_surrogate_first(unicode))
					pair_first = unicode;
				else
				{
					pg_unicode_to_server(unicode, (unsigned char *) cbuf);
					appendStringInfoString(&str, cbuf);
				}

				instr += 4 + offset;
				len -= 4 + offset;
			}
			else if (len >= 8 &&
					 instr[1] == '+' &&
					 isxdigit_four(&instr[2]) &&
					 isxdigit((unsigned char) instr[6]) &&
					 isxdigit((unsigned char) instr[7]))
			{
				pg_wchar	unicode;

				unicode = (hexval_four(&instr[2]) << 8) +
								(hexval(instr[6]) << 4) +
								 hexval(instr[7]);

				check_unicode_value(unicode);

				if (pair_first)
				{
					if (is_utf16_surrogate_second(unicode))
					{
						unicode = surrogate_pair_to_codepoint(pair_first, unicode);
						pair_first = 0;
					}
					else
						goto invalid_pair;
				}
				else if (is_utf16_surrogate_second(unicode))
					goto invalid_pair;

				if (is_utf16_surrogate_first(unicode))
					pair_first = unicode;
				else
				{
					pg_unicode_to_server(unicode, (unsigned char *) cbuf);
					appendStringInfoString(&str, cbuf);
				}

				instr += 8;
				len -= 8;
			}
			else if (len >= 10 &&
					 instr[1] == 'U' &&
					 isxdigit_four(&instr[2]) &&
					 isxdigit_four(&instr[6]))
			{
				pg_wchar	unicode;

				unicode = (hexval_four(&instr[2]) << 16) + hexval_four(&instr[6]);

				check_unicode_value(unicode);

				if (pair_first)
				{
					if (is_utf16_surrogate_second(unicode))
					{
						unicode = surrogate_pair_to_codepoint(pair_first, unicode);
						pair_first = 0;
					}
					else
						goto invalid_pair;
				}
				else if (is_utf16_surrogate_second(unicode))
					goto invalid_pair;

				if (is_utf16_surrogate_first(unicode))
					pair_first = unicode;
				else
				{
					pg_unicode_to_server(unicode, (unsigned char *) cbuf);
					appendStringInfoString(&str, cbuf);
				}

				instr += 10;
				len -= 10;
			}
			else
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("invalid Unicode escape"),
						 errhint("Unicode escapes must be \\XXXX, \\+XXXXXX, \\uXXXX or \\UXXXXXXXX.")));
		}
		else
		{
			if (pair_first)
				goto invalid_pair;

			appendStringInfoChar(&str, *instr++);
			len--;
		}
	}

	/* unfinished surrogate pair? */
	if (pair_first)
		goto invalid_pair;

	result = cstring_to_text_with_len(str.data, str.len);
	pfree(str.data);

	PG_RETURN_TEXT_P(result);

invalid_pair:
	ereport(ERROR,
			(errcode(ERRCODE_SYNTAX_ERROR),
			 errmsg("invalid Unicode surrogate pair")));

#if defined(_MSC_VER)

	/* be MSVC quiet */
	return 0;

#endif
}
