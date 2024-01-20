/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/bson_text_gin.c
 *
 * Gin operator implementations of BSON Text indexing.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>
#include <string.h>
#include <utils/builtins.h>
#include <access/reloptions.h>
#include <tsearch/ts_utils.h>
#include <tsearch/ts_type.h>
#include <tsearch/ts_cache.h>
#include <catalog/namespace.h>
#include <utils/array.h>
#include <nodes/makefuncs.h>

#include "io/helio_bson_core.h"
#include "opclass/helio_gin_common.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "opclass/helio_bson_gin_private.h"
#include "utils/mongo_errors.h"
#include "opclass/helio_bson_text_gin.h"
#include "metadata/metadata_cache.h"
#include "opclass/helio_index_support.h"


extern QueryTextIndexData *QueryTextData;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

typedef struct
{
	/* The mongodb name of the language */
	const char *mongoLanguageName;

	/* The postgres language regconfig for the text search. */
	const char *postgresLanguageName;
} MongoLanguageExpression;

typedef struct TextQueryEvalData
{
	BsonGinTextPathOptions *indexOptions;

	TSQuery queryData;
} TextQueryEvalData;

static QTNode * RewriteQueryTree(QTNode *node, bool *rewrote);

static IndexTraverseOption GetTextIndexTraverseOption(void *contextOptions,
													  const char *currentPath, uint32_t
													  currentPathLength,
													  bson_type_t bsonType);

static Oid ExtractTsConfigFromLanguage(const StringView *stringView,
									   bool isCreateIndex);

static void BsonValidateAndExtractTextQuery(const bson_value_t *queryValue,
											bson_value_t *searchValue,
											Oid *languageOid,
											bson_value_t *caseSensitive,
											bson_value_t *diacriticSensitive);
static Datum BsonTextGenerateTSQueryCore(const bson_value_t *queryValue,
										 bytea *indexOptions, Oid tsConfigOid);
static void ValidateDefaultLanguageSpec(const char *defaultLanguage);
static Size FillDefaultLanguageSpec(const char *defaultLanguage, void *buffer);
static void ValidateWeightsSpec(const char *weightsSpec);
static Size FillWeightsSpec(const char *weightsSpec, void *buffer);


static TSVector GenerateTsVectorWithOptions(pgbson *doc,
											BsonGinTextPathOptions *options);

/*
 * Returns the language settings based on the index options.
 * If not provided, uses the system default TextSearch config.
 */
inline static Oid
GetDefaultLanguage(BsonGinTextPathOptions *options)
{
	const char *languageOption = GET_STRING_RELOPTION(options, defaultLanguage);
	if (languageOption != NULL)
	{
		Oid *languageOid = (Oid *) languageOption;

		if (*languageOid != InvalidOid)
		{
			return *languageOid;
		}
	}

	return getTSCurrentConfig(true);
}


/*
 * Track short ISO code language map to the full language name
 * The full language name will be accepted as is. For the list of
 * supported languages see
 * https://www.mongodb.com/docs/manual/reference/text-search-languages/#std-label-text-search-languages
 */
static MongoLanguageExpression LanguageExpressions[] =
{
	{ "da", "danish" },
	{ "nl", "dutch" },
	{ "en", "english" },
	{ "fi", "finnish" },
	{ "fr", "french" },
	{ "de", "german" },
	{ "hu", "hungarian" },
	{ "it", "italian" },
	{ "nb", "norwegian" },
	{ "pt", "portuguese" },
	{ "ro", "romanian" },
	{ "ru", "russian" },
	{ "es", "spanish" },
	{ "sv", "swedish" },
	{ "tr", "turkish" }
};

static int NumberOfLanguages = sizeof(LanguageExpressions) /
							   sizeof(MongoLanguageExpression);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(rum_bson_single_path_extract_tsvector);
PG_FUNCTION_INFO_V1(command_bson_query_to_tsquery);
PG_FUNCTION_INFO_V1(rum_bson_text_path_options);
PG_FUNCTION_INFO_V1(bson_dollar_text_meta_qual);

/*
 * Implements the extract tsvector function that extracts index terms
 * for a given BSON document. Follows the same pattern as the
 * RUM extract TSVector and the bson gin to extract text values.
 */
Datum
rum_bson_single_path_extract_tsvector(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON(0);
	int32_t *nentries = (int32_t *) PG_GETARG_POINTER(1);

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	BsonGinTextPathOptions *options =
		(BsonGinTextPathOptions *) PG_GET_OPCLASS_OPTIONS();
	TSVector vector = GenerateTsVectorWithOptions(bson, options);

	if (vector == NULL)
	{
		*nentries = 0;
		PG_RETURN_POINTER(NULL);
	}

	/* Call RUM extract tsvector with the given tsvector. */
	Datum result = OidFunctionCall5(RumExtractTsVectorFunctionId(),
									TSVectorGetDatum(vector),
									PG_GETARG_DATUM(1),
									PG_GETARG_DATUM(2),
									PG_GETARG_DATUM(3),
									PG_GETARG_DATUM(4));
	PG_RETURN_DATUM(result);
}


/*
 * internal diagnostic function that takes a bson query that is passed
 * to the $text query and creates an equivalent TSQuery (used to debug queries).
 * e.g. '{ "$search": "my current query "}' -> '(my | current | query)'::tsquery
 */
