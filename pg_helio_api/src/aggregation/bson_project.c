/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_project.c
 *
 * Implementation of BSON projection functions.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>
#include <executor/executor.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <utils/float.h>
#include <parser/parse_coerce.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>

#include "aggregation/bson_project.h"
#include "types/decimal128.h"
#include "aggregation/bson_positional_query.h"
#include "aggregation/bson_tree_write.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "query/helio_bson_compare.h"
#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"
#include "operators/bson_expression.h"
#include "operators/bson_expr_eval.h"
#include "aggregation/bson_project_operator.h"
#include "utils/fmgr_utils.h"
#include "commands/commands_common.h"

/* --------------------------------------------------------- */
/* Error-Messages */
/* --------------------------------------------------------- */

const char COLLISION_ERR_MSG[75] =
	"Invalid specification for aggregation stage:: Path collision detected.";

/* --------------------------------------------------------- */
/* Data types */
/* --------------------------------------------------------- */

/*
 * State for projection that is cached across
 * query calls.
 */
typedef struct BsonProjectionQueryState
{
	/* The bson path tree that is constructed
	 * from parsing the projection type spec */
	const BsonIntermediatePathNode *root;

	const ExpressionVariableContext *variableContext;

	/* Whether or not the projection has an inclusion */
	bool hasInclusion;

	/* Whether or not the projection has an exclusion */
	bool hasExclusion;

	/* Whether or not we project non-matching fields in the
	 * document onto the target document */
	bool projectNonMatchingFields;

	/* Total number of projections that needs to come at the end */
	uint32_t endTotalProjections;

	/* Optional: Bson Project Document stage function hooks */
	BsonProjectDocumentFunctions projectDocumentFuncs;
} BsonProjectionQueryState;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

/* projection path building functions */
static BsonIntermediatePathNode * BuildBsonUnsetPathTree(const bson_value_t *unsetValue);
static void AdjustPathProjectionsForId(BsonIntermediatePathNode *tree, bool hasInclusion,
									   bool forceProjectId, bool *hasExclusion);

/* projection writer functions */
static void ProjectCurrentIteratorFieldToWriter(bson_iter_t *documentIterator,
												const BsonIntermediatePathNode *
												pathSpecTree,
												pgbson_writer *writer,
												bool projectNonMatchingFields,
												Bitmapset **fieldHandledBitMap,
												ProjectDocumentState *projectDocState,
												bool isInNestedArray);
static void HandleUnresolvedFields(const BsonIntermediatePathNode *parentNode,
								   Bitmapset *fieldBitMapSet,
								   pgbson_writer *writer,
								   pgbson *parentDocument,
								   const ExpressionVariableContext *variableContext);
static void TraverseArrayAndAppendToWriter(bson_iter_t *parentIterator,
										   pgbson_array_writer *writer,
										   const BsonIntermediatePathNode *pathNode,
										   bool projectNonMatchingFields,
										   ProjectDocumentState *projectDocState,
										   bool isInNestedArray);
static void ProjectCurrentArrayIterToWriter(bson_iter_t *arrayIter,
											pgbson_array_writer *writer,
											const BsonIntermediatePathNode *pathNode,
											bool projectNonMatchingFields,
											ProjectDocumentState *projectDocState,
											bool isInNestedArray);
static bool TraverseDocumentAndWriteLookupIndexCondition(pgbson_array_writer *arrayWriter,
														 bson_iter_t *documentIterator,
														 const char *path, int
														 pathLength);

static pgbson * BsonLookUpGetFilterExpression(pgbson *sourceDocument,
											  pgbsonelement *lookupSpecElement);

static pgbson * BsonLookUpProject(pgbson *sourceDocument, int numMatchedDocuments,
								  Datum *mathedArray, char *matchedDocsFieldName);
static void PopulateReplaceRootExpressionDataFromSpec(
	AggregationExpressionData *expressionData, pgbson *pathSpec);

static void BuildBsonPathTreeForDollarProject(BsonProjectionQueryState *state,
											  BsonProjectionContext *context);
static void BuildBsonPathTreeForDollarAddFields(BsonProjectionQueryState *state,
												bson_iter_t *addFieldsSpec,
												bool skipParseAggregationExpressions);
static void BuildBsonPathTreeForDollarUnset(BsonProjectionQueryState *state,
											const bson_value_t *unsetValue,
											bool forceProjectId);
static void BuildBsonPathTreeForDollarProjectFind(BsonProjectionQueryState *state,
												  BsonProjectionContext *projectionContext);
static void BuildBsonPathTreeForDollarProjectCore(BsonProjectionQueryState *state,
												  BsonProjectionContext *projectionContext,
												  BuildBsonPathTreeContext *
												  pathTreeContext);
static bool FilterNodeToWrite(void *state, int currentIndex);
static void PostProcessParseProjectNode(void *state, const StringView *path,
										BsonPathNode *node,
										bool *isExclusionIfNoInclusion,
										bool *hasFieldsForIntermediate);
static pgbson * ProjectGeonearDocument(const GeonearDistanceState *state,
									   pgbson *document);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(bson_dollar_project);
PG_FUNCTION_INFO_V1(bson_dollar_project_find);
PG_FUNCTION_INFO_V1(bson_dollar_add_fields);
PG_FUNCTION_INFO_V1(bson_dollar_set);
PG_FUNCTION_INFO_V1(bson_dollar_unset);
PG_FUNCTION_INFO_V1(bson_dollar_replace_root);
PG_FUNCTION_INFO_V1(bson_dollar_merge_documents);
PG_FUNCTION_INFO_V1(bson_dollar_lookup_extract_filter_expression);
PG_FUNCTION_INFO_V1(bson_dollar_lookup_extract_filter_array);
PG_FUNCTION_INFO_V1(bson_dollar_lookup_project);
PG_FUNCTION_INFO_V1(bson_dollar_facet_project);
PG_FUNCTION_INFO_V1(bson_dollar_project_geonear);

/*
 * bson_dollar_project performs a projection of one or more paths in a binary serialized bson for aggregation $project
 */
Datum
bson_dollar_project(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *pathSpec = PG_GETARG_PGBSON(1);

	/* project_find with empty projection spec is a no-op */
	if (IsPgbsonEmptyDocument(pathSpec))
	{
		PG_RETURN_POINTER(document);
	}

	const BsonProjectionQueryState *state;

	int argPosition = 1;

	bson_iter_t pathSpecIter;
	PgbsonInitIterator(pathSpec, &pathSpecIter);
	BsonProjectionContext context = {
		.forceProjectId = false,
		.allowInclusionExclusion = false,
		.pathSpecIter = &pathSpecIter,
		.querySpec = NULL,
	};
	SetCachedFunctionState(
		state,
		BsonProjectionQueryState,
		argPosition,
		BuildBsonPathTreeForDollarProject,
		&context);

	if (state == NULL)
	{
		BsonProjectionQueryState projectionState = { 0 };
		BuildBsonPathTreeForDollarProject(&projectionState, &context);
		PG_RETURN_POINTER(ProjectDocumentWithState(document, &projectionState));
	}
	else
	{
		PG_RETURN_POINTER(ProjectDocumentWithState(document, state));
	}
}


/*
 * bson_dollar_project_find performs a projection of one or more paths in a binary serialized bson for find operation
 */
Datum
bson_dollar_project_find(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *pathSpec = PG_GETARG_PGBSON(1);
	pgbson *querySpec = PG_GETARG_MAYBE_NULL_PGBSON(2);

	if (querySpec == NULL)
	{
		querySpec = PgbsonInitEmpty();
	}

	/* project_project_find with empty projection spec and query spec is a no-op */
	if (IsPgbsonEmptyDocument(pathSpec) && IsPgbsonEmptyDocument(querySpec))
	{
		PG_RETURN_POINTER(document);
	}

	const BsonProjectionQueryState *state;

	int argPosition = 1;

	bson_iter_t pathSpecIter;
	PgbsonInitIterator(pathSpec, &pathSpecIter);

	BsonProjectionContext context = {
		.forceProjectId = false,
		.allowInclusionExclusion = false,
		.pathSpecIter = &pathSpecIter,
		.querySpec = querySpec,
	};

	SetCachedFunctionState(
		state,
		BsonProjectionQueryState,
		argPosition,
		BuildBsonPathTreeForDollarProjectFind,
		&context);

	if (state == NULL)
	{
		BsonProjectionQueryState projectionState = { 0 };
		BuildBsonPathTreeForDollarProjectFind(&projectionState, &context);
		PG_RETURN_POINTER(ProjectDocumentWithState(document, &projectionState));
	}
	else
	{
		PG_RETURN_POINTER(ProjectDocumentWithState(document, state));
	}
}


