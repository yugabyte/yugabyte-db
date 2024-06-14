/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/bson_gin_entrypoint
 * .c
 *
 * Gin operator implementations of BSON.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <access/reloptions.h>
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

#include "opclass/helio_gin_common.h"
#include "opclass/helio_bson_gin_private.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "opclass/helio_gin_index_term.h"
#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"
#include <math.h>


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(gin_bson_single_path_extract_value);
PG_FUNCTION_INFO_V1(gin_bson_wildcard_project_extract_value);
PG_FUNCTION_INFO_V1(gin_bson_extract_query);
PG_FUNCTION_INFO_V1(gin_bson_compare_partial);
PG_FUNCTION_INFO_V1(gin_bson_pre_consistent);
PG_FUNCTION_INFO_V1(gin_bson_can_pre_consistent);
PG_FUNCTION_INFO_V1(gin_bson_consistent);
PG_FUNCTION_INFO_V1(gin_bson_single_path_options);
PG_FUNCTION_INFO_V1(gin_bson_wildcard_project_options);
PG_FUNCTION_INFO_V1(gin_bson_get_single_path_generated_terms);
PG_FUNCTION_INFO_V1(gin_bson_get_wildcard_project_generated_terms);


static IndexTraverseOption GetHashIndexTraverseOption(void *contextOptions,
													  const char *currentPath,
													  uint32_t currentPathLength);
static void ValidateWildcardProjectPathSpec(const char *prefix);
static Size FillWildcardProjectPathSpec(const char *prefix, void *buffer);


extern Datum gin_bson_exclusion_pre_consistent(PG_FUNCTION_ARGS);
extern uint32_t MaxWildcardIndexKeySize;

/*
 * gin_bson_single_path_extract_value is run on the insert/update path and collects the terms
 * that will be indexed for indexes for a single path definition. the method provides the bson document as an input, and
 * can return as many terms as is necessary (1:N).
 * For more details see documentation on the 'extractValue' method in the GIN extensibility.
 */
Datum
gin_bson_single_path_extract_value(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON_PACKED(0);
	int32_t *nentries = (int32_t *) PG_GETARG_POINTER(1);

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}
	BsonGinSinglePathOptions *options =
		(BsonGinSinglePathOptions *) PG_GET_OPCLASS_OPTIONS();

	GenerateTermsContext context = { 0 };

	GenerateSinglePathTermsCore(bson, &context, options);

	*nentries = context.totalTermCount;

	PG_FREE_IF_COPY(bson, 0);
	PG_RETURN_POINTER(context.terms.entries);
}


/*
 * gin_bson_wildcard_project_extract_value is run on the insert/update path and collects the terms
 * that will be indexed for indexes for a wildcard projection path definition. the method provides the bson document as an input, and
 * can return as many terms as is necessary (1:N).
 * For more details see documentation on the 'extractValue' method in the GIN extensibility.
 * For any given document and path,  the method consults the specified wildcard projections and ensures that the necessary paths
 * are included/excluded from the final terms being generated.
 */
Datum
gin_bson_wildcard_project_extract_value(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON_PACKED(0);
	int32_t *nentries = (int32_t *) PG_GETARG_POINTER(1);
	GenerateTermsContext context = { 0 };
	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	BsonGinWildcardProjectionPathOptions *options =
		(BsonGinWildcardProjectionPathOptions *) PG_GET_OPCLASS_OPTIONS();

	GenerateWildcardPathTermsCore(bson, &context, options);
	*nentries = context.totalTermCount;

	PG_FREE_IF_COPY(bson, 0);
	PG_RETURN_POINTER(context.terms.entries);
}


/*
 * gin_bson_extract_query is run on the query path when a predicate could be pushed
 * to the index. The predicate and the "strategy" based on the operator is passed down.
 * In the operator class, the OPERATOR index maps to the strategy index presented here.
 * The method then returns a set of terms that are valid for that predicate and strategy.
 * For more details see documentation on the 'extractQuery' method in the GIN extensibility.
 * TODO: Today this recurses through the given document fully. We would need to implement
 * something that recurses down 1 level of objects & arrays for a given path unless it's a wildcard
 * index.
 */
Datum
gin_bson_extract_query(PG_FUNCTION_ARGS)
{
	StrategyNumber strategy = PG_GETARG_UINT16(2);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	/* Special case operators: These are not shared across the index
	 * and elemmatch evaluation. Consequently these are handled at the top level
	 * entrypoint
	 */
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_UNIQUE_EQUAL:
		{
			/* unique index equality operator is special and is only expected on the insert path. */
			PG_RETURN_POINTER(GinBsonExtractQueryUniqueIndexTerms(fcinfo));
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ORDERBY:
		{
			PG_RETURN_POINTER(GinBsonExtractQueryOrderBy(fcinfo));
		}

		default:
		{
			break;
		}
	}

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	Datum query = PG_GETARG_DATUM(0);
	bytea *options = (bytea *) PG_GET_OPCLASS_OPTIONS();
	if (!ValidateIndexForQualifierValue(options, query, strategy))
	{
		*nentries = 0;

		/* Note: we don't use PG_RETURN_NULL here since fmgr complains
		 * even theough the contract here is that when nentries is 0, we return null
		 * See for instance gin_extract_jsonb_query in the Postgres codebase */
		PG_RETURN_POINTER(NULL);
	}

	BsonExtractQueryArgs args =
	{
		.query = PG_GETARG_PGBSON(0),
		.nentries = (int32 *) PG_GETARG_POINTER(1),
		.partialmatch = (bool **) PG_GETARG_POINTER(3),
		.extra_data = (Pointer **) PG_GETARG_POINTER(4),
		.options = options,
		.termMetadata = GetIndexTermMetadata(options)
	};

	Datum *entries = GinBsonExtractQueryCore(strategy, &args);

	PG_RETURN_POINTER(entries);
}