Datum
command_bson_query_to_tsquery(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(0);
	Oid languageOid = InvalidOid;
	if (!PG_ARGISNULL(1))
	{
		char *language = TextDatumGetCString(PG_GETARG_TEXT_P(1));
		StringView languageView = { .string = language, .length = strlen(language) };

		bool isCreateIndex = false;
		languageOid = ExtractTsConfigFromLanguage(&languageView, isCreateIndex);
	}


	bson_value_t docValue = ConvertPgbsonToBsonValue(query);
	PG_RETURN_DATUM(BsonTextGenerateTSQueryCore(&docValue, NULL, languageOid));
}


/*
 * rum_bson_text_path_options sets up the option specification for single field text indexes
 * This initializes the structure that is used by the Index AM to process user specified
 * options on how to handle documents with the index.
 * For single field text indexes we need to track the path being indexed, and whether or not
 * it's a wildcard. Additionally, any language specific settings are tracked.
 * usage is as: using ExtensionObjectPrefix_rum(document rum_bson_text_path_options(path='a.b',iswildcard=true))
 * For more details see documentation on the 'options' method in the GIN extensibility.
 */
Datum
rum_bson_text_path_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(BsonGinTextPathOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_Text, /* default value */
							IndexOptionsType_Text, /* min */
							IndexOptionsType_Text, /* max */
							offsetof(BsonGinTextPathOptions, base.type));
	add_local_int_reloption(relopts, "version",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V0,         /* max */
							offsetof(BsonGinTextPathOptions, base.version));
	bool isWildcardDefault = false;
	add_local_bool_reloption(relopts, "iswildcard",
							 "Whether the path is a wildcard", isWildcardDefault,
							 offsetof(BsonGinTextPathOptions, isWildcard));
	add_local_string_reloption(relopts, "weights",
							   "The Paths and Weights for the index",
							   NULL, &ValidateWeightsSpec, &FillWeightsSpec,
							   offsetof(BsonGinTextPathOptions, weights));
	add_local_string_reloption(relopts, "indexname",
							   "[deprecated] The mongo specific name for the index",
							   NULL, NULL, &FillDeprecatedStringSpec,
							   offsetof(BsonGinTextPathOptions,
										base.intOption_deprecated));
	add_local_int_reloption(relopts, "indextermsize",
							"[deprecated] The index term size limit for truncation",
							-1, /* default value */
							-1, /* min */
							-1, /* max: text index terms shouldn't be truncated. */
							offsetof(BsonGinTextPathOptions, base.intOption_deprecated));
	add_local_int_reloption(relopts, "ts",
							"[deprecated] The index term size limit for truncation",
							-1, /* default value */
							-1, /* min */
							-1, /* max: text index terms shouldn't be truncated. */
							offsetof(BsonGinTextPathOptions, base.intOption_deprecated));
	add_local_string_reloption(relopts, "defaultlanguage",
							   "The mongo specific language for the index",
							   NULL, &ValidateDefaultLanguageSpec,
							   &FillDefaultLanguageSpec,
							   offsetof(BsonGinTextPathOptions, defaultLanguage));
	add_local_string_reloption(relopts, "languageoverride",
							   "The name of the path within the document that has the language override",
							   NULL, NULL, NULL,
							   offsetof(BsonGinTextPathOptions, languageOverride));
	PG_RETURN_VOID();
}


/*
 * This is a temporary hack to get the ts_rank data needed for the expression
 * evaluation syntax for sort, group and project. In reality this would be injected
 * into the $let framework. However, until that is done, we inject the options into
 * the process global. Since we only have 1 $text per query this is fine for now.
 */
Datum
bson_dollar_text_meta_qual(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	TSQuery query = PG_GETARG_TSQUERY(1);
	bytea *indexOptions = PG_GETARG_BYTEA_P(2);
	bool evaluateRuntimeCheck = PG_GETARG_BOOL(3);

	BsonGinTextPathOptions *textOptions = (BsonGinTextPathOptions *) indexOptions;

	/* If a runtime check is required, do it */
	if (evaluateRuntimeCheck)
	{
		TSVector vector = GenerateTsVectorWithOptions(document, textOptions);
		if (vector == NULL)
		{
			PG_RETURN_BOOL(false);
		}

		PG_RETURN_DATUM(OidFunctionCall2(TsMatchFunctionOid(), PointerGetDatum(vector),
										 PointerGetDatum(query)));
	}

	/* This always matches */
	PG_RETURN_BOOL(true);
}


/*
 * Given a '$text' filter and an associated index with options, generates a
 * mongo compatible TSQuery that can be used to query the index.
 */
Datum
BsonTextGenerateTSQuery(const bson_value_t *queryValue, bytea *indexOptions)
{
	return BsonTextGenerateTSQueryCore(queryValue, indexOptions, InvalidOid);
}


/*
 * Parses the $text filter and validates the options for what's supported in helioapi.
 */
void
BsonValidateTextQuery(const bson_value_t *queryValue)
{
	bson_value_t searchValue = { 0 };
	bson_value_t caseSensitive = { 0 };
	bson_value_t diacriticSensitive = { 0 };
	Oid languageOid = InvalidOid;
	BsonValidateAndExtractTextQuery(queryValue, &searchValue,
									&languageOid, &caseSensitive, &diacriticSensitive);
}


/*
 * Generates a FuncExpr that can be used later in the query to evaluate $meta scenarios.
 */