/*
 * bson_dollar_project_geonear performs a projection for $geoNear aggregation stage
 */
Datum
bson_dollar_project_geonear(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	pgbson *geoNearQuery = PG_GETARG_PGBSON_PACKED(1);

	const GeonearDistanceState *state;
	int argPosition = 1;

	SetCachedFunctionState(
		state,
		GeonearDistanceState,
		argPosition,
		BuildGeoNearDistanceState,
		geoNearQuery);

	if (state == NULL)
	{
		GeonearDistanceState projectionState;
		memset(&projectionState, 0, sizeof(GeonearDistanceState));
		BuildGeoNearDistanceState(&projectionState, geoNearQuery);
		PG_RETURN_POINTER(ProjectGeonearDocument(&projectionState, document));
	}

	PG_RETURN_POINTER(ProjectGeonearDocument(state, document));
}


/*
 * Given a projection spec specified by the iterator,
 * builds a BsonProjectionQueryState that can later be used to
 * apply the projectionSpec on documents.
 */
const BsonProjectionQueryState *
GetProjectionStateForBsonProject(bson_iter_t *projectionSpecIter,
								 bool forceProjectId,
								 bool allowInclusionExclusion)
{
	BsonProjectionQueryState *projectionState = palloc0(sizeof(BsonProjectionQueryState));
	BsonProjectionContext context = {
		.pathSpecIter = projectionSpecIter,
		.forceProjectId = forceProjectId,
		.allowInclusionExclusion = allowInclusionExclusion,
		.querySpec = NULL,
	};
	BuildBsonPathTreeForDollarProject(projectionState, &context);

	/* TODO VARIABLE take as argument. */
	projectionState->variableContext = NULL;
	return projectionState;
}


/*
 * bson_dollar_add_fields performs
 *      (1) a projection of all the fields in a binary serialized bson,
 *      (2) evaluates and add new fields (driven by the addFields specs) to the projection.
 */
Datum
bson_dollar_add_fields(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *pathSpec = PG_GETARG_PGBSON(1);

	/* bson_dollar_add_fields with empty projection spec is a no-op */
	if (IsPgbsonEmptyDocument(pathSpec))
	{
		PG_RETURN_POINTER(document);
	}

	const BsonProjectionQueryState *state;

	int argPosition = 1;

	bool skipParseAggregationExpressions = false;
	bson_iter_t pathSpecIter;
	PgbsonInitIterator(pathSpec, &pathSpecIter);
	SetCachedFunctionState(
		state,
		BsonProjectionQueryState,
		argPosition,
		BuildBsonPathTreeForDollarAddFields,
		&pathSpecIter,
		skipParseAggregationExpressions);

	if (state == NULL)
	{
		BsonProjectionQueryState projectionState = { 0 };
		BuildBsonPathTreeForDollarAddFields(&projectionState, &pathSpecIter,
											skipParseAggregationExpressions);
		PG_RETURN_POINTER(ProjectDocumentWithState(document, &projectionState));
	}
	else
	{
		PG_RETURN_POINTER(ProjectDocumentWithState(document, state));
	}
}


/*
 * This is similar to the behavior of $addFields except it doesn't
 * consider any values as operators/expressions. Simply as Field constants.
 */
Datum
bson_dollar_merge_documents(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *pathSpec = PG_GETARG_PGBSON(1);

	/* bson_dollar_add_fields with empty projection spec is a no-op */
	if (IsPgbsonEmptyDocument(pathSpec))
	{
		PG_RETURN_POINTER(document);
	}

	const BsonProjectionQueryState *state;

	int argPosition = 1;

	bool skipParseAggregationExpressions = true;
	bson_iter_t pathSpecIter;
	PgbsonInitIterator(pathSpec, &pathSpecIter);
	SetCachedFunctionState(
		state,
		BsonProjectionQueryState,
		argPosition,
		BuildBsonPathTreeForDollarAddFields,
		&pathSpecIter,
		skipParseAggregationExpressions);

	if (state == NULL)
	{
		BsonProjectionQueryState projectionState = { 0 };
		BuildBsonPathTreeForDollarAddFields(&projectionState, &pathSpecIter,
											skipParseAggregationExpressions);
		PG_RETURN_POINTER(ProjectDocumentWithState(document, &projectionState));
	}
	else
	{
		PG_RETURN_POINTER(ProjectDocumentWithState(document, state));
	}
}


/*
 * Given a addFields spec specified by the iterator,
 * builds a BsonProjectionQueryState that can later be used to
 * apply the projectionSpec on documents.
 */
const BsonProjectionQueryState *
GetProjectionStateForBsonAddFields(bson_iter_t *projectionSpecIter)
{
	bool skipParseAggregationExpressions = false;
	BsonProjectionQueryState *projectionState = palloc0(sizeof(BsonProjectionQueryState));
	BuildBsonPathTreeForDollarAddFields(projectionState, projectionSpecIter,
										skipParseAggregationExpressions);
	return projectionState;
}


/*
 * bson_dollar_set performs
 *      (1) a projection of all the fields in a binary serialized bson,
 *      (2) evaluates and add new fields (driven by the $set specs) to the projection.
 */
Datum
bson_dollar_set(PG_FUNCTION_ARGS)
{
	/* $set is an alias of $addFields */
	return bson_dollar_add_fields(fcinfo);
}


/*
 * bson_dollar_replace_root performs
 *      (1) evaluates the newRoot Expression provided by the spec and projects the evaluated
 * document as the new document.
 *      (2) It throws an error if the evaluated expression is not a document.
 *
 *   Spec: { $replaceRoot: { newRoot: <replacementDocument> } }
 * ReplaceRoot performs a projection of one or more paths given a source document and a replaceRoot
 * specification as a bson iterator.
 *      sourceDocument => Single bson document
 *      replaceRootSpecIter => Projection specification
 *		forceProjectId => whether _id needs to be projected as well.
 *
 *      example replaceRoot Spec:   Example 1: { 'newRoot' :  { "a" : "$b" } }
 *                                  Example 2: { 'newRoot' :  "$a.b" }
 *                                  Example 3: { 'newRoot' :  { "a" : "$b", [ "$x", {} ], { "c" : "d" } } }
 */
Datum
bson_dollar_replace_root(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *pathSpec = PG_GETARG_PGBSON(1);

	const AggregationExpressionData *replaceRootExpression;
	int argPosition = 1;

	SetCachedFunctionState(
		replaceRootExpression,
		AggregationExpressionData,
		argPosition,
		PopulateReplaceRootExpressionDataFromSpec,
		pathSpec);

	bool forceProjectId = false;
	if (replaceRootExpression == NULL)
	{
		AggregationExpressionData expressionData;
		PopulateReplaceRootExpressionDataFromSpec(&expressionData, pathSpec);
		PG_RETURN_POINTER(ProjectReplaceRootDocument(document, &expressionData,
													 forceProjectId));
	}
	else
	{
		PG_RETURN_POINTER(ProjectReplaceRootDocument(document, replaceRootExpression,
													 forceProjectId));
	}
}


/*
 * Given a replaceRoot expression, and a document,
 * walks the document and returns a new document with the
 * specified sub-document as the new root.
 * see bson_dollar_replace_root for more details.
 *
 */
