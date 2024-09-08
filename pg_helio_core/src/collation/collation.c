/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/collation/collation.c
 *
 * Implementation of the backend query generation for pipelines.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <unicode/ures.h>
#include <unicode/uloc.h>
#include <utils/hsearch.h>
#include <utils/memutils.h>
#include <unicode/umachine.h>
#include <utils/pg_locale.h>


#include "io/helio_bson_core.h"
#include "lib/stringinfo.h"
#include "utils/helio_errors.h"
#include "collation/collation.h"

#define ALPHABET_SIZE 26
#define DEFAULT_ICU_COLLATION_SORT_KEY_LENGTH 512

typedef struct
{
	unsigned long collationKey;
	UCollator *collator; /* locale_t struct, or 0 if not valid */
} ucollator_cache_entry;

/*
 *
 * Pre-initialized 2D array that represets `locale` string. Array[0][0] represents locale 'aa', Array[0][1] represents locale 'ab' and so on.
 * Note that locales are case-sensitive.
 *
 * Following are the meaning of the array values:
 *
 * ' ' -> locale does not exist. E.g., in this matrix below, locale 'aa' does not exists and so on.
 * 't' -> Locale exists, and no other locale starts with the corresponding two letter locale. E.g., locale 'am' exists
 *      but, no other locale starts with prefix 'am'.
 * '<Capital Letter>' -> Locale exists with a prefix represented by the locale matrix.
 *
 * For example, code 'A' represents either 'fr' or 'fr_CA'.
 * So, when we look up code for locale prefix 'fr' and get a code 'A', we know that the locale could only be 'fr' or 'fr_CA'.
 * With the help of this matrix we can efficiently tell,
 *  (1) fr is supported, when we get code 'A' and check that the input code length is 2.
 *  (2) fr_CA is supported, when we get code 'A', and we do a string comaprison with 'fr_CA' when code length > 2
 *  (3) fr_US is not supported, when we get code 'A', and we do a string comaprison with 'fr_CA' when code length > 2
 * The same logic applies for all the following special codes. There are some three letter codes that follow the same framework.
 *
 * 'A' -> fr, fr_CA
 * 'B' -> dsb
 * 'C' -> xx, xx@collation=compat [ar]
 * 'D' -> de, de@collation=search, de@collation=phonebook, de@collation=eor, de_AT, de_AT@collation=phonebook
 * 'E' -> en, en_US_POSIX, en_US
 * 'F' -> fa, fa_AF
 * 'G' -> bs, bs@collation=search, bs_Cyrl
 * 'H' -> ha, haw
 * 'I' -> fi, fil, fi@collation=search, fi@collation=traditional
 * 'J' -> ja, ja@collation=unihan
 * 'K' -> ko, kok, ko@collation=search, ko@collation=searchjl, ko@collation=unihan
 * 'L' -> sr, sr_Latin, sr_Latn@collation=search
 * 'M' -> lkt
 * 'N' -> smn, smn@collation=search
 * 'O' -> hsb
 * 'P' -> xx, xx@collation=phonetic [ln]
 * 'Q' -> es, es@collation=search, es@collation=traditional
 * 'R' -> chr
 * 'S' -> xx, xx@collation=search  [az, ca, hr, cs, da, fo, gl, he, is, kl, se, nb, sk, sv, tr, yi]
 * 'T' -> xx, xx@collation=traditional [bn, kn, vi]
 * 'W' -> wae
 * 'Y' -> si, si@collation=dictionary
 * 'Z' -> zh, zh@collation=big5han, zh@collation=gb2312han, zh@collation=unihan, zh@collation=zhuyin, zh_Hant
 */