Expr *
GetFuncExprForTextWithIndexOptions(List *args, bytea *indexOptions, bool doRuntimeScan,
								   QueryTextIndexData *textIndexData)
{
	/* First args is the same (from the input - it's just the document) */
	Node *documentArg = linitial(args);
	if (!IsA(documentArg, Var))
	{
		return NULL;
	}

	/* Second arg is the TSQuery to be processed */
	Node *node = lsecond(args);
	if (!IsA(node, Const))
	{
		ereport(ERROR, (errmsg(
							"Expecting a constant value for text meta processing function")));
	}

	Const *constVal = (Const *) node;
	Assert(constVal->consttype == TSQUERYOID);

	/* Third arg is the index options */
	bytea *copiedOptions = pg_detoast_datum_copy(indexOptions);
	Const *finalOptionsConst = makeConst(BYTEAOID, -1, InvalidOid, -1, PointerGetDatum(
											 copiedOptions), false, false);
	Const *doRuntimeScanConst = makeConst(BOOLOID, -1, InvalidOid, sizeof(bool),
										  doRuntimeScan, false, true);
	List *funcArgs = list_make4(documentArg, constVal, finalOptionsConst,
								doRuntimeScanConst);

	/* Set the text index data */
	textIndexData->indexOptions = copiedOptions;
	textIndexData->query = constVal->constvalue;

	return (Expr *) makeFuncExpr(BsonTextSearchMetaQualFuncId(), BOOLOID, funcArgs,
								 InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
}


/*
 * Evaluates the ts_rank function for a given document for a persisted global
 * state tsquery and index options (from bson_dollar_text_meta_qual).
 * Note: this is currently a hack until $let support is added.
 */
double
EvaluateMetaTextScore(pgbson *document)
{
	if (QueryTextData == NULL)
	{
		ereport(ERROR, (errcode(MongoLocation40218),
						errmsg(
							"query requires text score metadata, but it is not available")));
	}

	if (QueryTextData->indexOptions == NULL ||
		QueryTextData->query == (Datum) 0)
	{
		bool isDatumQueryNull = QueryTextData->query == (Datum) 0;
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							"query text data is provided, but required properties are null."),
						errhint(
							"query text data is provided, but required properties are null - indexOptions %d, query %d",
							QueryTextData->indexOptions == NULL,
							isDatumQueryNull)));
	}

	BsonGinTextPathOptions *textOptions =
		(BsonGinTextPathOptions *) QueryTextData->indexOptions;
	TSVector vector = GenerateTsVectorWithOptions(document, textOptions);

	if (vector == NULL)
	{
		ereport(ERROR, (errmsg("Unexpected, text vector is not valid for $meta query")));
	}

	/* Now call ts_rank with the vector and query */
	const char *pathSpecBytes = NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
	uint32_t pathCount;
	Get_Index_Path_Option(textOptions, weights, pathSpecBytes, pathCount);
#pragma GCC diagnostic pop

	/* The datum array is right after the path count */
	Datum *weightDatum = (Datum *) pathSpecBytes;
	ArrayType *rankArray = construct_array(weightDatum, 4, FLOAT4OID, sizeof(float), true,
										   TYPALIGN_INT);

	Datum result = OidFunctionCall3(TsRankFunctionId(),
									PointerGetDatum(rankArray),
									PointerGetDatum(vector),
									QueryTextData->query);

	return (double) DatumGetFloat4(result);
}


/*
 * Validates that the order by is for a text search
 * i.e. the document { "$meta": "textScore" }
 */
bool
TryCheckMetaScoreOrderBy(const bson_value_t *value)
{
	pgbsonelement metaOrderingElement = { 0 };

	if (value->value_type != BSON_TYPE_DOCUMENT)
	{
		return false;
	}

	if (TryGetBsonValueToPgbsonElement(value, &metaOrderingElement) &&
		metaOrderingElement.pathLength == 5 &&
		strncmp(metaOrderingElement.path, "$meta", 5) == 0)
	{
		if (metaOrderingElement.bsonValue.value_type != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(MongoLocation31138),
							errmsg("Illegal $meta sort: $meta: \"%s\"",
								   BsonValueToJsonForLogging(
									   &metaOrderingElement.bsonValue))));
		}

		if (metaOrderingElement.bsonValue.value.v_utf8.len != 9 ||
			strncmp(metaOrderingElement.bsonValue.value.v_utf8.str, "textScore", 9) != 0)
		{
			ereport(ERROR, (errcode(MongoLocation31138),
							errmsg("$meta for sort only allows textScore not %s",
								   metaOrderingElement.bsonValue.value.v_utf8.str)));
		}

		bson_iter_t documentIterator;
		BsonValueInitIterator(value, &documentIterator);
		if (!TryGetSinglePgbsonElementFromBsonIterator(&documentIterator,
													   &metaOrderingElement))
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"Cannot have additional keys in a $meta sort specification")));
		}

		return true;
	}

	return false;
}


/*
 * Takes a '$text' query, index options and a text search configuration, and produces a datum
 * containing the tsquery for the index.
 */