pgbson *
ProjectReplaceRootDocument(pgbson *document, const
						   AggregationExpressionData *replaceRootExpression, bool
						   forceProjectId)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	bson_iter_t docIterator;
	bson_value_t idValue = { 0 };
	if (forceProjectId && PgbsonInitIteratorAtPath(document, "_id", &docIterator))
	{
		idValue = *bson_iter_value(&docIterator);
		PgbsonWriterAppendValue(&writer, "_id", 3, &idValue);
	}

	pgbson_writer valueWriter;
	bson_iter_t writerIterator;
	pgbsonelement resultElement = { 0 };
	PgbsonWriterInit(&valueWriter);
	bool isNullOnEmpty = false;

	/* TODO VARIABLE: take as argument? */
	ExpressionVariableContext *variableContext = NULL;
	StringView path = { .string = "", .length = 0 };
	EvaluateAggregationExpressionDataToWriter(replaceRootExpression, document, path,
											  &valueWriter, variableContext,
											  isNullOnEmpty);

	PgbsonWriterGetIterator(&valueWriter, &writerIterator);
	if (!TryGetSinglePgbsonElementFromBsonIterator(&writerIterator, &resultElement))
	{
		if (resultElement.bsonValue.value_type == BSON_TYPE_EOD)
		{
			ereport(ERROR, (errcode(MongoLocation40228),
							errmsg(
								"'newRoot' expression must evaluate to an object, but resulting value was: : MISSING. Type of resulting value: 'missing'")));
		}

		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
						errmsg(
							"Writing expression to single valued bson failed to get bson value")));
	}

	if (resultElement.bsonValue.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation40228),
						errmsg(
							"'newRoot' expression must evaluate to an object, but resulting value was: %s. Type of resulting value: '%s'.",
							BsonValueToJsonForLogging(&resultElement.bsonValue),
							BsonTypeName(resultElement.bsonValue.value_type)),
						errhint(
							"'newRoot' expression must evaluate to an object, but the type of resulting value: '%s'.",
							BsonTypeName(resultElement.bsonValue.value_type))));
	}

	if (forceProjectId)
	{
		memset(&writerIterator, 0, sizeof(bson_iter_t));
		bson_iter_init_from_data(&writerIterator,
								 resultElement.bsonValue.value.v_doc.data,
								 resultElement.bsonValue.value.v_doc.data_len);
		if (bson_iter_find(&writerIterator, "_id") &&
			idValue.value_type != BSON_TYPE_EOD &&
			!BsonValueEqualsStrict(&idValue, bson_iter_value(&writerIterator)))
		{
			/* TODO: should this be ignored? */
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							errmsg("_id must not be reset in the child document")));
		}
	}

	PgbsonWriterConcatBytes(&writer, resultElement.bsonValue.value.v_doc.data,
							resultElement.bsonValue.value.v_doc.data_len);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Walks the bson document that specifies the projection specification and
 *
 *  Either, builds a tree with a single node with key "newRoot" and value: "<expression>"
 *  Or,     sets the singleValueExpression to a single value expression (expression without any key)
 */
void
GetBsonValueForReplaceRoot(bson_iter_t *replaceRootIterator, bson_value_t *value)
{
	bool replaceRootFound = false;
	while (bson_iter_next(replaceRootIterator))
	{
		const char *path = bson_iter_key(replaceRootIterator);

		/* Mongo behavior: replaceRoot spec can't have anything other field than "newRoot" */
		if (strcmp(path, "newRoot") != 0)
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							errmsg(
								"BSON fields '$replaceRoot.%s' is an unknown field",
								path)));
		}

		*value = *bson_iter_value(replaceRootIterator);
		replaceRootFound = true;
	}

	if (!replaceRootFound)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"BSON field '$replaceRoot.newRoot' is missing but a required field")));
	}
}


/*
 * Ref: docs/lookup.md
 * Mongo $lookup semantics: For each document from a collection t1, find all documents from a
 * collection t2, where t1.localField = t2.foreignField.
 *
 * We implement this by iterating over all documents in t1, a generating a filter expression,
 * that can pushed down to the index if there is an index on t2.foreginfield.
 *
 * This method performs the function of extracting the filter expression given the spec of the following
 * general form:
 *      '{"a.b.c"  :  "x.y.z"}'
 *
 * The details of the filter expression can be found in BsonLookUpGetFilterExpression(), called from this method.
 *
 */
Datum
bson_dollar_lookup_extract_filter_expression(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filterExpressionSpec = PG_GETARG_PGBSON(1);

	pgbsonelement pgbsonElement;
	PgbsonToSinglePgbsonElement(filterExpressionSpec, &pgbsonElement);

	PG_RETURN_POINTER(BsonLookUpGetFilterExpression(document, &pgbsonElement));
}


/*
 * This is similar to lookup_extract_filter_expression but it returns a bson[] instead of a
 * bson document with the array paths.
 */
Datum
bson_dollar_lookup_extract_filter_array(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filterExpressionSpec = PG_GETARG_PGBSON(1);

	pgbsonelement pgbsonElement;
	PgbsonToSinglePgbsonElement(filterExpressionSpec, &pgbsonElement);

	pgbson *result = BsonLookUpGetFilterExpression(document, &pgbsonElement);
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(result, &element);

	int count = BsonDocumentValueCountKeys(&element.bsonValue);

	Datum *inArray = palloc(sizeof(Datum) * count);

	bson_iter_t arrayIter;
	BsonValueInitIterator(&element.bsonValue, &arrayIter);
	count = 0;
	while (bson_iter_next(&arrayIter))
	{
		inArray[count] = PointerGetDatum(BsonValueToDocumentPgbson(bson_iter_value(
																	   &arrayIter)));
		count++;
	}

	ArrayType *resultVal = construct_array(inArray, count, BsonTypeId(), -1, false,
										   TYPALIGN_INT);
	pfree(result);
	PG_RETURN_ARRAYTYPE_P(resultVal);
}


/*
 * Ref: docs/lookup.md
 * Mongo $lookup semantics: For each document from a collection t1, find all documents from a
 * collection t2, where t1.localField = t2.foreignField.
 *
 * After the matching is done bson_dollar_lookup_project() formats the output according to Mongo spec.
 *
 * Details about the formatting logic is encoded in BsonLookUpProject(), which is called from this function.
 */
Datum
bson_dollar_lookup_project(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	char *matchedDocsFieldName = text_to_cstring(PG_GETARG_TEXT_P(2));

	ArrayType *val_array = PG_GETARG_ARRAYTYPE_P(1);

	Datum *val_datums;
	bool *val_is_null_marker;
	int val_count;

	deconstruct_array(val_array,
					  ARR_ELEMTYPE(val_array), -1, false, TYPALIGN_INT,
					  &val_datums, &val_is_null_marker, &val_count);

	/*
	 *  Datum array is not expected to have null, so we can free up the isnull marker array.
	 *  We could  have used NULL as a param, but passing a real address is the recommended pattern.
	 *  Implementation of deconstruct_array() can be found in postgres repo: src/backend/utils/adt/arrayfuncs.c
	 */
	pfree(val_is_null_marker);

	PG_RETURN_POINTER(BsonLookUpProject(document, val_count, val_datums,
										matchedDocsFieldName));
}


/*
 * This UDF is primarily perform checks on whether the document produced by
 * the facet stage is under 16MB. Note that the check is controlled by the
 * parameter validateDocumentSize. It is set to true, by the gateway when
 * facet is the last stage of an aggregation pipeline. If, facet is not the last
 * stage of the pipeline, we skip the check facet could be followed by an $unwind
 * to yield smaller documents.
 */
Datum
bson_dollar_facet_project(PG_FUNCTION_ARGS)
{
	bool validateDocumentSize = PG_GETARG_BOOL(1);
	pgbson *document = PG_GETARG_PGBSON(0);

	if (validateDocumentSize)
	{
		uint32_t size = PgbsonGetBsonSize(document);
		if (size > BSON_MAX_ALLOWED_SIZE)
		{
			ereport(ERROR, (errcode(MongoBsonObjectTooLarge),
							errmsg("Size %u is larger than MaxDocumentSize %u",
								   size, BSON_MAX_ALLOWED_SIZE)));
		}
	}

	PG_RETURN_POINTER(document);
}


/*
 * bson_dollar_unset performs a projection of one or more paths to unset
 * in a binary serialized bson. THis is equivalent to the $project but with
 * exclude being true.
 */
Datum
bson_dollar_unset(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *pathSpec = PG_GETARG_PGBSON(1);

	pgbsonelement unsetElement;
	if (!TryGetSinglePgbsonElementFromPgbson(pathSpec, &unsetElement))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
							"Unset should be a single element with a value")));
	}

	bool forceProjectId = false;
	int argPosition = 1;
	const BsonProjectionQueryState *state;

	SetCachedFunctionState(
		state,
		BsonProjectionQueryState,
		argPosition,
		BuildBsonPathTreeForDollarUnset,
		&unsetElement.bsonValue,
		forceProjectId);

	if (state == NULL)
	{
		BsonProjectionQueryState projectionState = { 0 };
		BuildBsonPathTreeForDollarUnset(&projectionState, &unsetElement.bsonValue,
										forceProjectId);
		PG_RETURN_POINTER(ProjectDocumentWithState(document, &projectionState));
	}
	else
	{
		PG_RETURN_POINTER(ProjectDocumentWithState(document, state));
	}
}


/*
 * Given an unsetValue specified by the bson_value,
 * builds a BsonProjectionQueryState that can later be used to
 * apply the projectionSpec on documents.
 */
