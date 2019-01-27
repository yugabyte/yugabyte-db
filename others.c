#include "postgres.h"
#include <stdlib.h>
#include <locale.h>
#include "catalog/pg_operator.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "nodes/nodeFuncs.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parse_expr.h"
#include "parser/parse_oper.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "orafce.h"
#include "builtins.h"

/*
 * Source code for nlssort is taken from postgresql-nls-string
 * package by Jan Pazdziora
 */

static char *lc_collate_cache = NULL;
static size_t multiplication = 1;

text *def_locale = NULL;

PG_FUNCTION_INFO_V1(ora_lnnvl);

Datum
ora_lnnvl(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
		PG_RETURN_BOOL(true);

	PG_RETURN_BOOL(!PG_GETARG_BOOL(0));
}

PG_FUNCTION_INFO_V1(ora_concat);

Datum
ora_concat(PG_FUNCTION_ARGS)
{
	text *t1;
	text *t2;
	int l1;
	int l2;
	text *result;

	if (PG_ARGISNULL(0) && PG_ARGISNULL(1))
		PG_RETURN_NULL();

	if (PG_ARGISNULL(0))
		PG_RETURN_DATUM(PG_GETARG_DATUM(1));

	if (PG_ARGISNULL(1))
		PG_RETURN_DATUM(PG_GETARG_DATUM(0));

	t1 = PG_GETARG_TEXT_PP(0);
	t2 = PG_GETARG_TEXT_PP(1);

	l1 = VARSIZE_ANY_EXHDR(t1);
	l2 = VARSIZE_ANY_EXHDR(t2);

	result = palloc(l1+l2+VARHDRSZ);
	memcpy(VARDATA(result), VARDATA_ANY(t1), l1);
	memcpy(VARDATA(result) + l1, VARDATA_ANY(t2), l2);
	SET_VARSIZE(result, l1 + l2 + VARHDRSZ);

	PG_RETURN_TEXT_P(result);
}


PG_FUNCTION_INFO_V1(ora_nvl);

Datum
ora_nvl(PG_FUNCTION_ARGS)
{
	if (!PG_ARGISNULL(0))
		PG_RETURN_DATUM(PG_GETARG_DATUM(0));

	if (!PG_ARGISNULL(1))
		PG_RETURN_DATUM(PG_GETARG_DATUM(1));

	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ora_nvl2);

