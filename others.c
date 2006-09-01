#include "postgres.h"
#include "utils/memutils.h"
#include "fmgr.h"
#include "orafunc.h"
#include <stdlib.h>
#include <locale.h>

/*
 * Source code for nlssort is taken from postgresql-nls-string 
 * package by Jan Pazdziora
 *
 */

Datum ora_nvl(PG_FUNCTION_ARGS);
Datum ora_nvl2(PG_FUNCTION_ARGS);
Datum ora_concat(PG_FUNCTION_ARGS);
Datum ora_nlssort(PG_FUNCTION_ARGS);
Datum ora_set_nls_sort(PG_FUNCTION_ARGS);
Datum ora_lnnvl(PG_FUNCTION_ARGS);

static char *lc_collate_cache = NULL;
static int multiplication = 1;

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
		PG_RETURN_TEXT_P(PG_GETARG_TEXT_P(1));
	
    if (PG_ARGISNULL(1))
		PG_RETURN_TEXT_P(PG_GETARG_TEXT_P(0));

    t1 = PG_GETARG_TEXT_P(0);
    t2 = PG_GETARG_TEXT_P(1);    
    
    l1 = VARSIZE(t1) - VARHDRSZ;
    l2 = VARSIZE(t2) - VARHDRSZ;
    
    result = palloc(l1+l2+VARHDRSZ);
    memcpy(VARDATA(result), VARDATA(t1), l1);
    memcpy(VARDATA(result) + l1, VARDATA(t2), l2);
    VARATT_SIZEP(result) = l1 + l2 + VARHDRSZ;
    
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
		PG_RETURN_DATUM(PG_GETARG_DATUM(0));

    if (!PG_ARGISNULL(1))
		PG_RETURN_DATUM(PG_GETARG_DATUM(1));

    if (!PG_ARGISNULL(2))
		PG_RETURN_DATUM(PG_GETARG_DATUM(1));

    PG_RETURN_NULL();
} 


PG_FUNCTION_INFO_V1(ora_set_nls_sort);

Datum
ora_set_nls_sort(PG_FUNCTION_ARGS)
{
	text *arg = PG_GETARG_TEXT_P(0);

	if (def_locale != NULL)
		pfree(def_locale);

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
			lc_collate_cache = strdup(lc_collate_cache);
		if (!lc_collate_cache)
			elog(ERROR, "failed to retrieve the default LC_COLLATE value");
	}

	/*
	 * To run strxfrm, we need a zero-terminated strings.
	 */
	string_len = VARSIZE(string) - VARHDRSZ;
	if (string_len < 0)
		return NULL;
	string_str = palloc(string_len + 1);
	memcpy(string_str, VARDATA(string), string_len);

	*(string_str + string_len) = '\0';

	if (locale)
	{
		locale_len = VARSIZE(locale) - VARHDRSZ;
	}
	/*
	 * If different than default locale is requested, call setlocale.
	 */
	if (locale_len > 0
		&& (strncmp(lc_collate_cache, VARDATA(locale), locale_len)
			|| *(lc_collate_cache + locale_len) != '\0'))
	{
		locale_str = palloc(locale_len + 1);
		memcpy(locale_str, VARDATA(locale), locale_len);
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
	result->vl_len = rest + VARHDRSZ;
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
			locale->vl_len = 0;
		}
	}
	else
	{
		locale = PG_GETARG_TEXT_P(1);
	}

	result = _nls_run_strxfrm(PG_GETARG_TEXT_P(0), locale);

	if (! result)
		PG_RETURN_NULL();

	PG_RETURN_BYTEA_P(result);
}