const BsonProjectionQueryState *
GetProjectionStateForBsonUnset(const bson_value_t *unsetValue, bool forceProjectId)
{
	BsonProjectionQueryState *projectionState = palloc0(sizeof(BsonProjectionQueryState));
	BuildBsonPathTreeForDollarUnset(projectionState, unsetValue, forceProjectId);
	return projectionState;
}


/*
 * Given a bson document specified by sourceDocument,
 * applies the transform specified by the BsonProjectionQueryState
 * on the document and produces a new output document.
 */
pgbson *
ProjectDocumentWithState(pgbson *sourceDocument,
						 const BsonProjectionQueryState *state)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_iter_t documentIterator;
	PgbsonInitIterator(sourceDocument, &documentIterator);

	ProjectDocumentState projectDocState = {
		.isPositionalAlreadyEvaluated = false,
		.parentDocument = sourceDocument,
		.variableContext = state->variableContext,
		.hasExclusion = state->hasExclusion,
		.projectDocumentFuncs = state->projectDocumentFuncs,
		.pendingProjectionState = NULL,
	};

	if (projectDocState.projectDocumentFuncs.initializePendingProjectionFunc != NULL)
	{
		projectDocState.pendingProjectionState =
			projectDocState.projectDocumentFuncs.initializePendingProjectionFunc(
				state->endTotalProjections);
	}

	bool isInNestedArray = false;
	TraverseObjectAndAppendToWriter(&documentIterator, state->root, &writer,
									state->projectNonMatchingFields,
									&projectDocState, isInNestedArray);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Tries to inline a left Projection expression with a right Projection expression
 * to create a merged expression if possible.
 */
bool
TryInlineProjection(Node *currentExprNode, Oid functionOid, const
					bson_value_t *projectValue)
{
	/* All projection operators are FuncExprs - skip if it's not a FuncExpr */
	if (!IsA(currentExprNode, FuncExpr))
	{
		return false;
	}

	FuncExpr *currentExpr = (FuncExpr *) currentExprNode;
	if (currentExpr->funcid != BsonDollarProjectFunctionOid() &&
		currentExpr->funcid != BsonDollarAddFieldsFunctionOid())
	{
		return false;
	}

	if (currentExpr->funcid == BsonDollarProjectFunctionOid() &&
		functionOid == BsonDollarAddFieldsFunctionOid())
	{
		MemoryContext tempContext = AllocSetContextCreate(CurrentMemoryContext,
														  "projection context",
														  ALLOCSET_DEFAULT_SIZES);

		BuildBsonPathTreeContext contextCopy = { 0 };
		BsonIntermediatePathNode *root;
		MemoryContext oldContext = MemoryContextSwitchTo(tempContext);

		/* First build a tree from the current: We use this to detect inclusion/exclusion */
		Const *projectConst = lsecond(currentExpr->args);
		pgbson *pathSpec = DatumGetPgBson(projectConst->constvalue);

		bson_iter_t pathSpecIter;
		PgbsonInitIterator(pathSpec, &pathSpecIter);

		/*
		 * See the input for bson_dollar_project this replicates building that tree
		 * This would also do input validation and management for the tree.
		 */
		BsonProjectionContext projectContext = {
			.forceProjectId = false,
			.allowInclusionExclusion = false,
			.pathSpecIter = &pathSpecIter,
			.querySpec = NULL,
		};
		BsonProjectionQueryState projectionState = { 0 };
		BuildBsonPathTreeForDollarProject(&projectionState, &projectContext);

		/* TODO: Handle inclusion projection */
		if (!projectionState.hasExclusion || projectionState.hasInclusion)
		{
			return false;
		}

		/* First step, create a tree of the addFields */
		BsonValueInitIterator(projectValue, &pathSpecIter);
		BuildBsonPathTreeContext addFieldsContext = { 0 };
		addFieldsContext.buildPathTreeFuncs = &DefaultPathTreeFuncs;
		addFieldsContext.skipParseAggregationExpressions = true;

		bool hasFieldsIgnore = false;
		bool forceLeafExpression = true;
		root = BuildBsonPathTree(&pathSpecIter, &addFieldsContext,
								 forceLeafExpression, &hasFieldsIgnore);

		BuildBsonPathTreeContext pathTreeContext = { 0 };
		BuildBsonPathTreeFunctions functions = DefaultPathTreeFuncs;
		functions.postProcessLeafNodeFunc = PostProcessParseProjectNode;
		pathTreeContext.buildPathTreeFuncs = &functions;
		pathTreeContext.allowInclusionExclusion = true;
		pathTreeContext.skipParseAggregationExpressions = true;
		pathTreeContext.pathTreeState = &contextCopy;

		/* Add any new nodes that aren't specified as Exclusions */
		pathTreeContext.skipIfAlreadyExists = true;

		PgbsonInitIterator(pathSpec, &pathSpecIter);
		MergeBsonPathTree(root, &pathSpecIter,
						  &pathTreeContext,
						  forceLeafExpression, &hasFieldsIgnore);
		MemoryContextSwitchTo(oldContext);

		if (contextCopy.hasInclusion)
		{
			MemoryContextDelete(tempContext);
			return false;
		}

		/*
		 * If there's a $project exclusion, if it's followed by an addFields
		 * then we can treat the exclusion as an addFields with each field
		 * being a $$REMOVE. This is because an exclusion projects all of the original
		 * doc except the fields specified. AddFields also projects all of the original
		 * doc and adds the new fields. Consequently an addFields with the exclusion field
		 * being $$REMOVE and the addFields is semantically equivalent.
		 */
		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		bson_iter_t sourceIter;
		pgbson *sourceDoc = PgbsonInitEmpty();
		PgbsonInitIterator(sourceDoc, &sourceIter);
		bool isInNestedArray = false;
		ProjectDocumentState projectDocState = {
			.isPositionalAlreadyEvaluated = false,
			.parentDocument = sourceDoc,
			.hasExclusion = contextCopy.hasExclusion,
			.projectDocumentFuncs = { 0 },
			.pendingProjectionState = NULL
		};

		bool projectNonMatchingFields = true;
		TraverseObjectAndAppendToWriter(&sourceIter, root, &writer,
										projectNonMatchingFields,
										&projectDocState, isInNestedArray);

		pgbson *targetBson = PgbsonWriterGetPgbson(&writer);
		projectConst->constvalue = PointerGetDatum(targetBson);
		currentExpr->funcid = functionOid;

		MemoryContextDelete(tempContext);
		return true;
	}

	/*
	 * TODO: AddFields followed by add fields, in this case, we can concat the 2 together
	 * only if the second one doesn't reference the firstone. Without validating this we can't
	 * inline.
	 * currentExpr->funcid == BsonDollarAddFieldsFunctionOid() && functionOid == BsonDollarAddFieldsFunctionOid()
	 */
	return false;
}


/*
 * Given a projection spec iterator, builds a BsonPathTree
 * for the $project stage of aggregation pipeline
 */
static void
BuildBsonPathTreeForDollarProject(BsonProjectionQueryState *state,
								  BsonProjectionContext *projectionContext)
{
	BuildBsonPathTreeContext context = { 0 };
	context.buildPathTreeFuncs = &DefaultPathTreeFuncs;
	context.allowInclusionExclusion = projectionContext->allowInclusionExclusion;

	BuildBsonPathTreeForDollarProjectCore(state, projectionContext, &context);
}


/*
 * Given a projection spec iterator, builds a BsonPathTree
 * for the purpose of find projection and its operators e.g. $slice, $elemMatch, $(positional)
 */
static void
BuildBsonPathTreeForDollarProjectFind(BsonProjectionQueryState *state,
									  BsonProjectionContext *projectionContext)
{
	BuildBsonPathTreeContext context = { 0 };
	context.pathTreeState = GetPathTreeStateForFind(projectionContext->querySpec);
	context.allowInclusionExclusion = projectionContext->allowInclusionExclusion;

	/* Set the necessary function hooks for find projection */
	context.buildPathTreeFuncs = &FindPathTreeFunctions;

	/* Build the tree */
	BuildBsonPathTreeForDollarProjectCore(state, projectionContext, &context);
	state->endTotalProjections = PostProcessStateForFind(&state->projectDocumentFuncs,
														 &context);
}


/*
 * Given a projection spec iterator, builds a BsonPathTree
 * for the purposes of $project and populates the BsonProjectionQueryState
 * with the necessary information about the tree, and whether the projection
 * had inclusions or exclusions. These will be used in projecting values
 * for document tuples.
 */
