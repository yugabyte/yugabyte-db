/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_text_ops.c
 *
 * Operator implementations for text operations of BSON.
 * Primarily intended for text search and Regex
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <catalog/pg_collation.h>
#include <executor/executor.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <parser/parse_coerce.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>
#include <tsearch/ts_type.h>
#include <tsearch/ts_utils.h>
#include <tsearch/ts_cache.h>
#include <regex/regex.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "utils/mongo_errors.h"
#include "types/pcre_regex.h"
#include "query/bson_dollar_operators.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(command_bson_to_tsvector);
PG_FUNCTION_INFO_V1(command_bson_to_tsvector_with_regconfig);


Datum
command_bson_to_tsvector(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errmsg("Deprecated function")));
}


Datum
command_bson_to_tsvector_with_regconfig(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errmsg("Deprecated function")));
}


/*
 * Compares the value in a given document iterator against a regex.
 * returns false if it's not a text value.
 * if it is a text value then runs the regex execute against the string value.
 * Note: compile_and_execute caches the regex so if the same regex is provided multiple
 * times, it'll used the cached value.
 */
bool
CompareRegexTextMatch(const bson_value_t *docBsonVal, RegexData *regexData)
{
	if (docBsonVal->value_type == BSON_TYPE_REGEX)
	{
		/* if the document is of type regular expression object, then
		 * do a direct match of regex pattern and options against
		 * the regular expression object in the document */
		if ((strcmp(docBsonVal->value.v_regex.regex, regexData->regex) == 0) &&
			(strcmp(docBsonVal->value.v_regex.options,
					regexData->options) == 0))
		{
			return true;
		}

		return false;
	}

	if (docBsonVal->value_type != BSON_TYPE_UTF8)
	{
		return false;
	}

	const StringView strView =
	{
		.string = docBsonVal->value.v_utf8.str,
		.length = docBsonVal->value.v_utf8.len
	};

	return PcreRegexExecute(regexData->regex, regexData->options,
							regexData->pcreData, &strView);
}