/*
 * gin_bson_compare_partial is run on the query path when extract_query requests a partial
 * match on the index. Each index term that has a partial match (with the lower bound as a
 * starting point) will be an input to this method. compare_partial will return '0' if the term
 * is a match, '-1' if the term is not a match but enumeration should continue, and '1' if
 * enumeration should stop. Note that enumeration may happen multiple times - this sorted enumeration
 * happens once per GIN page so there may be several sequences of [-1, 0]* -> 1 per query.
 * The strategy passed in will map to the index of the Operator on the OPERATOR class definition
 * For more details see documentation on the 'comparePartial' method in the GIN extensibility.
 */
Datum
gin_bson_compare_partial(PG_FUNCTION_ARGS)
{
	/* 0 will be the value we passed in for the extract query */
	bytea *queryValue = PG_GETARG_BYTEA_PP(0);

	/* 1 is the value in the index we want to compare against. */
	bytea *compareValue = PG_GETARG_BYTEA_PP(1);
	StrategyNumber strategy = PG_GETARG_UINT16(2);
	Pointer extraData = PG_GETARG_POINTER(3);

	BsonIndexTerm queryIndexTerm = {
		false, false, { 0 }
	};
	BsonIndexTerm compareIndexTerm = {
		false, false, { 0 }
	};
	InitializeBsonIndexTerm(queryValue, &queryIndexTerm);
	InitializeBsonIndexTerm(compareValue, &compareIndexTerm);

	/* Special case operators: These are not shared across the index
	 * and elemmatch evaluation. Consequently these are handled at the top level
	 * entrypoint
	 */
	if (strategy == BSON_INDEX_STRATEGY_DOLLAR_ORDERBY)
	{
		PG_RETURN_INT32(GinBsonComparePartialOrderBy(&queryIndexTerm, &compareIndexTerm));
	}

	int res = GinBsonComparePartialCore(strategy, &queryIndexTerm, &compareIndexTerm,
										extraData);

	PG_FREE_IF_COPY(compareValue, 1);
	PG_RETURN_INT32(res);
}


/*
 * gin_bson_can_pre_consistent validates whether a FastScan can
 * be applied given the operators and strategies. Currently, only
 * equality is a supported strategy for fast-scan.
 * This currently means a $eq or $in with a single key.
 */
Datum
gin_bson_can_pre_consistent(PG_FUNCTION_ARGS)
{
	StrategyNumber strategy = PG_GETARG_UINT16(0);
	uint32 numKeys = PG_GETARG_UINT32(2);
	bool res;
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		{
			res = numKeys == 1;
			break;
		}

		case BSON_INDEX_STRATEGY_UNIQUE_EQUAL:
		{
			res = true;
			break;
		}

		default:
		{
			res = false;
			break;
		}
	}

	PG_RETURN_BOOL(res);
}


/*
 * PreConsistent is applied for RUM fast-scan paths.
 * In the scenario where you have A && B where A is frequent and B is rare,
 * the scan currently has to scan all of A (lots of postings), all of B (few postings)
 * and then do the intersection.
 * RUM added a pre-consistent function that allows you to filter out most irrelevant pages
 * for both A & B *iff* it's not a comparePartial scan. In those cases, it seeks to page-boundaries
 * and calls pre-consistent which can return false positives, but allows the index to be selective
 * about which pages it loads. In this case, we support this for all the conjunction equality filters which
 * do not need comparePartial support.
 */
Datum
gin_bson_pre_consistent(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);
	StrategyNumber strategy = PG_GETARG_UINT16(1);

	/* pgbson *query = PG_GETARG_PGBSON(2); */
	uint32_t numKeys = PG_GETARG_UINT32(3);
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);
	bool *recheck = (bool *) PG_GETARG_POINTER(5);       /* out param. */
	Datum *queryKeys = (Datum *) PG_GETARG_POINTER(6);

	bytea *options = (bytea *) PG_GET_OPCLASS_OPTIONS();
	bool res = GinBsonConsistentCore(strategy,
									 check,
									 extra_data,
									 numKeys,
									 recheck,
									 queryKeys,
									 options);

	PG_RETURN_BOOL(res);
}


/*
 * gin_bson_consistent validates whether a given match on a key
 * can be used to satisfy a query. given an array of queryKeys and
 * an array of 'check' that indicates whether that queryKey matched
 * exactly for the check. it allows for the gin index to do a full
 * runtime check for partial matches (recheck) or to accept that the term was a
 * hit for the query.
 * For more details see documentation on the 'consistent' method in the GIN extensibility.
 */