char supported_locale_codes[ALPHABET_SIZE][ALPHABET_SIZE] = {
	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* a */
	{ ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ',
	  ' ', ' ', 'C', 't', ' ', ' ', ' ', ' ', ' ', ' ', 'S' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* b */ { ' ', ' ', ' ', ' ', 't', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', 'T', 't',
			  ' ', ' ', ' ', 'G', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* c */ { 'S', ' ', ' ', ' ', ' ', ' ', ' ', 'R', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', 'S', ' ', ' ', ' ', ' ', ' ', 't', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* d */ { 'S', ' ', ' ', ' ', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', 'B', ' ', ' ', ' ', ' ', ' ', ' ', 't' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* e */ { ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ', 'E', 't',
			  ' ', ' ', ' ', 'Q', 't', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* f */ { 'F', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'I', ' ', ' ', ' ', ' ', ' ', 'S',
			  ' ', ' ', 'A', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* g */ { 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'S', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* h */ { 'H', ' ', ' ', ' ', 'S', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', 'S', 'O', ' ', 't', ' ', ' ', ' ', 't', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* i */ { ' ', ' ', ' ', 't', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', 'S', 't', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* j */ { 'J', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* k */ { 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'M', 'S', 't', 'T', 'K',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* l */ { ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'C', ' ', ' ', 'P', 't',
			  ' ', ' ', ' ', ' ', 't', ' ', 't', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* m */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 't', 't', ' ', 't', ' ',
			  ' ', ' ', 't', 't', 't', ' ', ' ', ' ', ' ', 't', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* n */ { ' ', 'S', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ', 'S', ' ',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* o */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ',
			  ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* p */ { 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ',
			  ' ', ' ', ' ', 't', 't', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* q */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* r */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 't',
			  ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* s */ { ' ', ' ', ' ', ' ', 'S', ' ', ' ', ' ', 'Y', ' ', 'S', 't', 'N', ' ', ' ',
			  ' ', 't', 'L', ' ', ' ', ' ', 'S', 't', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* t */ { 't', ' ', ' ', ' ', 't', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', 't',
			  ' ', ' ', 'S', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* u */ { ' ', ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ',
			  ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* v */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'T', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* w */ { 'W', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* x */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* y */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'S', ' ', ' ', ' ', ' ', ' ', 't',
			  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },

	/*         p    q    r    s    t    u    v    w    x    y    z  */

	/*         a    b    c    d    e    f    g    h    i    j    k    l    m    n    o  */
	/* z */ { ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'Z', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
			  ' ', ' ', ' ', ' ', ' ', 't', ' ', ' ', ' ', ' ', ' ' }

	/*         p    q    r    s    t    u    v    w    x    y    z  */
};

static HTAB *collation_cache = NULL;

static ucollator_cache_entry * LookupUCollatorCache(const char *collationString);

inline static void CheckCollationInputParamType(bson_type_t expectedType, bson_type_t
												foundType, const char *paramName);

inline static bool CheckIfValidLocale(const char *locale);
inline static void ThrowInvalidLocaleError(const char *locale);

/*
 *  This takes a mongo collation document and convert to postgre locate string
 *  e.g., en-u-ks-level1-kc-false-kf-upper-kn-false, and use that to perform
 *  comparisons.
 *
 *  See for conversion details: https://www.postgresql.org/docs/current/collation.html
 */
void
ParseAndGetCollationString(const bson_value_t *collationValue, const char *colationString)
{
	bson_iter_t docIter;
	BsonValueInitIterator(collationValue, &docIter);

	const char *locale = NULL;      /* required */
	int strength = 3;               /* optional, default = 3 */
	const char *caseFirst = NULL;   /* optional, default = off */
	bool caseLevel = false;         /* optional, default = false */
	bool numericOrdering = false;   /* optional, default = false */
	bool backwards = false;         /* optional, default = false */
	bool normalization = false;     /* optional, default = false */
	const char *alternate = NULL;   /* optional, default = non-ignorable */
	const char *maxVariable = NULL; /* optional, default not specified. ICU default punct. */

	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		const bson_value_t value = *bson_iter_value(&docIter);

		if (strcmp(key, "locale") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_UTF8, value.value_type, "locale");
			locale = value.value.v_utf8.str;

			if (strcmp(locale, "simple") == 0)
			{
				/* Mongo uses 'simple' locale to specify simple binary comparison. It's a no-op */
				/* since postgres ICU will pick default. */
				continue;
			}

			CheckIfValidLocale(locale);
		}
		else if (strcmp(key, "strength") == 0)
		{
			if (value.value_type == BSON_TYPE_DOUBLE)
			{
				/* If the value is docuble Mongo casts it to int. Strength 2.9 is treated as 2 and so on. */
				strength = (int) value.value.v_double;
			}
			else
			{
				CheckCollationInputParamType(BSON_TYPE_INT32, value.value_type,
											 "strength");
				strength = value.value.v_int32;
			}

			if (strength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
									"unable to parse collation :: caused by :: Enumeration value '0' for field 'collation.strength' is not a valid value")));
			}
			else if (strength < 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION51024), errmsg(
									"unable to parse collation :: caused by :: BSON field 'strength' value must be >= 0, actual value '%d'",
									strength),
								errdetail_log(
									"unable to parse collation :: caused by :: BSON field 'strength' value must be >= 0, actual value '%d'",
									strength)));
			}
			else if (strength > 5)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION51024), errmsg(
									"unable to parse collation :: caused by :: BSON field 'strength' value must be <= 5, actual value '%d'",
									strength),
								errdetail_log(
									"unable to parse collation :: caused by :: BSON field 'strength' value must be <= 5, actual value '%d'",
									strength)));
			}
		}
		else if (strcmp(key, "caseLevel") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_BOOL, value.value_type, "caseLevel");
			caseLevel = value.value.v_bool;
		}
		else if (strcmp(key, "caseFirst") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_UTF8, value.value_type, "caseFirst");
			caseFirst = value.value.v_utf8.str;

			if (strcmp(caseFirst, "off") == 0)
			{
				caseFirst = NULL;

				/* No op, as default for ICU is false. We could have also added "-kf-false" */
			}
			else if (strcmp(caseFirst, "upper") != 0 && strcmp(caseFirst, "lower") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
									"unable to parse collation :: caused by :: Enumeration value '%s' for field 'collation.caseFirst' is not a valid value.",
									caseFirst)));
			}
		}
		else if (strcmp(key, "numericOrdering") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_BOOL, value.value_type,
										 "numericOrdering");
			numericOrdering = value.value.v_bool;
		}
		else if (strcmp(key, "backwards") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_BOOL, value.value_type,
										 "backwards");
			backwards = value.value.v_bool;
		}
		else if (strcmp(key, "normalization") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_BOOL, value.value_type,
										 "normalization");
			normalization = value.value.v_bool;
		}
		else if (strcmp(key, "alternate") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_UTF8, value.value_type, "alternate");
			alternate = value.value.v_utf8.str;

			if (strcmp(alternate, "non-ignorable") == 0)
			{
				/* No op, as default for ICU is false. We could have also added "-ka-noignore" */
				alternate = NULL;
			}
			else if (strcmp(alternate, "shifted") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
									"unable to parse collation :: caused by :: Enumeration value '%s' for field 'collation.alternate' is not a valid value.",
									alternate)));
			}
		}
		else if (strcmp(key, "maxVariable") == 0)
		{
			CheckCollationInputParamType(BSON_TYPE_UTF8, value.value_type, "maxVariable");
			maxVariable = value.value.v_utf8.str;

			if (strcmp(maxVariable, "punct") == 0)
			{
				/* No op, as default for ICU is false. We could have also added "-kv-punct" */
				maxVariable = NULL;
			}
			else if (strcmp(maxVariable, "space") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
									"unable to parse collation :: caused by :: Enumeration value '%s' for field 'collation.maxVariable' is not a valid value.",
									maxVariable)));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_UNKNOWNBSONFIELD), errmsg(
								"unable to parse collation :: caused by :: BSON field 'collation.%s' is an unknown field.",
								key)));
		}
	}

	/* ICU ignores unsupported locales and picks default, and so do we. This string is not used in SQL query so safe from SQL injection */
	StringInfoData icuCollation = { 0 };
	icuCollation.data = (char *) colationString;
	icuCollation.maxlen = MAX_ICU_COLLATION_LENGTH;

	appendStringInfo(&icuCollation, "%s-u-", (locale == NULL) ? "und" : locale);

	if (strength < 5)
	{
		appendStringInfo(&icuCollation, "ks-level%d", strength);
	}
	else
	{
		appendStringInfo(&icuCollation, "ks-identic");
	}

	if (caseFirst != NULL)
	{
		appendStringInfo(&icuCollation, "-kf-%s", caseFirst);
	}

	if (caseLevel)
	{
		appendStringInfo(&icuCollation, "-kc-true");
	}

	if (numericOrdering)
	{
		appendStringInfo(&icuCollation, "-kn-true");
	}

	if (backwards)
	{
		appendStringInfo(&icuCollation, "-kb-true");
	}

	if (normalization)
	{
		appendStringInfo(&icuCollation, "-kk-true");
	}

	if (alternate != NULL)
	{
		appendStringInfo(&icuCollation, "-ka-%s", alternate);
	}

	if (maxVariable != NULL && strcmp(alternate, "shifted") != 0)
	{
		appendStringInfo(&icuCollation, "-kv-%s", maxVariable);
	}
}