static void
BuildBsonPathTreeForDollarProjectCore(BsonProjectionQueryState *state,
									  BsonProjectionContext *projectionContext,
									  BuildBsonPathTreeContext *pathTreeContext)
{
	bool hasFields = false;
	bool forceLeafExpression = false;
	BsonIntermediatePathNode *root = BuildBsonPathTree(projectionContext->pathSpecIter,
													   pathTreeContext,
													   forceLeafExpression, &hasFields);

	/* by default we do path based projections if there's ANY inclusions/exclusions */
	AdjustPathProjectionsForId(root, pathTreeContext->hasInclusion,
							   projectionContext->forceProjectId,
							   &pathTreeContext->hasExclusion);
	state->root = root;
	state->hasInclusion = pathTreeContext->hasInclusion;
	state->hasExclusion = pathTreeContext->hasExclusion;
	state->projectNonMatchingFields = pathTreeContext->hasExclusion;

	/* TODO VARIABLE parse variable spec and set from there */
	state->variableContext = NULL;
}


/*
 * Given a projection spec iterator, builds a BsonPathTree
 * for the purposes of $addFields and populates the BsonProjectionQueryState
 * with the necessary information about the tree, and whether the projection
 * had inclusions or exclusions. These will be used in projecting values
 * for document tuples.
 */
static void
BuildBsonPathTreeForDollarAddFields(BsonProjectionQueryState *state,
									bson_iter_t *projectionSpecIter,
									bool skipParseAggregationExpressions)
{
	BuildBsonPathTreeContext context = { 0 };
	context.buildPathTreeFuncs = &DefaultPathTreeFuncs;
	context.skipParseAggregationExpressions = skipParseAggregationExpressions;

	bool hasFields = false;
	bool forceLeafExpression = true;
	BsonIntermediatePathNode *root = BuildBsonPathTree(projectionSpecIter, &context,
													   forceLeafExpression, &hasFields);

	state->root = root;
	state->hasInclusion = context.hasInclusion;
	state->hasExclusion = context.hasExclusion;
	state->projectNonMatchingFields = true;

	/* TODO VARIABLE take as an argument and parse. */
	state->variableContext = NULL;
}


/*
 * Given a projection spec iterator, builds a BsonPathTree
 * for the purposes of $unset and populates the BsonProjectionQueryState
 * with the necessary information about the tree,
 * These will be used in projecting values for downstream tuples.
 */
static void
BuildBsonPathTreeForDollarUnset(BsonProjectionQueryState *state,
								const bson_value_t *unsetValue,
								bool forceProjectId)
{
	bool hasInclusion = false;
	bool hasExclusion = true;
	BsonIntermediatePathNode *root = BuildBsonUnsetPathTree(unsetValue);


	if (!IntermediateNodeHasChildren(root))
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$unset specification must have at least one field.")));
	}

	/* by default we do path based projections if there's ANY inclusions/exclusions */
	AdjustPathProjectionsForId(root, hasInclusion, forceProjectId, &hasExclusion);

	state->root = root;
	state->hasInclusion = hasInclusion;
	state->hasExclusion = hasExclusion;
	state->projectNonMatchingFields = hasExclusion;
	state->variableContext = NULL;
}


/*
 *
 * This method performs the function of extracting the filter expression given the spec of the following
 * general form:
 *      '{"a.b.c"  :  "x.y.z"}'
 *
 * The key represents the foreignField, and the value represents the localField.
 *
 * Given a document  { "x" : [ { "y" [{ "z" : 1}, {"z" : null }] }, { "y" : [{ "z" : "10"},  {"z" : { "p" : 1} }] } ] }
 *
 * The above filter expression spec will generate the following filter expression:
 *
 *      "a.b.c" : [ 1, null, "10", { "p" : 1} ]
 *
 * If there is no match in the document the filter expression would be:
 *
 *      "a.b.c" : [ null ]
 *
 * Even a single element is wrapped in [], as we used the @*= operator to match the filter expression in t2.
 *
 */