Datum
gin_bson_consistent(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);
	StrategyNumber strategy = PG_GETARG_UINT16(1);
	int32_t numKeys = (int32_t) PG_GETARG_INT32(3);
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);
	bool *recheck = (bool *) PG_GETARG_POINTER(5);       /* out param. */
	Datum *queryKeys = (Datum *) PG_GETARG_POINTER(6);
	bool res;

	/* Special case operators: These are not shared across the index
	 * and elemmatch evaluation. Consequently these are handled at the top level
	 * entrypoint
	 */
	if (strategy == BSON_INDEX_STRATEGY_DOLLAR_ORDERBY)
	{
		*recheck = false;
		PG_RETURN_BOOL(check[0] || check[1] || check[2]);
	}

	bytea *options = (bytea *) PG_GET_OPCLASS_OPTIONS();
	res = GinBsonConsistentCore(strategy,
								check,
								extra_data,
								numKeys,
								recheck,
								queryKeys,
								options);
	PG_RETURN_BOOL(res);
}


/*
 * gin_bson_get_single_path_generated_terms is an internal utility function that allows to retrieve
 * the set of terms that *would* be inserted in the index for a given document for a single
 * path index option specification.
 * The function gets a document, path, and if it's a wildcard, and sets up the index structures
 * to call 'generateTerms' and returns it as a SETOF records.
 */
Datum
gin_bson_get_single_path_generated_terms(PG_FUNCTION_ARGS)
{
	FuncCallContext *functionContext;
	GenerateTermsContext *context;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		pgbson *document = PG_GETARG_PGBSON(0);

		functionContext = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(functionContext->multi_call_memory_ctx);

		context = (GenerateTermsContext *) palloc0(sizeof(GenerateTermsContext));

		const char *prefixStr = text_to_cstring(PG_GETARG_TEXT_P(1));
		Size fieldSize = FillSinglePathSpec(prefixStr, NULL);
		BsonGinSinglePathOptions *options = (BsonGinSinglePathOptions *) palloc0(
			fieldSize + sizeof(BsonGinSinglePathOptions));
		FillSinglePathSpec(prefixStr, ((char *) options) +
						   sizeof(BsonGinSinglePathOptions));

		options->path = sizeof(BsonGinSinglePathOptions);
		options->isWildcard = PG_GETARG_BOOL(2);
		options->generateNotFoundTerm = PG_GETARG_BOOL(3);

		int32_t truncateLimit = PG_GETARG_INT32(5);
		options->base.indexTermTruncateLimit = truncateLimit;
		options->base.wildcardIndexTruncatedPathLimit = truncateLimit > 0 ?
														MaxWildcardIndexKeySize : 0;

		GenerateSinglePathTermsCore(document, context, options);
		context->index = 0;
		MemoryContextSwitchTo(oldcontext);
		functionContext->user_fctx = (void *) context;
	}

	functionContext = SRF_PERCALL_SETUP();
	context = (GenerateTermsContext *) functionContext->user_fctx;

	if (context->index < context->totalTermCount)
	{
		Datum next = context->terms.entries[context->index++];
		BsonIndexTerm term = {
			false, false, { 0 }
		};
		bytea *serializedTerm = DatumGetByteaPP(next);
		InitializeBsonIndexTerm(serializedTerm, &term);
		bool addMetadata = PG_GETARG_BOOL(4);

		/* By default we only print out the index term. If addMetadata is set, then we
		 * also append the bson metadata for the index term to the final output.
		 * This includes things like whether or not the term is truncated
		 */
		if (!addMetadata)
		{
			SRF_RETURN_NEXT(functionContext, PointerGetDatum(PgbsonElementToPgbson(
																 &term.element)));
		}
		else
		{
			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendValue(&writer, term.element.path, term.element.pathLength,
									&term.element.bsonValue);
			PgbsonWriterAppendBool(&writer, "t", 1, term.isIndexTermTruncated);
			SRF_RETURN_NEXT(functionContext, PointerGetDatum(PgbsonWriterGetPgbson(
																 &writer)));
		}
	}

	SRF_RETURN_DONE(functionContext);
}


/*
 * gin_bson_get_wildcard_project_generated_terms is an internal utility function that allows to retrieve
 * the set of terms that *would* be inserted in the index for a given document for a wildcard projection
 * option specification.
 * The function gets a document, a set of paths, and if it's an exclusion, and sets up the index structures
 * to call 'generateTerms' and returns it as a SETOF records.
 */