/*
 *  Compares two strings using an ICU collation string. The code
 *  follows same logic as how postgres performs ICU based string
 *  comparison given an ICU standard collation string (e.g., en-u-kf-upper-kr-grek)
 *
 *  Reference: https://unicode-org.github.io/icu-docs/apidoc/dev/icu4c/ucol_8h.html
 */
int
StringCompareWithCollation(const char *left, uint32_t leftLength,
						   const char *right, uint32_t rightLength, const
						   char *collationStr)
{
	ucollator_cache_entry *collation_entry = LookupUCollatorCache(collationStr);

	UErrorCode status = U_ZERO_ERROR;

	/* Reference: varstr_cmp() in varlena.c */
	int result = ucol_strcollUTF8(collation_entry->collator,
								  left, leftLength,
								  right, rightLength, &status);

	if (U_FAILURE(status))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"Collation aware string comparison failed for collation language tag: %s",
							collationStr),
						errdetail_log(
							"Collation aware string comparison failed for collation language tag: %s",
							collationStr)));
	}

	return result;
}


/*
 *  Convenience function to generate a collation aware sortkey that can be used in strcmp().
 *
 *  Two calls to ucol_getSortKey() is a pattern used in pg code. This is to know the expected size
 *  of the sort key so that we allocate a larger buffer if needed.
 *  Reference: https://unicode-org.github.io/icu-docs/apidoc/dev/icu4c/ucol_8h.html#a58be2c76d01184cb1821ff0af28081c2
 *  Reference: https://unicode-org.github.io/icu/userguide/collation/api.html
 */