static Datum
BsonTextGenerateTSQueryCore(const bson_value_t *queryValue, bytea *indexOptions, Oid
							tsConfigOid)
{
	if (queryValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("$text expects an object")));
	}

	bson_iter_t queryIterator;
	BsonValueInitIterator(queryValue, &queryIterator);

	bson_value_t searchValue = { 0 };
	Oid languageOid = InvalidOid;
	bson_value_t caseSensitive = { 0 };
	bson_value_t diacriticSensitive = { 0 };
	BsonValidateAndExtractTextQuery(queryValue, &searchValue,
									&languageOid, &caseSensitive, &diacriticSensitive);

	/* If language is provided, extract it */
	if (languageOid != InvalidOid)
	{
		tsConfigOid = languageOid;
	}
	else if (indexOptions != NULL)
	{
		BsonGinTextPathOptions *options = (BsonGinTextPathOptions *) indexOptions;
		tsConfigOid = GetDefaultLanguage(options);
	}
	else
	{
		tsConfigOid = getTSCurrentConfig(true);
	}

	/* we have a valid ts_query string. */
	/* first pass: we use the websearch_to_tsquery as it has the closest rules to native mongo; */
	Datum textDatum = CStringGetTextDatum(searchValue.value.v_utf8.str);

	Datum result;
	if (tsConfigOid != InvalidOid)
	{
		result = OidFunctionCall2(WebSearchToTsQueryWithRegConfigFunctionId(),
								  tsConfigOid, textDatum);
	}
	else
	{
		result = OidFunctionCall1(WebSearchToTsQueryFunctionId(), textDatum);
	}

	/* Now get the TSQuery result */
	TSQuery query = DatumGetTSQuery(result);

	/* This TSQuery does an "AND" for phrases to check. Convert the AND to OR (mongo behavior) */
	/* we leave the <=> phrase operator as-is. */
	/* first convert it to a tree. */
	QTNode *node = QT2QTN(GETQUERY(query), GETOPERAND(query));

	/* now walk the tree and convert AND to OR. */
	QTNTernary(node);
	bool rewritten = false;
	node = RewriteQueryTree(node, &rewritten);

	/* Recreate the TSQuery */
	if (rewritten)
	{
		QTNBinary(node);
		query = QTN2QT(node);
	}

	QTNFree(node);
	return PointerGetDatum(query);
}


/*
 * We use websearch_to_tsquery to convert a string to a TSQuery (Since it's the closest to
 * what Mongo semantics are). However, websearch_to_tsquery applies:
 * - "phrases are quoted" - same as Mongo
 * - hyphenated are "NOT"s
 * - Phrases are "AND"ed - Same as Mongo
 * - the default phrases are "AND"ed (Does not match Mongo)
 *
 * To resolve these, we walk the TSQuery generated, and look at the top level.
 * We then group all the phrases under an 'OR' and leave the rest as ANDed.
 */
static QTNode *
RewriteQueryTree(QTNode *node, bool *rewrote)
{
	*rewrote = false;
	if (node->child == NULL)
	{
		return node;
	}

	int numNegation = 0;
	int numTerms = 0;
	int numPhrases = 0;
	for (int i = 0; i < node->nchild; i++)
	{
		if (node->child[i]->valnode->type != QI_OPR)
		{
			numTerms++;
			continue;
		}

		QueryOperator op = node->child[i]->valnode->qoperator;
		switch (op.oper)
		{
			case OP_NOT:
			{
				numNegation++;
				continue;
			}

			case OP_PHRASE:
			{
				numPhrases++;
				continue;
			}

			default:
			{
				numTerms++;
				continue;
			}
		}
	}

	/* Only loose terms - convert AND to OR and return */
	if (numNegation == 0 && numPhrases == 0)
	{
		if (node->valnode &&
			node->valnode->type == QI_OPR &&
			node->valnode->qoperator.oper == OP_AND)
		{
			*rewrote = true;
			node->valnode->qoperator.oper = OP_OR;
		}

		return node;
	}

	if (numTerms <= 1)
	{
		/* Only 1 term needs an OR - nothing to do - just leave everything as AND */
		return node;
	}

	/* Do another pass, but this time collect the terms */
	List *negationTerms = NIL;
	List *regularTerms = NIL;
	List *phrases = NIL;
	if (node->child != NULL)
	{
		for (int i = 0; i < node->nchild; i++)
		{
			if (node->child[i]->valnode->type != QI_OPR)
			{
				regularTerms = lappend(regularTerms, node->child[i]);
				continue;
			}

			QueryOperator op = node->child[i]->valnode->qoperator;
			switch (op.oper)
			{
				case OP_NOT:
				{
					negationTerms = lappend(negationTerms, node->child[i]);
					continue;
				}

				case OP_PHRASE:
				{
					phrases = lappend(phrases, node->child[i]);
					continue;
				}

				case OP_OR:
				case OP_AND:
				default:
				{
					/* This is unexpected - error for now */
					ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
									errmsg("Unsupported text search query")));
				}
			}
		}
	}

	/* Build a new tree. */
	*rewrote = true;
	QTNode *newRoot = (QTNode *) palloc0(sizeof(QTNode));
	newRoot->valnode = (QueryItem *) palloc0(sizeof(QueryItem));
	newRoot->flags = node->flags | QTN_NEEDFREE;
	newRoot->valnode->type = QI_OPR;
	newRoot->valnode->qoperator.oper = OP_AND;
	newRoot->sign = node->sign;

	/* Number of AND phrases is negations, phrases, and 1 for the "OR" of terms */
	newRoot->nchild = list_length(negationTerms) + list_length(phrases) + 1;

	newRoot->child = (QTNode **) palloc(sizeof(QTNode *) * newRoot->nchild);

	ListCell *cell;
	int i = 0;

	/* Add negation terms to the top level AND */
	if (negationTerms != NIL)
	{
		foreach(cell, negationTerms)
		{
			newRoot->child[i] = lfirst(cell);
			i++;
		}
	}

	/* Add phrase terms to the top level AND */
	if (phrases != NIL)
	{
		foreach(cell, phrases)
		{
			newRoot->child[i] = lfirst(cell);
			i++;
		}
	}

	/* Regular terms to a single OR at the top level */
	if (regularTerms != NIL)
	{
		QTNode *termRoot = (QTNode *) palloc0(sizeof(QTNode));
		termRoot->valnode = (QueryItem *) palloc0(sizeof(QueryItem));
		termRoot->flags = node->flags | QTN_NEEDFREE;
		termRoot->valnode->type = QI_OPR;
		termRoot->valnode->qoperator.oper = OP_OR;

		termRoot->nchild = list_length(regularTerms);
		termRoot->child = (QTNode **) palloc(sizeof(QTNode *) * termRoot->nchild);

		int j = 0;
		foreach(cell, regularTerms)
		{
			termRoot->child[j] = lfirst(cell);
			termRoot->sign |= termRoot->child[j]->sign;
			j++;
		}

		newRoot->child[i] = termRoot;
		i++;
	}

	Assert(i == newRoot->nchild);

	return newRoot;
}