Datum
gin_bson_get_wildcard_project_generated_terms(PG_FUNCTION_ARGS)
{
	FuncCallContext *functionContext;
	GenerateTermsContext *context;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		pgbson *document = PG_GETARG_PGBSON(0);

		functionContext = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(functionContext->multi_call_memory_ctx);

		context = (GenerateTermsContext *) palloc0(sizeof(GenerateTermsContext));

		const char *prefixStr = text_to_cstring(PG_GETARG_TEXT_P(1));
		Size fieldSize = FillWildcardProjectPathSpec(prefixStr, NULL);
		BsonGinWildcardProjectionPathOptions *options =
			(BsonGinWildcardProjectionPathOptions *) palloc0(fieldSize +
															 sizeof(
																 BsonGinWildcardProjectionPathOptions));
		FillWildcardProjectPathSpec(prefixStr, ((char *) options) +
									sizeof(BsonGinWildcardProjectionPathOptions));
		options->pathSpec = sizeof(BsonGinWildcardProjectionPathOptions);
		options->isExclusion = PG_GETARG_BOOL(2);
		options->includeId = PG_GETARG_BOOL(3);
		options->base.type = IndexOptionsType_Wildcard;

		int32_t truncateLimit = PG_GETARG_INT32(5);
		options->base.indexTermTruncateLimit = truncateLimit;
		options->base.wildcardIndexTruncatedPathLimit = truncateLimit > 0 ?
														MaxWildcardIndexKeySize : 0;

		GenerateWildcardPathTermsCore(document, context, options);

		context->index = 0;
		MemoryContextSwitchTo(oldcontext);
		functionContext->user_fctx = (void *) context;
	}

	functionContext = SRF_PERCALL_SETUP();
	context = (GenerateTermsContext *) functionContext->user_fctx;

	if (context->index < context->totalTermCount)
	{
		Datum next = context->terms.entries[context->index++];
		BsonIndexTerm term = {
			false, false, { 0 }
		};
		bytea *serializedTerm = DatumGetByteaPP(next);
		InitializeBsonIndexTerm(serializedTerm, &term);
		bool addMetadata = PG_GETARG_BOOL(4);

		/* By default we only print out the index term. If addMetadata is set, then we
		 * also append the bson metadata for the index term to the final output.
		 * This includes things like whether or not the term is truncated
		 */
		if (!addMetadata)
		{
			SRF_RETURN_NEXT(functionContext, PointerGetDatum(PgbsonElementToPgbson(
																 &term.element)));
		}
		else
		{
			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendValue(&writer, term.element.path, term.element.pathLength,
									&term.element.bsonValue);
			PgbsonWriterAppendBool(&writer, "t", 1, term.isIndexTermTruncated);
			SRF_RETURN_NEXT(functionContext, PointerGetDatum(PgbsonWriterGetPgbson(
																 &writer)));
		}
	}

	SRF_RETURN_DONE(functionContext);
}


/*
 * gin_bson_single_path_options sets up the option specification for single field indexes
 * This initializes the structure that is used by the Index AM to process user specified
 * options on how to handle documents with the index.
 * For single field indexes we only need to track the path being indexed, and whether or not
 * it's a wildcard.
 * usage is as: using gin(document bson_gin_single_path_ops(path='a.b',iswildcard=true))
 * For more details see documentation on the 'options' method in the GIN extensibility.
 */
Datum
gin_bson_single_path_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(BsonGinSinglePathOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_SinglePath, /* default value */
							IndexOptionsType_SinglePath, /* min */
							IndexOptionsType_SinglePath, /* max */
							offsetof(BsonGinSinglePathOptions, base.type));

	bool isWildcardDefault = false;
	bool generateNotFoundTermDefault = false;
	add_local_bool_reloption(relopts, "iswildcard",
							 "Whether the path is a wildcard", isWildcardDefault,
							 offsetof(BsonGinSinglePathOptions, isWildcard));
	add_local_bool_reloption(relopts, "generatenotfoundterm",
							 "Whether the index generates terms for paths not found (currently used in unique)",
							 generateNotFoundTermDefault,
							 offsetof(BsonGinSinglePathOptions, generateNotFoundTerm));
	add_local_string_reloption(relopts, "path",
							   "Prefix path for the index",
							   NULL, &ValidateSinglePathSpec, &FillSinglePathSpec,
							   offsetof(BsonGinSinglePathOptions, path));
	add_local_string_reloption(relopts, "indexname",
							   "[deprecated] The mongo specific name for the index",
							   NULL, NULL, &FillDeprecatedStringSpec,
							   offsetof(BsonGinSinglePathOptions,
										base.intOption_deprecated));
	add_local_int_reloption(relopts, "indextermsize",
							"[deprecated] The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinSinglePathOptions,
									 base.intOption_deprecated));
	add_local_int_reloption(relopts, "ts",
							"[deprecated] The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinSinglePathOptions,
									 base.intOption_deprecated));
	add_local_int_reloption(relopts, "tl",
							"The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinSinglePathOptions,
									 base.indexTermTruncateLimit));

	add_local_int_reloption(relopts, "wkl",
							"The key size limit for wildcard index truncation.",
							INT32_MAX, /* default value */
							0, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinWildcardProjectionPathOptions,
									 base.wildcardIndexTruncatedPathLimit));

	add_local_int_reloption(relopts, "v",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V1,         /* max */
							offsetof(BsonGinSinglePathOptions, base.version));

	PG_RETURN_VOID();
}


/*
 * gin_bson_wildcard_project_options sets up the option specification for wildcard field indexes
 * This initializes the structure that is used by the Index AM to process user specified
 * options on how to handle documents with the index.
 * For wildcard field indexes we track whether the wildcard is an exclusion or inclusion,
 * whether or not we include the _id, and the set of paths in the inclusion/exclusion
 * the pathSpec is the jsonified content of the wildcardPrefixes bson fragment in the index specification.
 * usage is as: using gin(document bson_gin_wildcard_project_path_ops(isexclusion=false,pathspec='[ "a.b", "c.d"]'))
 * For more details see documentation on the 'options' method in the GIN extensibility.
 */
