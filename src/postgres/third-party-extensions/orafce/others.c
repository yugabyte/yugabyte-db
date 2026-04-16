#include "postgres.h"
#include <stdlib.h>
#include <locale.h>

#if PG_VERSION_NUM < 160000

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/sysattr.h"

#if PG_VERSION_NUM >= 120000

#include "access/table.h"

#endif

#endif

#include "catalog/indexing.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/extension.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "nodes/nodeFuncs.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"
#include "parser/parse_expr.h"
#include "parser/parse_oper.h"
#include "storage/lock.h"
#include "storage/proc.h"
#include "utils/array.h"
#include "utils/arrayaccess.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
#include "utils/uuid.h"
#include "orafce.h"
#include "builtins.h"

/*
 * Source code for nlssort is taken from postgresql-nls-string
 * package by Jan Pazdziora
 */

static char *lc_collate_cache = NULL;
static size_t multiplication = 1;

text *def_locale = NULL;

char *orafce_sys_guid_source;

static Oid uuid_generate_func_oid = InvalidOid;
static FmgrInfo uuid_generate_func_finfo;

/* The oid of function should be valid in oene transaction */
static LocalTransactionId uuid_generate_func_lxid = InvalidLocalTransactionId;
static char uuid_generate_func_name[30] = "";

static Datum ora_greatest_least(FunctionCallInfo fcinfo, bool greater);

#if PG_VERSION_NUM >= 170000

#define CURRENT_LXID	(MyProc->vxid.lxid)

#else

#define CURRENT_LXID	(MyProc->lxid)

#endif

#if PG_VERSION_NUM < 160000

static Oid
get_extension_schema(Oid ext_oid)
{
	Oid			result;
	Relation	rel;
	SysScanDesc scandesc;
	HeapTuple	tuple;
	ScanKeyData entry[1];

#if PG_VERSION_NUM >= 120000

	rel = table_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&entry[0],
				Anum_pg_extension_oid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(ext_oid));

#else

	rel = heap_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&entry[0],
				ObjectIdAttributeNumber,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(ext_oid));

#endif

	scandesc = systable_beginscan(rel, ExtensionOidIndexId, true,
								  NULL, 1, entry);

	tuple = systable_getnext(scandesc);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
		result = ((Form_pg_extension) GETSTRUCT(tuple))->extnamespace;
	else
		result = InvalidOid;

	systable_endscan(scandesc);

#if PG_VERSION_NUM >= 120000

	table_close(rel, AccessShareLock);

#else

	heap_close(rel, AccessShareLock);

#endif

	return result;
}

#endif