static pgbson *
BsonLookUpGetFilterExpression(pgbson *sourceDocument,
							  pgbsonelement *lookupSpecElement)
{
	const char *foreignField = lookupSpecElement->path;
	bson_value_t localFieldPath = lookupSpecElement->bsonValue;

	if (localFieldPath.value_type != BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$lookup argument 'localField' must be a string, found localField: %s",
							BsonTypeName(localFieldPath.value_type))));
	}

	const char *path = localFieldPath.value.v_utf8.str;
	uint32_t pathLength = localFieldPath.value.v_utf8.len;

	if (pathLength > 0 && path[0] == '$')
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"FieldPath field names may not start with '$'")));
	}

	/* Start the iterator at the provided path */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, foreignField, strlen(foreignField), &arrayWriter);

	bson_iter_t sourceDocumentIterator;
	PgbsonInitIterator(sourceDocument, &sourceDocumentIterator);

	if (!TraverseDocumentAndWriteLookupIndexCondition(&arrayWriter,
													  &sourceDocumentIterator, path,
													  strlen(path)))
	{
		PgbsonArrayWriterWriteNull(&arrayWriter);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Ref: docs/lookup.md
 * Mongo $lookup semantics: For each document from a collection t1, find all documents from a
 * collection t2, where t1.localField = t2.foreignField.
 *
 * After the matching is done bson_dollar_lookup_project() formats the output according to Mongo spec.
 *
 * Input to the method:
 *      1. A document from the t1
 *      2. Number of matched douments from t2
 *      3. An array of matched documents from t2.
 *      4. The "AS" field name for the matched array (say "matched.x").
 *
 * The output will be formatted as:
 *
 *      { <t1.field1>: <t1.value1>, ..... ,<t1.fieldN>: <t1.valueN>, "matched" : { "x" : [<t2.document1>, ..... <t2.documentN>]}
 *
 * Behavoir:
 *      1. If "matched" is an existing field in t1, it will be overwritten by the array.
 *
 *                  t1.doc :   { "a" : { "b" : 1}}
 *                  asFieldname :  "a.c"
 *                  matched array : [{"a" : 1}, { "b" : 1}]
 *                  output:  {"a" : { "b" : 1,  "c" : [{"a" : 1}, { "b" : 1}] }}
 *
 */
static pgbson *
BsonLookUpProject(pgbson *sourceDocument, int numMatched, Datum *matchedDocument,
				  char *matchedDocsFieldName)
{
	/*
	 *  Creating an addField spec of the form
	 *
	 *  "asFieldname"
	 *      matchedArray[0] -> matchedArray[1] -> ..... -> matchedArray[n]
	 *
	 */
	BsonIntermediatePathNode *root = MakeRootNode();
	StringView matchedDocsView = CreateStringViewFromString(matchedDocsFieldName);

	bool treatLeafDataAsConstant = true;
	BsonLeafArrayWithFieldPathNode *matchedDocsNode =
		TraverseDottedPathAndAddLeafArrayNode(
			&matchedDocsView,
			root,
			BsonDefaultCreateIntermediateNode,
			treatLeafDataAsConstant);

	for (int i = 0; i < numMatched; i++)
	{
		bson_value_t documentBsonValue = ConvertPgbsonToBsonValue(
			(pgbson *) matchedDocument[i]);

		const char *relativePath = NULL;
		AddValueNodeToLeafArrayWithField(matchedDocsNode, relativePath, i,
										 &documentBsonValue,
										 BsonDefaultCreateLeafNode,
										 treatLeafDataAsConstant);
	}


	/* Executing the addFieldSpec and returning the pgbson */
	pgbson_writer writer;
	bson_iter_t documentIterator;
	PgbsonWriterInit(&writer);
	PgbsonInitIterator(sourceDocument, &documentIterator);
	bool projectNonMatchingField = true;
	ProjectDocumentState projectDocState = {
		.isPositionalAlreadyEvaluated = false,
		.parentDocument = sourceDocument,
		.pendingProjectionState = NULL
	};

	bool isInNestedArray = false;
	TraverseObjectAndAppendToWriter(&documentIterator, root, &writer,
									projectNonMatchingField,
									&projectDocState, isInNestedArray);
	pfree(matchedDocument);
	return PgbsonWriterGetPgbson(&writer);
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

/*
 * Traverses a document from the left collection and writes the index conditions.
 */
bool
TraverseDocumentAndWriteLookupIndexCondition(pgbson_array_writer *arrayWriter,
											 bson_iter_t *documentIterator,
											 const char *path,
											 int pathLength)
{
	check_stack_depth();
	char *leftSubstring = memchr(path, '.', pathLength);
	bool writtenSomething = false;

	if (leftSubstring == NULL)
	{
		if (bson_iter_find(documentIterator, path))
		{
			if (BSON_ITER_HOLDS_ARRAY(documentIterator))
			{
				/* {"foreignField" : [<ArrayElem0>, <ArrayElem1>}, .. ]} */
				bson_iter_t childIter;
				if (bson_iter_recurse(documentIterator, &childIter))
				{
					while (bson_iter_next(&childIter))
					{
						PgbsonArrayWriterWriteValue(arrayWriter, bson_iter_value(
														&childIter));
						writtenSomething = true;
					}
				}
			}
			else
			{
				PgbsonArrayWriterWriteValue(arrayWriter, bson_iter_value(
												documentIterator));
				writtenSomething = true;
			}
		}
	}
	else
	{
		uint32_t subFieldLength = leftSubstring - path;

		if (bson_iter_find_w_len(documentIterator, path, subFieldLength))
		{
			path = leftSubstring + 1;
			pathLength = pathLength - subFieldLength - 1;

			if (BSON_ITER_HOLDS_DOCUMENT(documentIterator))
			{
				bson_iter_t objectElementInterator;
				bson_iter_recurse(documentIterator, &objectElementInterator);
				writtenSomething = TraverseDocumentAndWriteLookupIndexCondition(
					arrayWriter, &objectElementInterator, path,
					pathLength);
			}
			else if (BSON_ITER_HOLDS_ARRAY(documentIterator))
			{
				bson_iter_t arrayElementInterator;
				if (bson_iter_recurse(documentIterator, &arrayElementInterator))
				{
					/* If the current path ('a.0' or 'a.1.x' or 'a.b.0') matches an array (i.e., path 'a' points to an array) */
					/* we explore the next path segment. If the next path segment is an array index ( '0', or '1' in the first */
					/* two cases), we only traverse the array element pointed by the 'array index path'. Additionally we */
					/* also advance the path (e.g., NULL, 'x', 'b.0' accordingly). If the path is NULL, we print and terminate, */
					/* otherwise we traverse recursively. */
					char *arrayIndexSubstring = memchr(path, '.', pathLength);
					int32_t arrayIndex = -1;

					StringView result =
					{
						.string = path,
						.length = (arrayIndexSubstring == NULL) ? (uint32_t) pathLength :
								  (arrayIndexSubstring - path),
					};

					arrayIndex = StringViewToPositiveInteger(&result);

					if (arrayIndex > -1)
					{
						path = (arrayIndexSubstring == NULL) ? NULL :
							   (arrayIndexSubstring + 1);
						pathLength = (arrayIndexSubstring == NULL) ? 0 : (pathLength -
																		  (
																			  arrayIndexSubstring
																			  -
																			  path) -
																		  1);
					}

					int32_t currentIndex = 0;

					while (bson_iter_next(&arrayElementInterator))
					{
						/* If path has array index, only traverse the matching array elements */
						if (arrayIndex > -1 && currentIndex++ != arrayIndex)
						{
							continue;
						}

						if (pathLength > 0 && (BSON_ITER_HOLDS_DOCUMENT(
												   &arrayElementInterator) ||
											   BSON_ITER_HOLDS_ARRAY(
												   &arrayElementInterator)))
						{
							bson_iter_t objectElementInterator;
							bson_iter_recurse(&arrayElementInterator,
											  &objectElementInterator);
							writtenSomething =
								TraverseDocumentAndWriteLookupIndexCondition(
									arrayWriter,
									&objectElementInterator,
									path,
									pathLength) || writtenSomething;
						}
						else if (pathLength == 0)
						{
							PgbsonArrayWriterWriteValue(arrayWriter, bson_iter_value(
															&arrayElementInterator));
							writtenSomething = true;
						}

						/* Already matched an array index, break to avoid iterating rest of the elements. */
						if (arrayIndex > -1)
						{
							break;
						}
					}
				}
			}
		}
	}

	return writtenSomething;
}


/*
 * Walks the bson document that specifies the unset specification and builds a tree
 * with the projection data.
 * e.g. "a.b.c" and "a.d.e" will produce a -> b,d; b->c; d->e
 * This will allow us to walk the documents later and produce a single projection spec.
 */
static BsonIntermediatePathNode *
BuildBsonUnsetPathTree(const bson_value_t *pathSpecification)
{
	bson_value_t excludeValue;
	excludeValue.value_type = BSON_TYPE_INT32;
	excludeValue.value.v_int32 = 0;
	bool treatLeafDataAsConstant = true;

	BsonIntermediatePathNode *tree = MakeRootNode();
	if (pathSpecification->value_type == BSON_TYPE_UTF8)
	{
		StringView path = {
			.string = pathSpecification->value.v_utf8.str, .length =
				pathSpecification->value.v_utf8.len
		};
		TraverseDottedPathAndAddLeafValueNode(&path,
											  &excludeValue,
											  tree,
											  BsonDefaultCreateLeafNode,
											  BsonDefaultCreateIntermediateNode,
											  treatLeafDataAsConstant);
	}
	else if (pathSpecification->value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		bson_iter_init_from_data(&arrayIter, pathSpecification->value.v_doc.data,
								 pathSpecification->value.v_doc.data_len);
		while (bson_iter_next(&arrayIter))
		{
			const bson_value_t *arrayValue = bson_iter_value(&arrayIter);
			if (arrayValue->value_type == BSON_TYPE_UTF8)
			{
				StringView path = {
					.string = arrayValue->value.v_utf8.str, .length =
						arrayValue->value.v_utf8.len
				};
				TraverseDottedPathAndAddLeafValueNode(&path,
													  &excludeValue,
													  tree,
													  BsonDefaultCreateLeafNode,
													  BsonDefaultCreateIntermediateNode,
													  treatLeafDataAsConstant);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg(
									"$unset specification must be a string or an array containing only string values")));
			}
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("$unset specification must be a string or an array")));
	}

	return tree;
}


/*
 * Given a set of projections, adjusts the projections to handle the behavior of _id.
 * If no _id was specified, then adds an _id projection (default behavior)
 * If an _id was specified, then maps the exclusion behavior based on the projection.
 *
 *  hasIdExclusion => If _id is excluded and there are no other inclusions.
 */
static void
AdjustPathProjectionsForId(BsonIntermediatePathNode *tree,
						   bool hasInclusion, bool forceProjectId, bool *hasIdExclusion)
{
	const BsonPathNode *idField = NULL;
	const BsonPathNode *topLevelField;
	foreach_child(topLevelField, tree)
	{
		/* Find the _id: _id is always projected unless explicitly excluded - handled below. */
		if (StringViewEquals(&topLevelField->field, &IdFieldStringView))
		{
			idField = topLevelField;
			break;
		}
	}

	bson_value_t includedValue = { 0 };
	includedValue.value.v_int32 = 1;
	includedValue.value_type = BSON_TYPE_INT32;
	bool treatLeafDataAsConstant = true;
	if (idField == NULL)
	{
		TraverseDottedPathAndAddLeafValueNode(
			&IdFieldStringView, &includedValue, tree,
			BsonDefaultCreateLeafNode, BsonDefaultCreateIntermediateNode,
			treatLeafDataAsConstant);
	}
	else
	{
		/* if { _id: 0 } and no field projections exist. */
		/* in this case, this means project the rest of the document */
		/* if it has { _id: 0 } and any field projections, the document is not projected at all. */
		/* Note we don't set hasExclusion if there were any other inclusions already specified. */
		if (!hasInclusion && idField->nodeType == NodeType_LeafExcluded)
		{
			*hasIdExclusion = true;
		}

		if (idField->nodeType == NodeType_LeafExcluded && forceProjectId)
		{
			ResetNodeWithValue(CastAsLeafNode(idField), IdFieldStringView.string,
							   &includedValue, BsonDefaultCreateLeafNode,
							   treatLeafDataAsConstant);
		}
	}
}


/*
 * Takes the current field in an iterator for a document and tries to match a
 * specific projection Path. If the filter path is an exact match, then adds it to the result
 * projection. If the filter is a prefix-match (field match in a dot-traversal)
 * recurses and finds the remaining match and appends the projection.
 */