Datum
gin_bson_wildcard_project_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(BsonGinWildcardProjectionPathOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_Wildcard, /* default value */
							IndexOptionsType_Wildcard, /* min */
							IndexOptionsType_Wildcard, /* max */
							offsetof(BsonGinWildcardProjectionPathOptions, base.type));

	bool isExclusionDefault = false;
	add_local_bool_reloption(relopts, "isexclusion",
							 "Whether the projection specification is an exclusion",
							 isExclusionDefault,
							 offsetof(BsonGinWildcardProjectionPathOptions, isExclusion));
	bool includeIdDefault = true;
	add_local_bool_reloption(relopts, "includeid",
							 "Whether the _id is included in the filter",
							 includeIdDefault,
							 offsetof(BsonGinWildcardProjectionPathOptions, includeId));
	add_local_string_reloption(relopts, "pathspec",
							   "The set of wildcard prefix paths in the form { 'path1' : 1, 'path2' : 1 }",
							   NULL, &ValidateWildcardProjectPathSpec,
							   &FillWildcardProjectPathSpec,
							   offsetof(BsonGinWildcardProjectionPathOptions,
										pathSpec));
	add_local_string_reloption(relopts, "indexname",
							   "[deprecated] The mongo specific name for the index",
							   NULL, NULL, &FillDeprecatedStringSpec,
							   offsetof(BsonGinWildcardProjectionPathOptions,
										base.intOption_deprecated));
	add_local_int_reloption(relopts, "indextermsize",
							"[deprecated] The index term size limit for truncation",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinWildcardProjectionPathOptions,
									 base.intOption_deprecated));
	add_local_int_reloption(relopts, "ts",
							"[deprecated] The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinWildcardProjectionPathOptions,
									 base.intOption_deprecated));
	add_local_int_reloption(relopts, "tl",
							"The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinWildcardProjectionPathOptions,
									 base.indexTermTruncateLimit));
	add_local_int_reloption(relopts, "wkl",
							"The key size limit for wildcard index truncation.",
							INT32_MAX, /* default value */
							0, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinWildcardProjectionPathOptions,
									 base.wildcardIndexTruncatedPathLimit));

	add_local_int_reloption(relopts, "v",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V1,         /* max */
							offsetof(BsonGinWildcardProjectionPathOptions, base.version));

	PG_RETURN_VOID();
}


/*
 * ValidateIndexForQualifierValue checks that a given queryValue can be satisfied
 * by the current index given the indexOptions for that index and an operator strategy.
 */
bool
ValidateIndexForQualifierValue(bytea *indexOptions, Datum queryValue, BsonIndexStrategy
							   strategy)
{
	pgbson *queryBson;
	pgbsonelement filterElement;
	if (indexOptions == NULL)
	{
		ereport(ERROR, errmsg(
					"Unexpected - Must have valid index options to use the index"));
	}

	if (strategy == BSON_INDEX_STRATEGY_UNIQUE_EQUAL)
	{
		/* See ExtractQuery for gin_bson */
		return true;
	}

	queryBson = DatumGetPgBson(queryValue);
	PgbsonToSinglePgbsonElement(queryBson, &filterElement);

	BsonGinIndexOptionsBase *options = (BsonGinIndexOptionsBase *) indexOptions;

	IndexTraverseOption traverse = IndexTraverse_Invalid;
	switch (options->type)
	{
		case IndexOptionsType_Text:
		{
			/* Should not be called for text */
			traverse = IndexTraverse_Invalid;
			break;
		}

		case IndexOptionsType_2d:
		case IndexOptionsType_2dsphere:
		{
			Bson2dGeometryPathOptions *option = (Bson2dGeometryPathOptions *) options;
			uint32_t indexPathLength;
			const char *indexPath;
			Get_Index_Path_Option(option, path, indexPath, indexPathLength);
			if (indexPathLength == filterElement.pathLength &&
				strncmp(indexPath, filterElement.path, indexPathLength) == 0)
			{
				/* this is an exact match on the path. */
				traverse = IndexTraverse_Match;
			}
			else
			{
				traverse = IndexTraverse_Invalid;
			}
			break;
		}

		case IndexOptionsType_SinglePath:
		{
			BsonGinSinglePathOptions *singlePathOptions =
				(BsonGinSinglePathOptions *) indexOptions;

			if (singlePathOptions->isWildcard)
			{
				StringView fieldPathName = CreateStringViewFromString(filterElement.path);

				if (StringViewEndsWith(&fieldPathName, '.'))
				{
					ereport(ERROR, (errcode(MongoDottedFieldName), errmsg(
										"FieldPath must not end with a '.'.")));
				}
			}

			traverse = GetSinglePathIndexTraverseOption(options,
														filterElement.path,
														filterElement.pathLength,
														filterElement.bsonValue.value_type);
			break;
		}

		case IndexOptionsType_Hashed:
		{
			/* Hash index only supports $eq today */
			if (strategy != BSON_INDEX_STRATEGY_DOLLAR_EQUAL &&
				strategy != BSON_INDEX_STRATEGY_DOLLAR_IN)
			{
				return false;
			}

			traverse = GetHashIndexTraverseOption(options,
												  filterElement.path,
												  filterElement.pathLength);
			break;
		}

		case IndexOptionsType_Wildcard:
		{
			traverse = GetWildcardProjectionPathIndexTraverseOption(options,
																	filterElement.path,
																	filterElement.
																	pathLength,
																	filterElement.
																	bsonValue.value_type);
			break;
		}

		case IndexOptionsType_UniqueShardKey:
		{
			traverse = IndexTraverse_Invalid;
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unrecognized index options type %d", options->type)));
			break;
		}
	}

	return traverse == IndexTraverse_Match;
}