inline char *
GetCollationSortKey(const char *collationString, char *key, int keyLength)
{
	ucollator_cache_entry *collation_entry = LookupUCollatorCache(collationString);

	uint8_t *sortKeyPtr = palloc(DEFAULT_ICU_COLLATION_SORT_KEY_LENGTH);
	UChar *uchar;
	int32_t ulen;

	ulen = icu_to_uchar(&uchar, key, keyLength);
	Size expectedLength = ucol_getSortKey(collation_entry->collator, uchar, ulen,
										  sortKeyPtr,
										  DEFAULT_ICU_COLLATION_SORT_KEY_LENGTH);
	if (expectedLength > DEFAULT_ICU_COLLATION_SORT_KEY_LENGTH)
	{
		sortKeyPtr = repalloc(sortKeyPtr, expectedLength);
		ucol_getSortKey(collation_entry->collator, uchar, ulen, sortKeyPtr,
						expectedLength);
	}

	pfree(uchar);
	return (char *) sortKeyPtr;
}


/*
 *  Checks is a locale is supported, otherwise, throws error.
 */
inline static bool
CheckIfValidLocale(const char *locale)
{
	int localeLength = strlen(locale);
	if (strlen(locale) < 2)
	{
		ThrowInvalidLocaleError(locale);
	}

	/* All locales start with two lower case letters. Locale names are case sensitive. */
	int x = locale[0] - 'a';
	int y = locale[1] - 'a';

	if (x < 0 || y < 0 || x > 25 || y > 25)
	{
		ThrowInvalidLocaleError(locale);
	}

	char code = supported_locale_codes[x][y];

	if (localeLength == 2 && code != ' ')
	{
		/* Happy path, since most collation are two letter codes */
		return true;
	}

	const char *localeSuffix = locale + 2;

	switch (code)
	{
		case 'E':
		{
			if (strcmp(localeSuffix, "_US") == 0 ||
				strcmp(localeSuffix, "_US_POSIX") == 0)
			{
				return true;
			}
			break;
		}

		case 'A':
		{
			if (strcmp(localeSuffix, "_CA") == 0)
			{
				return true;
			}
			break;
		}

		case 'B':
		{
			if (strcmp(localeSuffix, "b") == 0)
			{
				return true;
			}
			break;
		}

		case 'C':
		{
			if (strcmp(localeSuffix, "@collation=compat") == 0)
			{
				return true;
			}
			break;
		}

		case 'D':
		{
			if (strcmp(localeSuffix, "@collation=compat") == 0 ||
				strcmp(localeSuffix, "@collation=phonebook") == 0 ||
				strcmp(localeSuffix, "@collation=eor") == 0 ||
				strcmp(localeSuffix, "_AT") == 0 ||
				strcmp(localeSuffix, "_AT@collation=phonebook") == 0)
			{
				return true;
			}
			break;
		}

		case 'F':
		{
			if (strcmp(localeSuffix, "_AF") == 0)
			{
				return true;
			}
			break;
		}

		case 'G':
		{
			if (strcmp(localeSuffix, "_Cyrl") == 0 ||
				strcmp(localeSuffix, "@collation=search") == 0)
			{
				return true;
			}
			break;
		}

		case 'H':
		{
			if (strcmp(localeSuffix, "w") == 0)
			{
				return true;
			}
			break;
		}

		case 'I':
		{
			if (strcmp(localeSuffix, "l") == 0 ||
				strcmp(localeSuffix, "@collation=search") == 0 ||
				strcmp(localeSuffix, "@collation=traditional") == 0)
			{
				return true;
			}
			break;
		}

		case 'J':
		{
			if (strcmp(localeSuffix, "@collation=unihan") == 0)
			{
				return true;
			}
			break;
		}

		case 'K':
		{
			if (strcmp(localeSuffix, "k") == 0 ||
				strcmp(localeSuffix, "@collation=search") == 0 ||
				strcmp(localeSuffix, "@collation=searchjl") == 0 ||
				strcmp(localeSuffix, "ko@collation=unihan") == 0)
			{
				return true;
			}
			break;
		}

		case 'L':
		{
			if (strcmp(localeSuffix, "_Latin,") == 0 ||
				strcmp(localeSuffix, "_Latn@collation=search") == 0)
			{
				return true;
			}
			break;
		}

		case 'M':
		{
			if (strcmp(localeSuffix, "t") == 0)
			{
				return true;
			}
			break;
		}

		case 'N':
		{
			if (strcmp(localeSuffix, "n") == 0 ||
				strcmp(localeSuffix, "n@collation=search") == 0)
			{
				return true;
			}
			break;
		}

		case 'O':
		{
			if (strcmp(localeSuffix, "b") == 0)
			{
				return true;
			}
			break;
		}

		case 'P':
		{
			if (strcmp(localeSuffix, "@collation=phonetic") == 0)
			{
				return true;
			}
			break;
		}

		case 'Q':
		{
			if (strcmp(localeSuffix, "@collation=search") == 0 ||
				strcmp(localeSuffix, "@collation=traditional") == 0)
			{
				return true;
			}
			break;
		}

		case 'R':
		{
			if (strcmp(localeSuffix, "r") == 0)
			{
				return true;
			}
			break;
		}

		case 'S':
		{
			if (strcmp(localeSuffix, "@collation=search") == 0)
			{
				return true;
			}
			break;
		}

		case 'T':
		{
			if (strcmp(localeSuffix, "@collation=traditional") == 0)
			{
				return true;
			}
			break;
		}

		case 'W':
		{
			if (strcmp(localeSuffix, "e") == 0)
			{
				return true;
			}
			break;
		}

		case 'Y':
		{
			if (strcmp(localeSuffix, "@collation=dictionary") == 0)
			{
				return true;
			}
			break;
		}

		case 'Z':
		{
			if (strcmp(localeSuffix, "_Hant") == 0 ||
				strcmp(localeSuffix, "@collation=big5han") == 0 ||
				strcmp(localeSuffix, "@collation=gb2312han") == 0 ||
				strcmp(localeSuffix, "@collation=unihan") == 0 ||
				strcmp(localeSuffix, "ko@collation=zhuyin") == 0)
			{
				return true;
			}
			break;
		}

		default:
		{
			break;
		}
	}

	ThrowInvalidLocaleError(locale);
	return false;
}