inline static IndexTraverseOption
FormatTraverseOptionForText(IndexTraverseOption pathOption, bson_type_t bsonType)
{
	if (pathOption == IndexTraverse_Match)
	{
		switch (bsonType)
		{
			case BSON_TYPE_DOCUMENT:
			case BSON_TYPE_ARRAY:
			{
				pathOption = IndexTraverse_Recurse;
				break;
			}

			case BSON_TYPE_UTF8:
			{
				pathOption = IndexTraverse_Match;
				break;
			}

			default:
			{
				pathOption = IndexTraverse_Invalid;
				break;
			}
		}
	}

	return pathOption;
}


/*
 * Implements the GetTextIndexTraverseOption for index term generation.
 * Validates that the term is valid given tha path filter, and also ensures that
 * the path is a "TEXTT or "array" of texts.
 */
static IndexTraverseOption
GetTextIndexTraverseOption(void *contextOptions,
						   const char *currentPath, uint32_t currentPathLength,
						   bson_type_t bsonType)
{
	BsonGinTextPathOptions *indexOpts = (BsonGinTextPathOptions *) contextOptions;

	if (indexOpts->isWildcard)
	{
		/* Root wildcard */
		return FormatTraverseOptionForText(IndexTraverse_Match, bsonType);
	}

	uint32_t pathCount = 0;
	const char *pathSpecBytes = "";
	Get_Index_Path_Option(indexOpts, weights, pathSpecBytes, pathCount);

	/* Skip over the weights */
	pathSpecBytes += sizeof(Datum) * 4;
	IndexTraverseOption overallOption = IndexTraverse_Invalid;
	for (uint32_t i = 0; i < pathCount; i++)
	{
		uint32_t indexPathLength = *(uint32_t *) pathSpecBytes;
		const char *indexPath = pathSpecBytes + sizeof(uint32_t);

		/* Skip through other metadata */
		pathSpecBytes += indexPathLength + sizeof(uint32_t) + sizeof(char);

		IndexTraverseOption pathOption = GetSinglePathIndexTraverseOptionCore(indexPath,
																			  indexPathLength,
																			  currentPath,
																			  currentPathLength,
																			  indexOpts->
																			  isWildcard);

		pathOption = FormatTraverseOptionForText(pathOption, bsonType);
		if (pathOption == IndexTraverse_Match)
		{
			return pathOption;
		}
		else if (pathOption == IndexTraverse_Recurse)
		{
			/* Set the overall best option so far, and try another path.
			 * If that path says "match" we'll prefer that.
			 * If the other path is invalid, we'll at least recurse this.
			 */
			overallOption = pathOption;
		}
	}

	return overallOption;
}


/*
 * Given a mongo language declaration e.g. 'en' 'pt' etc. Converts it to
 * an equivalent regconfig Oid.
 */
static Oid
ExtractTsConfigFromLanguage(const StringView *language,
							bool isCreateIndex)
{
	bool missingOk = true;
	if (StringViewEqualsCStringCaseInsensitive(language, "none"))
	{
		List *language = list_make2(
			makeString("pg_catalog"),
			makeString("simple"));
		Oid languageOid = get_ts_config_oid(language, missingOk);
		if (languageOid != InvalidOid)
		{
			return languageOid;
		}
	}

	/* First canonicalize language to a PG supported form and parse out
	 * the ones from Mongo supported values
	 */
	for (int i = 0; i < NumberOfLanguages; i++)
	{
		if (StringViewEqualsCStringCaseInsensitive(language,
												   LanguageExpressions[i].
												   mongoLanguageName) ||
			StringViewEqualsCStringCaseInsensitive(language,
												   LanguageExpressions[i].
												   postgresLanguageName))
		{
			/* We have a match, look up the PG language regconfig */
			List *language = list_make2(
				makeString("pg_catalog"),
				makeString((char *) LanguageExpressions[i].postgresLanguageName));
			Oid languageOid = get_ts_config_oid(language, missingOk);
			if (languageOid != InvalidOid)
			{
				return languageOid;
			}
		}
	}

	int errorCode = isCreateIndex ? MongoCannotCreateIndex : MongoBadValue;
	ereport(ERROR, (errcode(errorCode),
					errmsg("unsupported language: \"%.*s\" for text index version 3",
						   language->length, language->string)));

	/* keep the compiler happy. */
	return InvalidOid;
}