/*
 * checks if a path can be pushed to an index given the options for a $in type query.
 */
bool
ValidateIndexForQualifierPathForDollarIn(bytea *indexOptions, const StringView *queryPath)
{
	if (indexOptions == NULL)
	{
		ereport(ERROR, errmsg(
					"Unexpected - Must have valid index options to use the index"));
	}

	BsonGinIndexOptionsBase *options = (BsonGinIndexOptionsBase *) indexOptions;

	IndexTraverseOption traverse = IndexTraverse_Invalid;
	switch (options->type)
	{
		case IndexOptionsType_Text:
		{
			/* Should not be called for text */
			traverse = IndexTraverse_Invalid;
			break;
		}

		case IndexOptionsType_2d:
		case IndexOptionsType_2dsphere:
		{
			/* $in can't be pushed to 2d/2dsphere */
			traverse = IndexTraverse_Invalid;
			break;
		}

		case IndexOptionsType_SinglePath:
		{
			BsonGinSinglePathOptions *singlePathOptions =
				(BsonGinSinglePathOptions *) indexOptions;

			if (singlePathOptions->isWildcard)
			{
				if (StringViewEndsWith(queryPath, '.'))
				{
					ereport(ERROR, (errcode(MongoDottedFieldName), errmsg(
										"FieldPath must not end with a '.'.")));
				}
			}

			traverse = GetSinglePathIndexTraverseOption(options,
														queryPath->string,
														queryPath->length,
														BSON_TYPE_EOD);
			break;
		}

		case IndexOptionsType_Hashed:
		{
			/* Hash index only supports $eq today */
			traverse = GetHashIndexTraverseOption(options,
												  queryPath->string,
												  queryPath->length);
			break;
		}

		case IndexOptionsType_Wildcard:
		{
			traverse = GetWildcardProjectionPathIndexTraverseOption(options,
																	queryPath->string,
																	queryPath->length,
																	BSON_TYPE_EOD);
			break;
		}

		case IndexOptionsType_UniqueShardKey:
		{
			traverse = IndexTraverse_Invalid;
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unrecognized index options type %d", options->type)));
			break;
		}
	}

	return traverse == IndexTraverse_Match;
}


/*
 * Given an opaque index options returns the index
 * term metadata for the index if applicable.
 */
IndexTermCreateMetadata
GetIndexTermMetadata(void *indexOptions)
{
	BsonGinIndexOptionsBase *options = (BsonGinIndexOptionsBase *) indexOptions;

	if (options->version >= IndexOptionsVersion_V1 || options->indexTermTruncateLimit > 0)
	{
		StringView pathPrefix = { 0 };
		bool isWildcard = false;
		bool isWildcardProjection = false;
		if (options->type == IndexOptionsType_SinglePath)
		{
			/* For single path indexes, we can elide the index path prefix */
			BsonGinSinglePathOptions *singlePathOptions =
				(BsonGinSinglePathOptions *) options;

			if (options->version >= IndexOptionsVersion_V1 &&
				singlePathOptions->generateNotFoundTerm)
			{
				ereport(ERROR, (errmsg(
									"Index term version V1 is not supported by unique path indexes"),
								errhint(
									"Index term version V1 is not supported by unique path indexes")));
			}

			Get_Index_Path_Option(singlePathOptions, path, pathPrefix.string,
								  pathPrefix.length);
			isWildcard = singlePathOptions->isWildcard;
		}
		else if (options->type == IndexOptionsType_Wildcard)
		{
			isWildcard = true;
			isWildcardProjection = true;
		}
		else if (options->version >= IndexOptionsVersion_V1)
		{
			ereport(ERROR, (errmsg(
								"Index version V1 is not supported by hashed, text or 2d sphere indexes"),
							errhint(
								"Index version V1 is not supported by hashed, text or 2d sphere indexes")));
		}

		uint32_t wildcardIndexTruncatedPathLimit =
			options->wildcardIndexTruncatedPathLimit == 0 ?
			UINT32_MAX :
			options->
			wildcardIndexTruncatedPathLimit;

		return (IndexTermCreateMetadata) {
				   .indexTermSizeLimit = options->indexTermTruncateLimit,
				   .wildcardIndexTruncatedPathLimit = wildcardIndexTruncatedPathLimit,
				   .pathPrefix = pathPrefix,
				   .isWildcard = isWildcard,
				   .isWildcardProjection = isWildcardProjection,
				   .indexVersion = options->version
		};
	}

	return (IndexTermCreateMetadata) {
			   .indexTermSizeLimit = 0,
			   .wildcardIndexTruncatedPathLimit = UINT32_MAX,
			   .pathPrefix = { 0 },
			   .isWildcard = false,
			   .isWildcardProjection = false,
			   .indexVersion = options->version
	};
}