static void
ProjectCurrentIteratorFieldToWriter(bson_iter_t *documentIterator,
									const BsonIntermediatePathNode *pathSpecTree,
									pgbson_writer *writer,
									bool projectNonMatchingFields,
									Bitmapset **fieldHandledBitmapSet,
									ProjectDocumentState *projectDocState,
									bool isInNestedArray)
{
	StringView path = bson_iter_key_string_view(documentIterator);

	const BsonPathNode *child;
	int index = 0;
	bool isNullOnEmpty = false;
	ExpressionVariableContext *variableContext = NULL;
	foreach_child(child, pathSpecTree)
	{
		/* check if a field matches against a filter. */
		if (!StringViewEquals(&child->field, &path))
		{
			index++;
			continue;
		}

		/* field is a match. */
		/* field is a perfect match - add it. */
		switch (child->nodeType)
		{
			case NodeType_LeafExcluded:
			{
				return;
			}

			case NodeType_LeafIncluded:
			{
				PgbsonWriterAppendValue(writer, path.string, path.length, bson_iter_value(
											documentIterator));
				return;
			}

			case NodeType_LeafFieldWithContext:
			{
				const BsonLeafNodeWithContext *leafNode = CastAsBsonLeafNodeWithContext(
					child);
				ProjectionOpHandlerContext *context =
					(ProjectionOpHandlerContext *) leafNode->context;
				if (context != NULL)
				{
					context->projectionOpHandlerFunc(bson_iter_value(documentIterator),
													 &path,
													 writer,
													 projectDocState,
													 context->state,
													 isInNestedArray);
				}
				else
				{
					/* Treat as LeafField */
					StringView pathInner = {
						.string = child->field.string,
						.length = child->field.length
					};

					EvaluateAggregationExpressionDataToWriter(&leafNode->base.fieldData,
															  projectDocState->
															  parentDocument, pathInner,
															  writer, variableContext,
															  isNullOnEmpty);
				}

				*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet, index);
				return;
			}

			case NodeType_LeafField:
			{
				StringView pathInner = {
					.string = child->field.string,
					.length = child->field.length
				};

				const BsonLeafPathNode *leafNode = CastAsLeafNode(child);
				EvaluateAggregationExpressionDataToWriter(&leafNode->fieldData,
														  projectDocState->parentDocument,
														  pathInner, writer,
														  variableContext,
														  isNullOnEmpty);
				*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet, index);
				return;
			}

			case NodeType_LeafWithArrayField:
			{
				WriteLeafArrayFieldToWriter(writer, child,
											projectDocState->parentDocument,
											variableContext);
				*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet, index);
				return;
			}

			case NodeType_Intermediate:
			{
				const BsonIntermediatePathNode *intermediateNode =
					CastAsIntermediateNode(child);

				bool isHandled = false;
				if (BSON_ITER_HOLDS_DOCUMENT(documentIterator))
				{
					pgbson_writer childWriter;
					bson_iter_t childIter;

					PgbsonWriterStartDocument(writer, path.string, path.length,
											  &childWriter);
					if (bson_iter_recurse(documentIterator, &childIter))
					{
						TraverseObjectAndAppendToWriter(&childIter,
														intermediateNode,
														&childWriter,
														projectNonMatchingFields,
														projectDocState,
														isInNestedArray);
					}
					PgbsonWriterEndDocument(writer, &childWriter);
					isHandled = true;
					if (IsIntermediateNodeWithField(child))
					{
						*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet,
																index);
					}
				}
				else if (BSON_ITER_HOLDS_ARRAY(documentIterator))
				{
					pgbson_array_writer childWriter;
					PgbsonWriterStartArray(writer, path.string, path.length,
										   &childWriter);
					TraverseArrayAndAppendToWriter(documentIterator,
												   &childWriter,
												   intermediateNode,
												   projectNonMatchingFields,
												   projectDocState,
												   isInNestedArray);
					PgbsonWriterEndArray(writer, &childWriter);

					isHandled = true;
				}

				if (isHandled)
				{
					if (IsIntermediateNodeWithField(child))
					{
						*fieldHandledBitmapSet = bms_add_member(*fieldHandledBitmapSet,
																index);
					}
					return;
				}
				else if (IsIntermediateNodeWithField(child))
				{
					return;
				}

				break;
			}

			default:
			{
				ereport(ERROR, (errmsg("Unexpected node type %d",
									   child->nodeType)));
			}
		}
	}

	/*
	 *  if we reached here - none of the paths matched - add it to the result. Relevant when:
	 *      a. $project has exclusions on a path
	 *      b. $addFields
	 */
	if (projectNonMatchingFields)
	{
		PgbsonWriterAppendValue(writer, path.string, path.length, bson_iter_value(
									documentIterator));
	}
}


/*
 * Walks all fields in an object in the current iterator and validates if any paths match the
 * specified path tree specified.
 * If there are unresolved fields pending after matching, then creates new nodes in the writer.
 */
void
TraverseObjectAndAppendToWriter(bson_iter_t *iterator,
								const BsonIntermediatePathNode *pathSpecTree,
								pgbson_writer *writer,
								bool projectNonMatchingFields,
								ProjectDocumentState *projectDocState,
								bool isInNestedArray)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	/* determine the number of bits for a bitmask on the children. */
	Bitmapset *fieldHandledBitmapSet = NULL;
	while (bson_iter_next(iterator))
	{
		/* for documents, if a nested field matches, then project it in and move forward. */
		ProjectCurrentIteratorFieldToWriter(iterator, pathSpecTree, writer,
											projectNonMatchingFields,
											&fieldHandledBitmapSet,
											projectDocState,
											isInNestedArray);
	}

	/* add any unresolved field nodes that needed to be added. */
	HandleUnresolvedFields(pathSpecTree, fieldHandledBitmapSet, writer,
						   projectDocState->parentDocument,
						   projectDocState->variableContext);

	if (projectDocState->pendingProjectionState != NULL &&
		projectDocState->projectDocumentFuncs.writePendingProjectionFunc != NULL)
	{
		/* Write pending projections at the end */
		projectDocState->projectDocumentFuncs.writePendingProjectionFunc(writer,
																		 projectDocState->
																		 pendingProjectionState);
	}
}


/*
 * Walks all fields in an array, and projects any inner objects based off the path node specification.
 * Note that arrays do not participate in projections directly (so 'a.1' do not traverse into the 1st index of
 * the array, but instead try to find an object with a field '1' within the array)
 */
static void
TraverseArrayAndAppendToWriter(bson_iter_t *parentIterator,
							   pgbson_array_writer *arrayWriter,
							   const BsonIntermediatePathNode *pathNode,
							   bool projectNonMatchingFields,
							   ProjectDocumentState *projectDocState,
							   bool isInNestedArray)
{
	bson_iter_t childIter;
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	if (bson_iter_recurse(parentIterator, &childIter))
	{
		if (projectDocState->projectDocumentFuncs.tryMoveArrayIteratorFunc != NULL &&
			projectDocState->projectDocumentFuncs.tryMoveArrayIteratorFunc(pathNode,
																		   projectDocState,
																		   &childIter))
		{
			ProjectCurrentArrayIterToWriter(&childIter, arrayWriter,
											pathNode, projectNonMatchingFields,
											projectDocState, isInNestedArray);
			return;
		}

		/* for arrays, we walk every object and insert it. if there are matches they are written in. */
		/* Note: for arrays, array indexes are not considered in the match. */
		/* so a.0 does NOT match the 0th index of an array - instead it matches a field in a nested object */
		/* with the path "0". */
		while (bson_iter_next(&childIter))
		{
			ProjectCurrentArrayIterToWriter(&childIter, arrayWriter,
											pathNode, projectNonMatchingFields,
											projectDocState, isInNestedArray);
		}
	}
}


/*
 * Writes the current array element recursively into the writer for projection.
 * Includes only the dotted path element if projectNonMatching field is false
 */