/*
 * Does the walking and extracting of necessary fields from the '$text' query.
 */
static void
BsonValidateAndExtractTextQuery(const bson_value_t *queryValue,
								bson_value_t *searchValue, Oid *languageOid,
								bson_value_t *caseSensitive,
								bson_value_t *diacriticSensitive)
{
	if (queryValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("$text expects an object")));
	}

	bson_iter_t queryIterator;
	BsonValueInitIterator(queryValue, &queryIterator);

	bson_value_t languageValue = { 0 };
	searchValue->value_type = BSON_TYPE_EOD;
	caseSensitive->value_type = BSON_TYPE_EOD;
	diacriticSensitive->value_type = BSON_TYPE_EOD;
	while (bson_iter_next(&queryIterator))
	{
		const char *path = bson_iter_key(&queryIterator);
		if (strcmp(path, "$search") == 0)
		{
			*searchValue = *bson_iter_value(&queryIterator);
		}
		else if (strcmp(path, "$language") == 0)
		{
			languageValue = *bson_iter_value(&queryIterator);
		}
		else if (strcmp(path, "$caseSensitive") == 0)
		{
			*caseSensitive = *bson_iter_value(&queryIterator);
		}
		else if (strcmp(path, "$diacriticSensitive") == 0)
		{
			*diacriticSensitive = *bson_iter_value(&queryIterator);
		}
	}

	if (searchValue->value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Missing expected field \"$search\"")));
	}

	if (searchValue->value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("$search had the wrong type. Expected string, found %s",
							   BsonTypeName(searchValue->value_type))));
	}

	if (languageValue.value_type != BSON_TYPE_EOD &&
		languageValue.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("$language had the wrong type. Expected string, found %s",
							   BsonTypeName(languageValue.value_type))));
	}

	if (caseSensitive->value_type != BSON_TYPE_EOD &&
		caseSensitive->value_type != BSON_TYPE_BOOL)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$caseSensitive had the wrong type. Expected bool, found %s",
							BsonTypeName(caseSensitive->value_type))));
	}

	if (diacriticSensitive->value_type != BSON_TYPE_EOD &&
		diacriticSensitive->value_type != BSON_TYPE_BOOL)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$diacriticSensitive had the wrong type. Expected bool, found %s",
							BsonTypeName(diacriticSensitive->value_type))));
	}

	if (caseSensitive->value_type != BSON_TYPE_EOD &&
		caseSensitive->value.v_bool)
	{
		/* We don't yet support case sensitive searches */
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("$caseSensitive searches are not supported yet")));
	}

	if (diacriticSensitive->value_type != BSON_TYPE_EOD &&
		!diacriticSensitive->value.v_bool)
	{
		/* We don't yet support diacriticSensitive searches */
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg("$diacritic insensitive searches are not supported yet")));
	}

	if (languageValue.value_type == BSON_TYPE_UTF8)
	{
		StringView languageView = {
			.string = languageValue.value.v_utf8.str, .length =
				languageValue.value.v_utf8.len
		};

		bool isCreateIndex = false;
		*languageOid = ExtractTsConfigFromLanguage(&languageView, isCreateIndex);
	}
}


/*
 * Validates the language spec provided to a CREATE INDEX text search options.
 */
static void
ValidateDefaultLanguageSpec(const char *defaultLanguage)
{
	if (defaultLanguage != NULL)
	{
		StringView languageView = {
			.string = defaultLanguage, .length = strlen(defaultLanguage)
		};

		/* Validate the language - ignore the output here. */
		bool isCreateIndex = true;
		ExtractTsConfigFromLanguage(&languageView, isCreateIndex);
	}
}


/*
 * Populates the language spec provided to a CREATE INDEX text search options.
 */
static Size
FillDefaultLanguageSpec(const char *defaultLanguage, void *buffer)
{
	uint32_t length = defaultLanguage == NULL ? 0 : sizeof(Oid);

	if (buffer != NULL)
	{
		if (defaultLanguage != NULL)
		{
			Oid *address = (Oid *) buffer;
			StringView languageView = {
				.string = defaultLanguage, .length = strlen(defaultLanguage)
			};
			bool isCreateIndex = true;
			*address = ExtractTsConfigFromLanguage(&languageView, isCreateIndex);
		}
	}

	return length;
}


/*
 * Populates the index option weights into the index spec
 * This includes the path and associated weights encoded as follows
 * <numPaths><Datum[4] of weights>[<pathLength><path><weightIndex>]+
 */