/*
 * Given a specific index term path in the document (e.g. a.b.c) and a specific
 * index option, determines whether or not to generate terms based on whether it's
 * a wildcard, and/or suffix of the given path.
 * The method assumes that the options provided is a BsonGinWildcardProjectionPathOptions
 */
IndexTraverseOption
GetWildcardProjectionPathIndexTraverseOption(void *contextOptions, const
											 char *currentPath, uint32_t
											 currentPathLength,
											 bson_type_t bsonType)
{
	BsonGinWildcardProjectionPathOptions *option =
		(BsonGinWildcardProjectionPathOptions *) contextOptions;

	uint32_t pathCount;
	const char *pathSpecBytes;
	Get_Index_Path_Option(option, pathSpec, pathSpecBytes, pathCount);
	for (uint32_t i = 0; i < pathCount; i++)
	{
		uint32_t indexPathLength = *(uint32_t *) pathSpecBytes;
		const char *indexPath = pathSpecBytes + sizeof(uint32_t);
		pathSpecBytes += indexPathLength + sizeof(uint32_t);

		/* current path is some dotted path into the document (e.g. a.b.c) */
		/* wildcard path is some prefix of this path (e.g. a.b) */
		/* path is invalid if it's exclusion, valid otherwise. */
		if (indexPathLength <= currentPathLength &&
			strncmp(indexPath, currentPath, indexPathLength) == 0)
		{
			if ((indexPathLength + 1 <= currentPathLength &&
				 currentPath[indexPathLength] == '.') ||
				indexPathLength == currentPathLength)
			{
				return option->isExclusion ? IndexTraverse_Invalid : IndexTraverse_Match;
			}
		}

		/* current path is some dotted path into the document (e.g. a.b.c) */
		/* wildcard path is some suffix of this path (e.g. a.b.c.d), */
		/* continue traversing but don't collect terms. */
		/* this is only if inclusion is true. */
		if (currentPathLength < indexPathLength &&
			strncmp(indexPath, currentPath, currentPathLength) == 0 &&
			!option->isExclusion)
		{
			return IndexTraverse_Recurse;
		}
	}

	/* handle special cases - id inclusion or exclusion. */
	if (currentPathLength == 3 && memcmp(currentPath, "_id", 3) == 0)
	{
		return option->includeId ? IndexTraverse_Match : IndexTraverse_Invalid;
	}

	/* no path matched, if it's exclusion then generate terms; otherwise skip. */
	return option->isExclusion ? IndexTraverse_Match : IndexTraverse_Invalid;
}


/*
 * Given a specific index term path in the document (e.g. a.b.c) and a specific
 * index option, determines whether or not to generate terms based on whether it's
 * a wildcard, and/or suffix of the given path.
 * The method assumes that the options provided is a BsonGinSinglePathOptions
 */
IndexTraverseOption
GetSinglePathIndexTraverseOption(void *contextOptions,
								 const char *currentPath, uint32_t currentPathLength,
								 bson_type_t bsonType)
{
	BsonGinSinglePathOptions *option = (BsonGinSinglePathOptions *) contextOptions;
	uint32_t indexPathLength;
	const char *indexPath;
	Get_Index_Path_Option(option, path, indexPath, indexPathLength);
	return GetSinglePathIndexTraverseOptionCore(indexPath, indexPathLength,
												currentPath, currentPathLength,
												option->isWildcard);
}


IndexTraverseOption
GetSinglePathIndexTraverseOptionCore(const char *indexPath,
									 uint32_t indexPathLength,
									 const char *currentPath,
									 uint32_t currentPathLength,
									 bool isWildcard)
{
	if (indexPathLength == 0 && isWildcard)
	{
		/* wildcard at the root, all paths are valid. */
		return IndexTraverse_Match;
	}

	/* current path is some dotted path into the document (e.g. a.b.c) */
	/* wildcard path is some prefix of this path (e.g. a.b), path is valid if it's a wildcard. */
	if (indexPathLength < currentPathLength &&
		strncmp(indexPath, currentPath, indexPathLength) == 0)
	{
		return isWildcard && currentPath[indexPathLength] == '.' ?
			   IndexTraverse_Match : IndexTraverse_Invalid;
	}

	if (indexPathLength == currentPathLength &&
		strncmp(indexPath, currentPath, indexPathLength) == 0)
	{
		/* this is an exact match on the path. */
		return IndexTraverse_Match;
	}

	/* current path is some dotted path into the document (e.g. a.b.c) */
	/* wildcard path is some suffix of this path (e.g. a.b.c.d), */
	/* continue traversing but don't collect terms. */
	if (currentPathLength < indexPathLength &&
		strncmp(indexPath, currentPath, currentPathLength) == 0)
	{
		return IndexTraverse_Recurse;
	}

	/* otherwise skip the path */
	return IndexTraverse_Invalid;
}