Datum
ora_nvl2(PG_FUNCTION_ARGS)
{
	if (!PG_ARGISNULL(0))
	{
		if (!PG_ARGISNULL(1))
			PG_RETURN_DATUM(PG_GETARG_DATUM(1));
	}
	else
	{
		if (!PG_ARGISNULL(2))
			PG_RETURN_DATUM(PG_GETARG_DATUM(2));
	}
	PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(ora_set_nls_sort);

Datum
ora_set_nls_sort(PG_FUNCTION_ARGS)
{
	text *arg = PG_GETARG_TEXT_P(0);

	if (def_locale != NULL)
	{
		pfree(def_locale);
		def_locale = NULL;
	}

	def_locale = (text*) MemoryContextAlloc(TopMemoryContext, VARSIZE(arg));
	memcpy(def_locale, arg, VARSIZE(arg));

	PG_RETURN_VOID();
}

static text*
_nls_run_strxfrm(text *string, text *locale)
{
	char *string_str;
	int string_len;

	char *locale_str = NULL;
	int locale_len = 0;

	text *result;
	char *tmp = NULL;
	size_t size = 0;
	size_t rest = 0;
	int changed_locale = 0;

	/*
	 * Save the default, server-wide locale setting.
	 * It should not change during the life-span of the server so it
	 * is safe to save it only once, during the first invocation.
	 */
	if (!lc_collate_cache)
	{
		if ((lc_collate_cache = setlocale(LC_COLLATE, NULL)))
			/* Make a copy of the locale name string. */
#ifdef _MSC_VER
			lc_collate_cache = _strdup(lc_collate_cache);
#else
			lc_collate_cache = strdup(lc_collate_cache);
#endif
		if (!lc_collate_cache)
			elog(ERROR, "failed to retrieve the default LC_COLLATE value");
	}

	/*
	 * To run strxfrm, we need a zero-terminated strings.
	 */
	string_len = VARSIZE_ANY_EXHDR(string);
	if (string_len < 0)
		return NULL;
	string_str = palloc(string_len + 1);
	memcpy(string_str, VARDATA_ANY(string), string_len);

	*(string_str + string_len) = '\0';

	if (locale)
	{
		locale_len = VARSIZE_ANY_EXHDR(locale);
	}

	/*
	 * If different than default locale is requested, call setlocale.
	 */
	if (locale_len > 0
		&& (strncmp(lc_collate_cache, VARDATA_ANY(locale), locale_len)
			|| *(lc_collate_cache + locale_len) != '\0'))
	{
		locale_str = palloc(locale_len + 1);
		memcpy(locale_str, VARDATA_ANY(locale), locale_len);
		*(locale_str + locale_len) = '\0';

		/*
		 * Try to set correct locales.
		 * If setlocale failed, we know the default stayed the same,
		 * co we can safely elog.
		 */
		if (!setlocale(LC_COLLATE, locale_str))
			elog(ERROR, "failed to set the requested LC_COLLATE value [%s]", locale_str);

		changed_locale = 1;
	}

	/*
	 * We do TRY / CATCH / END_TRY to catch ereport / elog that might
	 * happen during palloc. Ereport during palloc would not be
	 * nice since it would leave the server with changed locales
	 * setting, resulting in bad things.
	 */
	PG_TRY();
	{

		/*
		 * Text transformation.
		 * Increase the buffer until the strxfrm is able to fit.
		 */
		size = string_len * multiplication + 1;
		tmp = palloc(size + VARHDRSZ);

		rest = strxfrm(tmp + VARHDRSZ, string_str, size);
		while (rest >= size)
		{
			pfree(tmp);
			size = rest + 1;
			tmp = palloc(size + VARHDRSZ);
			rest = strxfrm(tmp + VARHDRSZ, string_str, size);
			/*
			 * Cache the multiplication factor so that the next
			 * time we start with better value.
			 */
			if (string_len)
				multiplication = (rest / string_len) + 2;
		}
	}
	PG_CATCH ();
	{
		if (changed_locale) {
			/*
			 * Set original locale
			 */
			if (!setlocale(LC_COLLATE, lc_collate_cache))
				elog(FATAL, "failed to set back the default LC_COLLATE value [%s]", lc_collate_cache);
		}
	}
	PG_END_TRY ();

	if (changed_locale)
	{
		/*
		 * Set original locale
		 */
		if (!setlocale(LC_COLLATE, lc_collate_cache))
			elog(FATAL, "failed to set back the default LC_COLLATE value [%s]", lc_collate_cache);
		pfree(locale_str);
	}
	pfree(string_str);

	/*
	 * If the multiplication factor went down, reset it.
	 */
	if (string_len && rest < string_len * multiplication / 4)
		multiplication = (rest / string_len) + 1;

	result = (text *) tmp;
	SET_VARSIZE(result, rest + VARHDRSZ);
	return result;
}

PG_FUNCTION_INFO_V1(ora_nlssort);

Datum
ora_nlssort(PG_FUNCTION_ARGS)
{
	text *locale;
	text *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	if (PG_ARGISNULL(1))
	{
		if (def_locale != NULL)
			locale = def_locale;
		else
		{
			locale = palloc(VARHDRSZ);
			SET_VARSIZE(locale, VARHDRSZ);
		}
	}
	else
	{
		locale = PG_GETARG_TEXT_PP(1);
	}

	result = _nls_run_strxfrm(PG_GETARG_TEXT_PP(0), locale);

	if (! result)
		PG_RETURN_NULL();

	PG_RETURN_BYTEA_P(result);
}

PG_FUNCTION_INFO_V1(ora_decode);

/*
 * decode(lhs, [rhs, ret], ..., [default])
 */
Datum
ora_decode(PG_FUNCTION_ARGS)
{
	int		nargs;
	int		i;
	int		retarg;

	/* default value is last arg or NULL. */
	nargs = PG_NARGS();
	if (nargs % 2 == 0)
	{
		retarg = nargs - 1;
		nargs -= 1;		/* ignore the last argument */
	}
	else
		retarg = -1;	/* NULL */

	if (PG_ARGISNULL(0))
	{
		for (i = 1; i < nargs; i += 2)
		{
			if (PG_ARGISNULL(i))
			{
				retarg = i + 1;
				break;
			}
		}
	}
	else
	{
		FmgrInfo   *eq;
		Oid		collation = PG_GET_COLLATION();

		/*
		 * On first call, get the input type's operator '=' and save at
		 * fn_extra.
		 */
		if (fcinfo->flinfo->fn_extra == NULL)
		{
			MemoryContext	oldctx;
			Oid				typid = get_fn_expr_argtype(fcinfo->flinfo, 0);
			Oid				eqoid = equality_oper_funcid(typid);

			oldctx = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);
			eq = palloc(sizeof(FmgrInfo));
			fmgr_info(eqoid, eq);
			MemoryContextSwitchTo(oldctx);

			fcinfo->flinfo->fn_extra = eq;
		}
		else
			eq = fcinfo->flinfo->fn_extra;

		for (i = 1; i < nargs; i += 2)
		{
			Datum					result;

			if (PG_ARGISNULL(i))
				continue;

			result = FunctionCall2Coll(eq,
									   collation,
									   PG_GETARG_DATUM(0),
									   PG_GETARG_DATUM(i));

			if (DatumGetBool(result))
			{
				retarg = i + 1;
				break;
			}
		}
	}

	if (retarg < 0 || PG_ARGISNULL(retarg))
		PG_RETURN_NULL();
	else
		PG_RETURN_DATUM(PG_GETARG_DATUM(retarg));
}