static void
ProjectCurrentArrayIterToWriter(bson_iter_t *arrayIter,
								pgbson_array_writer *arrayWriter,
								const BsonIntermediatePathNode *pathNode,
								bool projectNonMatchingFields,
								ProjectDocumentState *projectDocState,
								bool isInNestedArray)
{
	/*
	 *  If the array element in a document
	 *
	 *  Document: { "a": [ 1, {"d": 1}, [ {"c": {"b": 1}} ,2], [1,2]] }
	 *  addFields :  { "a" : { "c" :  { "d" : 1}} }
	 *  Result: { "a": [
	 *              {"c": {"d": "1"}},
	 *              { "c": { "d": "1"},"d": 1},
	 *              [{"c": {"b": 1,"d": "1"}}, {"c": {"d": "1"}}],
	 *              [{"c": {"d": "1"}}, {"c": {"d": "1"}}]
	 *          ] }
	 */

	/*
	 * Mongo projection behavior (for operators like, project, addFields)
	 *
	 *  document (D): { b: [1, 2, 3] }
	 *      path Node:  b.c = 1
	 *          Requirement: For each element of b in D, we have to write { c : 1 }.
	 *      path Node:  b.c = 1, b.d = 1
	 *          Requirement: For each element of b in D, we have to write { {c : 1}, {d : 1} }.
	 *      path Node:  b.c.d = 1, b.e = 1
	 *          Requirement: For each element of b in D, we have to write { { c : { d : 1 } }, {e: 1} }
	 */
	if (BSON_ITER_HOLDS_DOCUMENT(arrayIter))
	{
		pgbson_writer childObjWriter;
		bson_iter_t childObjIter;
		PgbsonArrayWriterStartDocument(arrayWriter,
									   &childObjWriter);
		if (bson_iter_recurse(arrayIter, &childObjIter))
		{
			TraverseObjectAndAppendToWriter(&childObjIter,
											pathNode,
											&childObjWriter,
											projectNonMatchingFields,
											projectDocState,
											isInNestedArray);
		}

		PgbsonArrayWriterEndDocument(arrayWriter,
									 &childObjWriter);
	}
	else if (BSON_ITER_HOLDS_ARRAY(arrayIter))
	{
		pgbson_array_writer childArrayWriter;
		bool inNestedArrayInner = true;

		PgbsonArrayWriterStartArray(arrayWriter,
									&childArrayWriter);
		TraverseArrayAndAppendToWriter(arrayIter,
									   &childArrayWriter,
									   pathNode,
									   projectNonMatchingFields,
									   projectDocState,
									   inNestedArrayInner);
		PgbsonArrayWriterEndArray(arrayWriter,
								  &childArrayWriter);
	}
	else if (IsIntermediateNodeWithField(&pathNode->baseNode) &&
			 IntermediateNodeHasChildren(pathNode))
	{
		pgbson_writer childObjWriter;
		PgbsonArrayWriterStartDocument(arrayWriter, &childObjWriter);
		HandleUnresolvedFields(pathNode, NULL, &childObjWriter,
							   projectDocState->parentDocument,
							   projectDocState->variableContext);
		PgbsonArrayWriterEndDocument(arrayWriter, &childObjWriter);
	}
	/*
	 *  If if current object is non-array, non-document, and non-field we ignore it, unless
	 *  projectNonMatchingFields = true, which indicates if the corresponding field
	 *  needs to be projected.
	 */
	else if (projectNonMatchingFields)
	{
		const bson_value_t *value = bson_iter_value(arrayIter);
		PgbsonArrayWriterWriteValue(arrayWriter, value);
	}
}


/*
 * Updates the writer with any fields that couldn't be resolved for a given projection
 * This basically creates paths for nodes that were not handled in the existing object
 * This covers new fields (e.g if the original document had path { "a": { "b" : 1 }}
 * and the projection was for a.c: ["1"], then this is a 'new addition' of a field
 * when visiting the children of 'a'.)
 * This also covers new trees of fields (e.g. if the original document had the document above
 * and the projection is "a.b.c.d": [ "1"] this will create the object tree of "c": { "d": [ "1"] })
 */
static void
HandleUnresolvedFields(const BsonIntermediatePathNode *parentNode,
					   Bitmapset *fieldBitMapSet,
					   pgbson_writer *writer, pgbson *parentDocument, const
					   ExpressionVariableContext *variableContext)
{
	WriteTreeContext context =
	{
		.state = fieldBitMapSet,
		.filterNodeFunc = FilterNodeToWrite,
		.isNullOnEmpty = false,
	};

	TraverseTreeAndWriteFieldsToWriter(parentNode, writer, parentDocument, &context,
									   variableContext);
}


/* Hook function that uses the bitmapset in the current state
 * to determine if the node should be written or skipped. */
static bool
FilterNodeToWrite(void *state, int currentIndex)
{
	Bitmapset *bitmapSet = (Bitmapset *) state;
	return bms_is_member(currentIndex, bitmapSet);
}


/* Populates the aggregation expression data for a replace root stage based on the pathSpec specified to $replaceRoot. */
static void
PopulateReplaceRootExpressionDataFromSpec(AggregationExpressionData *expressionData,
										  pgbson *pathSpec)
{
	bson_iter_t pathSpecIter;
	PgbsonInitIterator(pathSpec, &pathSpecIter);

	bson_value_t bsonValue;
	GetBsonValueForReplaceRoot(&pathSpecIter, &bsonValue);

	ParseAggregationExpressionData(expressionData, &bsonValue);
}


/*
 * PostProcess with a project node during the inlining of projection
 * stages during planning.
 * Currently, this tracks exclusions that are of the type value == 0
 * and makes them fields with the variable $$REMOVE
 */
static void
PostProcessParseProjectNode(void *state, const StringView *path,
							BsonPathNode *node, bool *isExclusionIfNoInclusion,
							bool *hasFieldsForIntermediate)
{
	DefaultPathTreeFuncs.postProcessLeafNodeFunc(state, path, node,
												 isExclusionIfNoInclusion,
												 hasFieldsForIntermediate);

	BuildBsonPathTreeContext *context = (BuildBsonPathTreeContext *) state;
	if (NodeType_IsLeaf(node->nodeType) &&
		!StringViewEquals(&node->field, &IdFieldStringView))
	{
		BsonLeafPathNode *leafNode = (BsonLeafPathNode *) node;
		if (leafNode->fieldData.kind == AggregationExpressionKind_Constant &&
			BsonValueIsNumber(&leafNode->fieldData.value))
		{
			bool included = BsonValueAsDouble(&leafNode->fieldData.value) != 0;
			if (!included)
			{
				context->hasExclusion = true;

				/* Path: 0 is the same as $$REMOVE */
				leafNode->fieldData.value.value_type = BSON_TYPE_UTF8;
				leafNode->fieldData.value.value.v_utf8.str = "$$REMOVE";
				leafNode->fieldData.value.value.v_utf8.len = 8;
				return;
			}
		}
	}

	/* Presume inclusion otherwise */
	context->hasInclusion = true;
}


/*
 * Projects as per the rules of $geoNear aggregation stage from the cached state
 */
static pgbson *
ProjectGeonearDocument(const GeonearDistanceState *state, pgbson *document)
{
	/*
	 * Get the distance from document, either spherical or planar based on the state
	 * convert it into radians if spherical distance is returned for legacy points
	 */
	float8 distance = GeonearDistanceFromDocument(state, document);

	if (state->mode == DistanceMode_Radians)
	{
		distance = float8_div(distance, RADIUS_OF_EARTH_M);
	}

	distance = float8_mul(distance, state->distanceMultiplier);

	bson_value_t distanceValue = {
		.value.v_double = distance,
		.value_type = BSON_TYPE_DOUBLE,
	};

	BsonIntermediatePathNode *root = MakeRootNode();
	bool nodeCreated = false;
	bool treatLeafDataAsConstant = true;

	/*
	 * Add location to the document first if distance and loc fields are same then loc field gets the priority
	 * aggregation/sources/geonear/distancefield_and_includelocs.js
	 */
	if (state->includeLocs.length > 0 && state->includeLocs.string != NULL)
	{
		bson_iter_t iter;
		PgbsonInitIteratorAtPath(document, state->key.string, &iter);

		TraverseDottedPathAndGetOrAddField(
			&state->includeLocs,
			bson_iter_value(&iter),
			root,
			BsonDefaultCreateIntermediateNode,
			BsonDefaultCreateLeafNode,
			treatLeafDataAsConstant,
			NULL,
			&nodeCreated
			);
	}

	/* Add distance field */
	TraverseDottedPathAndGetOrAddField(
		&state->distanceField,
		&distanceValue,
		root,
		BsonDefaultCreateIntermediateNode,
		BsonDefaultCreateLeafNode,
		treatLeafDataAsConstant,
		NULL,
		&nodeCreated
		);

	/* Write the new fields to the document */
	pgbson_writer writer;
	bson_iter_t documentIterator;
	PgbsonWriterInit(&writer);
	PgbsonInitIterator(document, &documentIterator);
	bool projectNonMatchingField = true;
	ProjectDocumentState projectDocState = {
		.isPositionalAlreadyEvaluated = false,
		.parentDocument = document,
		.pendingProjectionState = NULL
	};

	bool isInNestedArray = false;
	TraverseObjectAndAppendToWriter(&documentIterator, root, &writer,
									projectNonMatchingField,
									&projectDocState, isInNestedArray);
	return PgbsonWriterGetPgbson(&writer);
}