static Size
FillWeightsSpec(const char *weightsSpec, void *buffer)
{
	/* Weights count + Weight array (for rank) */
	Size sizeRequired = sizeof(uint32_t) + (sizeof(Datum) * 4);

	pgbson *bson;
	if (weightsSpec != NULL && strlen(weightsSpec) > 0)
	{
		bson = PgbsonInitFromJson(weightsSpec);
	}
	else
	{
		bson = PgbsonInitEmpty();
	}

	bson_iter_t weightsIter;
	PgbsonInitIterator(bson, &weightsIter);

	int pathCount = 0;
	float4 maxWeight = 0;
	while (bson_iter_next(&weightsIter))
	{
		pathCount++;

		StringView pathView = bson_iter_key_string_view(&weightsIter);
		if (pathView.length == 0)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"filter must have a valid path")));
		}

		/* add the prefixed path length */
		sizeRequired += sizeof(uint32_t);

		/* add the path size */
		sizeRequired += pathView.length;

		/* Add 1 character for 'weight' char (A, B, C, D)*/
		sizeRequired++;

		float4 weight = (float4) BsonValueAsDouble(bson_iter_value(&weightsIter));
		maxWeight = Max(weight, maxWeight);
	}

	maxWeight = Max(maxWeight, 1.0);
	if (buffer != NULL)
	{
		char *bufferPtr = (char *) buffer;
		*((uint32_t *) bufferPtr) = pathCount;
		bufferPtr += sizeof(uint32_t);

		/* Save space for the weights array */
		Datum *weightDatums = (Datum *) bufferPtr;
		bufferPtr += (sizeof(Datum) * 4);

		for (int i = 0; i < 4; i++)
		{
			/* Initialize it to the default */
			weightDatums[i] = Float4GetDatum(1.0f / maxWeight);
		}

		PgbsonInitIterator(bson, &weightsIter);

		char weightChar = 1;
		while (bson_iter_next(&weightsIter))
		{
			StringView pathView = bson_iter_key_string_view(&weightsIter);

			/* add the prefixed path length */
			*((uint32_t *) bufferPtr) = pathView.length;
			bufferPtr += sizeof(uint32_t);

			/* Add the path */
			memcpy(bufferPtr, pathView.string, pathView.length);
			bufferPtr += pathView.length;

			/* Add the weight */
			double weight = BsonValueAsDouble(bson_iter_value(&weightsIter));

			char weightLabel = 0;
			if (weight != 1.0)
			{
				/* Postgres supports only 4 weights A, B, C, D */
				/* Since we reserve "D" as weight 1, we can only have 3 custom weights */
				if (weightChar >= 4)
				{
					ereport(ERROR, (errcode(MongoCommandNotSupported),
									errmsg(
										"Cannot have more than 3 custom weights in the index")));
				}

				weightLabel = weightChar;
				weightChar++;
			}

			*bufferPtr = weightLabel;
			bufferPtr++;
			switch (weightLabel)
			{
				case 0:
				case 1:
				case 2:
				case 3:
				{
					weightDatums[(int) weightLabel] = Float4GetDatum(((float) weight /
																	  maxWeight));
					break;
				}

				default:
				{
					elog(INFO, "unrecognized weight datum");
					Assert(false);
				}
			}
		}
	}

	return sizeRequired;
}


/*
 * Validates the incoming weight spec into create_indexes
 */
static void
ValidateWeightsSpec(const char *weightsSpec)
{
	if (weightsSpec == NULL || strlen(weightsSpec) == 0)
	{
		/* Wildcard */
		return;
	}

	pgbson *bson = PgbsonInitFromJson(weightsSpec);

	int numCustomWeights = 0;
	bson_iter_t weightsIter;
	PgbsonInitIterator(bson, &weightsIter);

	while (bson_iter_next(&weightsIter))
	{
		if (bson_iter_key_len(&weightsIter) == 0)
		{
			ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
							errmsg("Weights must have a valid path")));
		}

		double value = BsonValueAsDouble(bson_iter_value(&weightsIter));
		if (value != 1.0)
		{
			numCustomWeights++;
		}
	}

	/* Postgres supports only 4 weights A, B, C, D */
	/* Since we reserve "D" as weight 1, we can only have 3 custom weights */
	if (numCustomWeights > 3)
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("Cannot have more than 3 custom weights in the index")));
	}
}


/*
 * Gets the per path language override if available. Mongo allows each path
 * to optionally override the language:
 * e.g.
 * { "a": { "b": "foo", "language": "es"}}
 * will interpret a.b as a spanish phrase
 * while
 * { "a": { "b": "foo"}}
 * will use the default language of the index.
 */
static Oid
GetLanguagePathOverride(const StringView *pathView, StringView *lastParentPath,
						const StringView *languagePath, Oid *lastLanguageOid,
						StringView *languagePathBufferView, pgbson *document,
						Oid defaultLanguageOid)
{
	bson_iter_t docIterator;
	StringView prefix = StringViewFindPrefix(pathView, '.');
	if (StringViewEquals(&prefix, lastParentPath) && *lastLanguageOid != InvalidOid)
	{
		return *lastLanguageOid;
	}

	/* path 1 + '.' + path2 + '\0'; */
	uint32_t requiredPathLength = prefix.length + languagePath->length + 2;
	if (languagePathBufferView->length < requiredPathLength)
	{
		if (languagePathBufferView->string == NULL)
		{
			languagePathBufferView->string = palloc(requiredPathLength);
		}
		else
		{
			languagePathBufferView->string = repalloc(
				(char *) languagePathBufferView->string, requiredPathLength);
		}

		languagePathBufferView->length = requiredPathLength;
	}

	char *buffer = (char *) languagePathBufferView->string;
	int offset = 0;
	if (prefix.length > 0)
	{
		memcpy(buffer, prefix.string, prefix.length);
		buffer[prefix.length] = '.';
		offset = prefix.length + 1;
	}
	memcpy(&buffer[offset], languagePath->string, languagePath->length);
	buffer[offset + languagePath->length] = 0;
	if (PgbsonInitIteratorAtPath(document, buffer, &docIterator))
	{
		if (bson_iter_type(&docIterator) != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(MongoLocation17261),
							errmsg(
								"found language override field in document with non-string type")));
		}

		StringView languageView = { 0 };
		languageView.string = bson_iter_utf8(&docIterator, &languageView.length);

		bool isCreateIndex = false;
		Oid languageOid = ExtractTsConfigFromLanguage(&languageView, isCreateIndex);
		*lastLanguageOid = languageOid;
		*lastParentPath = prefix;
	}
	else
	{
		*lastLanguageOid = defaultLanguageOid;
		*lastParentPath = prefix;
	}

	return *lastLanguageOid;
}