Oid
equality_oper_funcid(Oid argtype)
{
	Oid	eq;
	get_sort_group_operators(argtype, false, true, false, NULL, &eq, NULL, NULL);
	return get_opcode(eq);
}

/*
 * dump(anyexpr [,format])
 *
 *  the dump function returns a varchar2 value that includes the datatype code, 
 *  the length in bytes, and the internal representation of the expression.
 */
PG_FUNCTION_INFO_V1(orafce_dump);

static void
appendDatum(StringInfo str, const void *ptr, size_t length, int format)
{
	if (!PointerIsValid(ptr))
		appendStringInfoChar(str, ':');
	else
	{
		const unsigned char *s = (const unsigned char *) ptr;
		const char *formatstr;
		size_t	i;

		switch (format)
		{
			case 8:
				formatstr = "%ho";
				break;
			case 10: 
				formatstr = "%hu";
				break;
			case 16:
				formatstr = "%hx";
				break;
			case 17:
				formatstr = "%hc";
				break;
			default:
				elog(ERROR, "unknown format");
				formatstr  = NULL; 	/* quite compiler */
		}

		/* append a byte array with the specified format */
		for (i = 0; i < length; i++)
		{
			if (i > 0)
				appendStringInfoChar(str, ',');

			/* print only ANSI visible chars */
			if (format == 17 && (iscntrl(s[i]) || !isascii(s[i])))
				appendStringInfoChar(str, '?');
			else
				appendStringInfo(str, formatstr, s[i]);
		}
	}
}


Datum
orafce_dump(PG_FUNCTION_ARGS)
{
	Oid		valtype = get_fn_expr_argtype(fcinfo->flinfo, 0);
	List	*args;
	int16	typlen;
	bool	typbyval;
	Size	length;
	Datum	value;
	int		format;
	StringInfoData	str;

	if (!fcinfo->flinfo || !fcinfo->flinfo->fn_expr)
		elog(ERROR, "function is called from invalid context");

	if (PG_ARGISNULL(0))
		elog(ERROR, "argument is NULL");

	value = PG_GETARG_DATUM(0);
	format = PG_GETARG_IF_EXISTS(1, INT32, 10);

	args = ((FuncExpr *) fcinfo->flinfo->fn_expr)->args;
	valtype = exprType((Node *) list_nth(args, 0));

	get_typlenbyval(valtype, &typlen, &typbyval);
	length = datumGetSize(value, typbyval, typlen);

	initStringInfo(&str);
	appendStringInfo(&str, "Typ=%d Len=%d: ", valtype, (int) length);

	if (!typbyval)
	{
		appendDatum(&str, DatumGetPointer(value), length, format);
	}
	else if (length <= 1)
	{
		char	v = DatumGetChar(value);
		appendDatum(&str, &v, sizeof(char), format);
	}
	else if (length <= 2)
	{
		int16	v = DatumGetInt16(value);
		appendDatum(&str, &v, sizeof(int16), format);
	}
	else if (length <= 4)
	{
		int32	v = DatumGetInt32(value);
		appendDatum(&str, &v, sizeof(int32), format);
	}
	else
	{
		int64	v = DatumGetInt64(value);
		appendDatum(&str, &v, sizeof(int64), format);
	}

	PG_RETURN_TEXT_P(cstring_to_text(str.data));
}

PG_FUNCTION_INFO_V1(ora_get_major_version);


/*
 * Returns current version etc, PostgreSQL 9.6, PostgreSQL 10, ..
 */
Datum
ora_get_major_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(PACKAGE_STRING));
}

PG_FUNCTION_INFO_V1(ora_get_major_version_num);

/*
 * Returns major version number 9.5, 9.6, 10, 11, ..
 */
Datum
ora_get_major_version_num(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(PG_MAJORVERSION));
}

PG_FUNCTION_INFO_V1(ora_get_full_version_num);

/*
 * Returns version number string - 9.5.1, 10.2, ..
 */
Datum
ora_get_full_version_num(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(PG_VERSION));
}

PG_FUNCTION_INFO_V1(ora_get_platform);

/*
 * 32bit, 64bit
 */
Datum
ora_get_platform(PG_FUNCTION_ARGS)
{
#ifdef USE_FLOAT8_BYVAL
	PG_RETURN_TEXT_P(cstring_to_text("64bit"));
#else
	PG_RETURN_TEXT_P(cstring_to_text("32bit"));
#endif
}

PG_FUNCTION_INFO_V1(ora_get_status);

/*
 * Production | Debug
 */
Datum
ora_get_status(PG_FUNCTION_ARGS)
{
#ifdef USE_ASSERT_CHECKING
	PG_RETURN_TEXT_P(cstring_to_text("Debug"));
#else
	PG_RETURN_TEXT_P(cstring_to_text("Production"));
#endif
}