/*
 *  Throws error for unsupported locales.
 */
inline static void
pg_attribute_noreturn()
ThrowInvalidLocaleError(const char * locale)
{
	ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
						"unable to parse collation :: caused by :: Field 'locale' is invalid in: { locale: \"%s\", strength: 1 }.",
						locale)));
}

/*
 *  Checks the input type of the parameters of the collation spec document against the expected types.
 */
inline static void
CheckCollationInputParamType(bson_type_t expectedType, bson_type_t foundType, const
							 char *paramName)
{
	if (expectedType == foundType)
	{
		return;
	}

	ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH), errmsg(
						"unable to parse collation :: caused by :: BSON field 'collation.%s' is the wrong type '%s', expected type '%s'",
						paramName, BsonTypeName(foundType), BsonTypeName(expectedType)),
					errdetail_log(
						"unable to parse collation :: caused by :: BSON field 'collation.%s' is the wrong type '%s', expected type '%s'",
						paramName, BsonTypeName(foundType), BsonTypeName(expectedType))));
}


/*
 * Well known hash function to efficiently calculate hash of a string. While it may have collections it's unlikely in our case
 * where is hash function is used to generate hash code for limited number of collation strings. Even if there is collision,
 * the functionality will not be broken, we will just generate a few more cache entries
 */