/*
 * Helper function given a document and an index spec, generates
 * the TSVector that would be inserted given the options.
 */
static TSVector
GenerateTsVectorWithOptions(pgbson *document,
							BsonGinTextPathOptions *options)
{
	GenerateTermsContext context = { 0 };
	context.options = (void *) options;
	context.traverseOptionsFunc = &GetTextIndexTraverseOption;
	context.generateNotFoundTerm = false;

	bool generateRootTerm = false;
	GenerateTerms(document, &context, generateRootTerm);

	/* Phase 2: Generate TSVector */

	/* Documents can provide a language override */
	Oid collationConfigurationOid = GetDefaultLanguage(options);

	TSVector overallVector = NULL;

	char *languagePathOverride = GET_STRING_RELOPTION(options, languageOverride);
	if (languagePathOverride == NULL)
	{
		languagePathOverride = "language";
	}

	StringView languagePathView =
	{
		.string = languagePathOverride,
		.length = strlen(languagePathOverride)
	};

	StringView lastParentPath = { 0 };
	Oid lastLanguageOid = InvalidOid;

	StringView languagePathBuffer = { 0 };

	for (int i = 0; i < context.totalTermCount; i++)
	{
		BsonIndexTerm term = { 0 };
		InitializeBsonIndexTerm(DatumGetByteaP(context.terms.entries[i]), &term);

		if (term.element.bsonValue.value_type == BSON_TYPE_UTF8)
		{
			/* Need to check for language override here */
			StringView pathView =
			{
				.string = term.element.path,
				.length = term.element.pathLength
			};

			Oid languageOid = GetLanguagePathOverride(&pathView, &lastParentPath,
													  &languagePathView, &lastLanguageOid,
													  &languagePathBuffer, document,
													  collationConfigurationOid);
			if (languageOid == InvalidOid)
			{
				languageOid = collationConfigurationOid;
			}

			/* Generate text words */
			ParsedText text = { 0 };

			/* Random estimate of word count (see to_tsvector) */
			text.lenwords = Max(term.element.bsonValue.value.v_utf8.len / 6, 2);
			text.curwords = 0;
			text.pos = 0;
			text.words = (ParsedWord *) palloc(sizeof(ParsedWord) * text.lenwords);
			parsetext(
				languageOid,
				&text,
				term.element.bsonValue.value.v_utf8.str,
				term.element.bsonValue.value.v_utf8.len);

			/* make the tsvector from it */
			TSVector vector = make_tsvector(&text);

			/* Determine the weight */
			int weight = 0;
			uint32_t pathCount = 0;
			const char *pathSpecBytes = "";
			Get_Index_Path_Option(options, weights, pathSpecBytes, pathCount);

			/* Skip over weight values */
			pathSpecBytes += sizeof(Datum) * 4;
			for (uint32_t i = 0; i < pathCount; i++)
			{
				uint32_t indexPathLength = *(uint32_t *) pathSpecBytes;
				const char *indexPath = pathSpecBytes + sizeof(uint32_t);
				char pathWeight = *(char *) (pathSpecBytes + indexPathLength +
											 sizeof(uint32_t));

				/* Skip through other metadata */
				pathSpecBytes += indexPathLength + sizeof(uint32_t) + sizeof(char);

				if (term.element.pathLength == indexPathLength &&
					strncmp(term.element.path, indexPath, indexPathLength) == 0)
				{
					weight = pathWeight;
					break;
				}
			}

			/* Set up the weight. see tsvector_setweight (this does this inline) */
			WordEntry *entry = ARRPTR(vector);
			int i = vector->size;
			int j = 0;
			while (i--)
			{
				if ((j = POSDATALEN(vector, entry)) != 0)
				{
					WordEntryPos *p = POSDATAPTR(vector, entry);
					while (j--)
					{
						WEP_SETWEIGHT(*p, weight);
						p++;
					}
				}

				entry++;
			}

			/* Now we concat with the parent vector */
			if (overallVector == NULL)
			{
				overallVector = vector;
			}
			else
			{
				overallVector =
					DatumGetTSVector(OidFunctionCall2(TsVectorConcatFunctionId(),
													  PointerGetDatum(vector),
													  PointerGetDatum(overallVector)));
			}
		}
	}

	if (languagePathBuffer.string != NULL)
	{
		pfree((char *) languagePathBuffer.string);
	}

	return overallVector;
}