/*
 * Callback that validates a user provided single path prefix
 * This is called on CREATE INDEX when a specific path is provided.
 */
void
ValidateSinglePathSpec(const char *prefix)
{
	if (prefix == NULL)
	{
		return;
	}

	int32_t stringLength = strlen(prefix);
	if (stringLength == 0)
	{
		/* root wildcard index */
		return;
	}
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */


/*
 * Given a specific index term path in the document (e.g. a.b.c) and a specific
 * index option, determines whether or not to generate terms based on whether it's
 * suffix or a exact match of the given path.
 * The method assumes that the options provided is a BsonGinHashOptions
 */
static IndexTraverseOption
GetHashIndexTraverseOption(void *contextOptions, const char *currentPath, uint32_t
						   currentPathLength)
{
	BsonGinHashOptions *option = (BsonGinHashOptions *) contextOptions;
	const char *indexPath;
	uint32_t indexPathLength;
	Get_Index_Path_Option(option, path, indexPath, indexPathLength);

	/* hashed indexes doesn't support wildcard key paths */
	bool isWildcard = false;
	return GetSinglePathIndexTraverseOptionCore(indexPath, indexPathLength, currentPath,
												currentPathLength, isWildcard);
}


/*
 * Callback that updates the single path data into the serialized,
 * post-processed options structure - this is used later in term generation
 * through PG_GET_OPCLASS_OPTIONS().
 * This is called on CREATE INDEX to set up the serialized structure.
 * This function is called twice
 * - once with buffer being NULL (to get alloc size)
 * - once again with the buffer that should be serialized.
 */
Size
FillSinglePathSpec(const char *prefix, void *buffer)
{
	uint32_t length = prefix == NULL ? 0 : strlen(prefix);

	/* trailing 0 */
	uint32_t suffixLength = prefix == NULL ? 0 : 1;

	if (buffer != NULL)
	{
		*((uint32_t *) buffer) = length;
		if (length > 0)
		{
			char *address = (char *) buffer;
			memcpy(address + sizeof(uint32_t), prefix, length);
			char *finalChar = address + sizeof(uint32_t) + length;
			*finalChar = 0;
		}
	}

	/* first 4 bytes are length, then chars, and trailing 0 */
	return sizeof(uint32_t) + length + suffixLength;
}


/*
 * Handles parsing/handling deprecated string fields.
 */
Size
FillDeprecatedStringSpec(const char *value, void *ptr)
{
	/* Do nothing */
	return 0;
}


/*
 * Callback that validates a user provided wildcard projection prefix
 * This is called on CREATE INDEX when a specific wildcard projection is provided.
 * We do minimal sanity validation here and instead use the Fill method to do final validation.
 */
static void
ValidateWildcardProjectPathSpec(const char *prefix)
{
	if (prefix == NULL)
	{
		/* validate can be called with the default value NULL. */
		return;
	}

	int32_t stringLength = strlen(prefix);
	if (stringLength < 3)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"at least one filter path must be specified")));
	}
}


/*
 * Callback that updates the single path data into the serialized,
 * post-processed options structure - this is used later in term generation
 * through PG_GET_OPCLASS_OPTIONS().
 * This is called on CREATE INDEX to set up the serialized structure.
 * This function is called twice
 * - once with buffer being NULL (to get alloc size)
 * - once again with the buffer that should be serialized.
 * Here we parse the jsonified path options to build a serialized path
 * structure that is more efficiently parsed during term generation.
 */
static Size
FillWildcardProjectPathSpec(const char *prefix, void *buffer)
{
	if (prefix == NULL)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"at least one filter path must be specified")));
	}

	pgbson *bson = PgbsonInitFromJson(prefix);
	uint32_t pathCount = 0;
	bson_iter_t bsonIterator;

	/* serialized length - start with the total term count. */
	uint32_t totalSize = sizeof(uint32_t);
	PgbsonInitIterator(bson, &bsonIterator);
	while (bson_iter_next(&bsonIterator))
	{
		if (!BSON_ITER_HOLDS_UTF8(&bsonIterator))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"filter must have a valid string path")));
		}

		uint32_t pathLength;
		bson_iter_utf8(&bsonIterator, &pathLength);
		if (pathLength == 0)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"filter must have a valid path")));
		}

		pathCount++;

		/* add the prefixed path length */
		totalSize += sizeof(uint32_t);

		/* add the path size */
		totalSize += pathLength;
	}

	if (buffer != NULL)
	{
		PgbsonInitIterator(bson, &bsonIterator);
		char *bufferPtr = (char *) buffer;
		*((uint32_t *) bufferPtr) = pathCount;
		bufferPtr += sizeof(uint32_t);

		while (bson_iter_next(&bsonIterator))
		{
			uint32_t pathLength;
			const char *path = bson_iter_utf8(&bsonIterator, &pathLength);

			/* add the prefixed path length */
			*((uint32_t *) bufferPtr) = pathLength;
			bufferPtr += sizeof(uint32_t);

			/* add the serialized string */
			memcpy(bufferPtr, path, pathLength);
			bufferPtr += pathLength;
		}
	}

	return totalSize;
}