static Oid
get_uuid_generate_func_oid(bool *reset_fmgr)
{
	Oid			result = InvalidOid;

	if (uuid_generate_func_lxid != CURRENT_LXID ||
		uuid_generate_func_oid == InvalidOid ||
		strcmp(orafce_sys_guid_source, uuid_generate_func_name) != 0)
	{
		if (strcmp(orafce_sys_guid_source, "gen_random_uuid") == 0)
		{
			/* generated uuid can have not nice features, but uses buildin functionality */
			result = fmgr_internal_function("gen_random_uuid");
		}
		else
		{
			Oid uuid_ossp_oid = InvalidOid;
			Oid uuid_ossp_namespace_oid = InvalidOid;
			CatCList   *catlist;
			int			i;

			uuid_ossp_oid = get_extension_oid("uuid-ossp", true);
			if (!OidIsValid(uuid_ossp_oid))
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_OBJECT),
						 errmsg("extension \"uuid-ossp\" is not installed"),
						 errhint("the extension \"uuid-ossp\" should be installed before using \"sys_guid\" function")));

			uuid_ossp_namespace_oid = get_extension_schema(uuid_ossp_oid);
			Assert(OidIsValid(uuid_ossp_namespace_oid));

			/* Search syscache by name only */
			catlist = SearchSysCacheList1(PROCNAMEARGSNSP,
										  CStringGetDatum(orafce_sys_guid_source));

			for (i = 0; i < catlist->n_members; i++)
			{
				HeapTuple	proctup = &catlist->members[i]->tuple;
				Form_pg_proc procform = (Form_pg_proc) GETSTRUCT(proctup);

				/*
				 * Consider only procs in specified namespace,
				 * with zero arguments and uuid type as result
				 */
				if (procform->pronamespace != uuid_ossp_namespace_oid ||
					 procform->pronargs != 0 || procform->prorettype != UUIDOID)
					continue;

#if PG_VERSION_NUM >= 120000

				result = procform->oid;

#else

				result = HeapTupleGetOid(proctup);

#endif

				break;
			}

			ReleaseSysCacheList(catlist);
		}

		/* should be available if extension uuid-ossp is installed */
		if (!OidIsValid(result))
			elog(ERROR, "function \"%s\" doesn't exist", orafce_sys_guid_source);

		uuid_generate_func_lxid = CURRENT_LXID;
		uuid_generate_func_oid = result;
		strcpy(uuid_generate_func_name, orafce_sys_guid_source);
		*reset_fmgr = true;
	}
	else
		*reset_fmgr = false;

	Assert(OidIsValid(uuid_generate_func_oid));

	return uuid_generate_func_oid;
}

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
	char	   *string_str;
	int			string_len;
	char	   *locale_str = NULL;
	text		*result;
	char	   *tmp = NULL;
	size_t		rest = 0;
	bool		changed_locale = false;

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
		int			locale_len = VARSIZE_ANY_EXHDR(locale);

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

			changed_locale = true;
		}
	}

	/*
	 * We do TRY / CATCH / END_TRY to catch ereport / elog that might
	 * happen during palloc. Ereport during palloc would not be
	 * nice since it would leave the server with changed locales
	 * setting, resulting in bad things.
	 */
	PG_TRY();
	{
		size_t		size = 0;

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

		PG_RE_THROW();
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
	int16	typlen;
	bool	typbyval;
	Size	length;
	Datum	value;
	int		format;
	StringInfoData	str;

	if (!OidIsValid(valtype))
		elog(ERROR, "function is called from invalid context");

	if (PG_ARGISNULL(0))
		elog(ERROR, "argument is NULL");

	value = PG_GETARG_DATUM(0);
	format = PG_GETARG_IF_EXISTS(1, INT32, 10);

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

PG_FUNCTION_INFO_V1(ora_greatest);

/*
 * ora_greatest(anyarray) returns anyelement
 */
Datum
ora_greatest(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(ora_greatest_least(fcinfo, true));
}

PG_FUNCTION_INFO_V1(ora_least);

/*
 * ora_least(anyarray) returns anyelement
 */
Datum
ora_least(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(ora_greatest_least(fcinfo, false));
}

/*
 * ora_greatest_least(anyarray, bool) returns anyelement
 * Boolean parameter is true for greatest and false for least.
 */
static Datum
ora_greatest_least(FunctionCallInfo fcinfo, bool greater)
{
	Datum		result;
	Datum		value;
	ArrayType	*array;
	ArrayIterator array_iterator;
	Oid		element_type;
	Oid		collation = PG_GET_COLLATION();
	ArrayMetaState *my_extra = NULL;
	bool	isnull;

	/* caller functions are marked as strict */
	Assert(!PG_ARGISNULL(0));
	Assert(!PG_ARGISNULL(1));

	array = PG_GETARG_ARRAYTYPE_P(1);
	element_type = ARR_ELEMTYPE(array);

	Assert(element_type == get_fn_expr_argtype(fcinfo->flinfo, 0));

	/* fast return */
	if (array_contains_nulls(array))
		PG_RETURN_NULL();

	my_extra = (ArrayMetaState *) fcinfo->flinfo->fn_extra;
	if (my_extra == NULL)
	{
		my_extra = MemoryContextAlloc(fcinfo->flinfo->fn_mcxt,
													  sizeof(ArrayMetaState));
		my_extra->element_type = ~element_type;

		fcinfo->flinfo->fn_extra = my_extra;
	}

	if (my_extra->element_type != element_type)
	{
		Oid		sortop_oid;

		get_typlenbyvalalign(element_type,
							 &my_extra->typlen,
							 &my_extra->typbyval,
							 &my_extra->typalign);

		if (greater)
			get_sort_group_operators(element_type, false, false, true,
									 NULL, NULL, &sortop_oid, NULL);
		else
			get_sort_group_operators(element_type, true, false, false,
									 &sortop_oid, NULL, NULL, NULL);

		my_extra->element_type = element_type;

		fmgr_info_cxt(get_opcode(sortop_oid), &my_extra->proc,
					  fcinfo->flinfo->fn_mcxt);
	}

	/* Let's return the first parameter by default */
	result = PG_GETARG_DATUM(0);

	array_iterator = array_create_iterator(array, 0, my_extra);
	while (array_iterate(array_iterator, &value, &isnull))
	{
		/* not nulls, so run the operator */
		if (!DatumGetBool(FunctionCall2Coll(&my_extra->proc, collation,
										   result, value)))
			result = value;
	}

	result = datumCopy(result, my_extra->typbyval, my_extra->typlen);

	array_free_iterator(array_iterator);

	/* Avoid leaking memory when handed toasted input */
	PG_FREE_IF_COPY(array, 1);

	PG_RETURN_DATUM(result);
}

#if PG_VERSION_NUM < 120000

static Datum
FunctionCall0Coll(FmgrInfo *flinfo, Oid collation)
{
	FunctionCallInfoData fcinfo_data;
	FunctionCallInfo fcinfo = &fcinfo_data;
	Datum		result;

	InitFunctionCallInfoData(*fcinfo, flinfo, 0, collation, NULL, NULL);

	result = FunctionCallInvoke(fcinfo);

	/* Check for null result, since caller is clearly not expecting one */
	if (fcinfo->isnull)
		elog(ERROR, "function %u returned NULL", flinfo->fn_oid);

	return result;
}

#endif


PG_FUNCTION_INFO_V1(orafce_sys_guid);

/*
 * Implementation of sys_guid() function
 *
 * Oracle uses guid based on mac address. The calculation is not too
 * difficult, but there are lot of depenedencies necessary for taking
 * mac address. Instead to making some static dependecies orafce uses
 * dynamic dependency on "uuid-ossp" extension, and calls choosed function
 * from this extension.
 */
Datum
orafce_sys_guid(PG_FUNCTION_ARGS)
{
	bool		reset_fmgr;
	Oid			funcoid;
	pg_uuid_t  *uuid;
	bytea	   *result;

	funcoid = get_uuid_generate_func_oid(&reset_fmgr);

	if (reset_fmgr)
		fmgr_info_cxt(funcoid,
					   &uuid_generate_func_finfo,
					  TopTransactionContext);

	uuid =  DatumGetUUIDP(FunctionCall0Coll(&uuid_generate_func_finfo,
										    InvalidOid));

	result = palloc(VARHDRSZ + UUID_LEN);
	SET_VARSIZE(result, VARHDRSZ + UUID_LEN);
	memcpy(VARDATA(result), uuid->data, UUID_LEN);

	PG_RETURN_BYTEA_P(result);
}