static unsigned long
djb2(const char *str)
{
	unsigned long hash = 5381;
	int c;
	while ((c = *str++))
	{
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
	return hash;
}


/*
 * Cache that live the lifetime of a backend process and caches a Ucollator object for performing
 * collation related operations. Open a collator object can be expensive and hence we create this cache.
 * When the backend process dies all memory associated with the collator cache is cleaned up.
 *
 * This is inspired by lookup_collation_cache() in pg_locale.c
 */
static ucollator_cache_entry *
LookupUCollatorCache(const char *collationString)
{
	ucollator_cache_entry *cache_entry;
	bool found;

	if (collation_cache == NULL)
	{
		/* First time through, initialize the hash table */
		HASHCTL ctl;
		memset(&ctl, 0, sizeof(ctl));

		ctl.keysize = sizeof(char *);
		ctl.entrysize = sizeof(ucollator_cache_entry);

		MemoryContext tempContext = AllocSetContextCreate(CurrentMemoryContext,
														  "Collation Context",
														  ALLOCSET_DEFAULT_SIZES);

		MemoryContext oldContext = MemoryContextSwitchTo(tempContext);
		collation_cache = hash_create("Collator cache", 100, &ctl,
									  HASH_ELEM | HASH_BLOBS);
		MemoryContextSwitchTo(oldContext);
	}

	unsigned long collationKey = djb2(collationString);

	cache_entry = hash_search(collation_cache, &collationKey, HASH_ENTER, &found);
	if (!found)
	{
		cache_entry->collationKey = collationKey;
		UErrorCode status = U_ZERO_ERROR;
		UCollator *collator = ucol_open(collationString, &status);

		if (U_FAILURE(status))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Collation is not supported by ICU for collation language tag: %s",
								collationString),
							errdetail_log(
								"Collation is not supported by ICU for collation language tag: %s",
								collationString)));
		}

		cache_entry->collator = collator;
	}

	return cache_entry;
}
