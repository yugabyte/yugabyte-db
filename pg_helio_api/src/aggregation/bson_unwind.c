/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_unwind.c
 *
 * Implementation of the $unwind operator.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "aggregation/bson_project.h"
#include "aggregation/bson_projection_tree.h"
#include "io/bson_set_returning_functions.h"
#include "io/bson_traversal.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

/* Traversal state for distinct passed into TraverseBson
 * Holds the tuplestore and descriptor that will be used to dump
 * the tuples from the current document.
 */
typedef struct DistinctTraverseState
{
	Tuplestorestate *tupleStore;
	TupleDesc tupleDescriptor;
} DistinctTraverseState;


static pgbson * BsonUnwindElement(pgbson *document, char *path, char *indexFieldName,
								  long index, const bson_value_t *element);
static pgbson * BsonUnwindEmptyArray(pgbson *document, char *path, char *indexFieldName);
static Datum BsonUnwindArray(PG_FUNCTION_ARGS, Tuplestorestate *tupleState,
							 TupleDesc *tupleDescriptor,
							 char *path, char *indexFieldName, bool
							 preserveNullAndEmpty);
static bool DistinctContinueProcessIntermediateArray(void *state, const
													 bson_value_t *value);
static void DistinctSetTraverseResult(void *state, TraverseBsonResult result);
static bool DistinctVisitArrayField(pgbsonelement *element, const
									StringView *traversePath, int
									arrayIndex, void *state);
static bool DistinctVisitTopLevelField(pgbsonelement *element, const
									   StringView *traversePath, void *state);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_dollar_unwind);
PG_FUNCTION_INFO_V1(bson_dollar_unwind_with_options);
PG_FUNCTION_INFO_V1(bson_distinct_unwind);
PG_FUNCTION_INFO_V1(bson_lookup_unwind);

/*
 * bson_dollar_unwind_with_options takes:
 * 1) a bson document
 * 2) A bson document of the form:
 *      { $unwind: {
 *          path: "$a.b",
 *          [optional] preserveNullAndEmptyArrays: bool,
 *          [optional] includeArrayIndex: string
 *      }}
 */
Datum
bson_dollar_unwind_with_options(PG_FUNCTION_ARGS)
{
	pgbson *spec = PG_GETARG_PGBSON_PACKED(1);

	char *path = NULL;
	bool preserveNullAndEmpty = false;
	char *indexFieldName = NULL;

	bson_iter_t specIter;
	PgbsonInitIterator(spec, &specIter);
	while (bson_iter_next(&specIter))
	{
		if (strcmp(bson_iter_key(&specIter), "path") == 0)
		{
			const bson_value_t *pathValue = bson_iter_value(&specIter);
			if (pathValue->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
									"$unwind path must be a text value")));
			}

			path = pathValue->value.v_utf8.str;
		}
		else if (strcmp(bson_iter_key(&specIter), "preserveNullAndEmptyArrays") == 0)
		{
			const bson_value_t *preserveNullAndEmptyValue = bson_iter_value(&specIter);
			if (preserveNullAndEmptyValue->value_type != BSON_TYPE_BOOL)
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
									"$unwind preserveNullAndEmptyArrays must be a bool value")));
			}
			preserveNullAndEmpty = preserveNullAndEmptyValue->value.v_bool;
		}
		else if (strcmp(bson_iter_key(&specIter), "includeArrayIndex") == 0)
		{
			const bson_value_t *arrayIndex = bson_iter_value(&specIter);
			if (arrayIndex->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
									"$unwind includeArrayIndex must be a text value")));
			}
			indexFieldName = arrayIndex->value.v_utf8.str;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
								"unrecognized option to unwind stage")));
		}
	}

	if (path == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
							"$unwind requires a path")));
	}

	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	return BsonUnwindArray(fcinfo, tupleStore, &descriptor, path, indexFieldName,
						   preserveNullAndEmpty);
}


/*
 * bson_dollar_unwind takes:
 * 1) a bson document
 * 2) a dot notation field path to the purported array
 * 3) an optional text for an index field in the output
 * 4) a boolean indicating whether to preserve null and empty arrays
 */
Datum
bson_dollar_unwind(PG_FUNCTION_ARGS)
{
	char *indexFieldName = NULL;
	bool preserveNullAndEmpty = false;

	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	return BsonUnwindArray(fcinfo, tupleStore, &descriptor, text_to_cstring(
							   PG_GETARG_TEXT_PP(1)), indexFieldName,
						   preserveNullAndEmpty);
}


/*
 * Implements the unwind function for Distinct. Walks the document for the given
 * dotted path, and for every element that matches the path, adds it to the tuple store
 * for instance, given the document { "a": [ { "b": [ 1, 2, 3, 4] }, { "b": [ 2, 4 ] }, { "b": 1 }]} for the path
 * "a.b", will produce a tuple store with the elements [ 1, 2, 3, 4, 2, 4, 1 ].
 * This will then be passed through DISTINCT to reduce the array set.
 */
Datum
bson_distinct_unwind(PG_FUNCTION_ARGS)
{
	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);
	pgbson *document = PG_GETARG_PGBSON(0);
	char *path = text_to_cstring(PG_GETARG_TEXT_P(1));

	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);
	TraverseBsonExecutionFuncs distinctExecutionFuncs =
	{
		.ContinueProcessIntermediateArray = DistinctContinueProcessIntermediateArray,
		.SetTraverseResult = DistinctSetTraverseResult,
		.VisitArrayField = DistinctVisitArrayField,
		.VisitTopLevelField = DistinctVisitTopLevelField,
		.SetIntermediateArrayIndex = NULL,
	};

	DistinctTraverseState traverseState =
	{
		.tupleDescriptor = descriptor,
		.tupleStore = tupleStore
	};

	TraverseBson(&documentIterator, path, &traverseState, &distinctExecutionFuncs);

	PG_RETURN_VOID();
}


/*
 * Implements the unwind function for lookup. Walks the (bson_array_agg) result document for the given
 * path, and for every element adds it to the tuplestore.
 * e.g. { "result": [ { "_id": 1}, { "_id": 2 } ]}
 * returns
 * { "_id": 1}, { "_id": 2 }
 */
Datum
bson_lookup_unwind(PG_FUNCTION_ARGS)
{
	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);
	pgbson *document = PG_GETARG_PGBSON(0);
	char *path = text_to_cstring(PG_GETARG_TEXT_P(1));

	bson_iter_t documentIterator;
	if (PgbsonInitIteratorAtPath(document, path, &documentIterator))
	{
		bson_iter_t arrayIter;
		if (!BSON_ITER_HOLDS_ARRAY(&documentIterator) ||
			!bson_iter_recurse(&documentIterator, &arrayIter))
		{
			ereport(ERROR, (errmsg("Lookup unwind expecting field to contain an array")));
		}

		while (bson_iter_next(&arrayIter))
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&arrayIter))
			{
				ereport(ERROR, (errmsg(
									"Lookup unwind array expecting entries to contain documents")));
			}

			Datum values[1];
			bool nulls[1];

			values[0] = PointerGetDatum(PgbsonInitFromDocumentBsonValue(bson_iter_value(
																			&arrayIter)));
			nulls[0] = false;
			tuplestore_putvalues(tupleStore, descriptor, values, nulls);
		}
	}

	PG_RETURN_VOID();
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

/*
 * BsonUnwindArray is the internal implementation of $unwind as a set returning function
 *      path -> The path to be unwound
 *      indexFieldName -> optional string to add the index in the output document
 *      preserveNullAndEmpty -> whether to keep null and empty unwind values
 *
 *  PG_FUNCTION_ARGS contains the document
 */
static Datum
BsonUnwindArray(PG_FUNCTION_ARGS, Tuplestorestate *tupleStore, TupleDesc *tupleDescriptor,
				char *path, char *indexFieldName, bool
				preserveNullAndEmpty)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);

	/* Strip the $ prefix from the path */
	if (strlen(path) <= 1)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
							"$unwind path should have at least two characters")));
	}

	if (path[0] != '$')
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg(
							"$unwind path must be prefixed by $")));
	}
	path = path + 1;

	/* Start the iterator at the provided path */
	bson_iter_t documentIterator;
	if (!PgbsonInitIteratorAtPath(document, path, &documentIterator))
	{
		/* No field was found, mongo returns no results on this document */
		if (preserveNullAndEmpty)
		{
			/* undefined elements are preserved */
			bson_value_t element;
			element.value_type = BSON_TYPE_EOD;
			Datum values[1];
			bool isNull = false;

			values[0] = PointerGetDatum(BsonUnwindElement(document,
														  path,
														  indexFieldName,
														  -1,
														  &element));
			tuplestore_putvalues(tupleStore, *tupleDescriptor, values, &isNull);
		}

		PG_FREE_IF_COPY(document, 0);
		PG_RETURN_VOID();
	}

	if (!BSON_ITER_HOLDS_ARRAY(&documentIterator))
	{
		if (!BSON_ITER_HOLDS_NULL(&documentIterator))
		{
			/* Single non-null elements are always preserved */
			Datum values[1];
			bool nulls[1];

			if (indexFieldName == NULL)
			{
				/* This is just the source doc */
				values[0] = PG_GETARG_DATUM(0);
			}
			else
			{
				const bson_value_t *element = bson_iter_value(&documentIterator);
				values[0] = PointerGetDatum(BsonUnwindElement(document,
															  path,
															  indexFieldName,
															  -1,
															  element));
			}

			nulls[0] = false;

			tuplestore_putvalues(tupleStore, *tupleDescriptor, values, nulls);
		}
		else if (preserveNullAndEmpty)
		{
			/* Nulls are persisted if the document is preserved in the output */
			bson_value_t element;
			element.value_type = BSON_TYPE_NULL;
			Datum values[1];
			bool isNull = false;

			values[0] = PointerGetDatum(BsonUnwindElement(document,
														  path,
														  indexFieldName,
														  -1,
														  &element));
			tuplestore_putvalues(tupleStore, *tupleDescriptor, values, &isNull);
		}

		PG_FREE_IF_COPY(document, 0);
		PG_RETURN_VOID();
	}

	bson_iter_t childIterator;

	/* If the target path is an array, recurse into it */
	bson_iter_recurse(&documentIterator, &childIterator);

	long index = 0;
	while (bson_iter_next(&childIterator))
	{
		/* Project normal array elements and single non-null elements */
		const bson_value_t *element = bson_iter_value(&childIterator);

		pgbson *result = BsonUnwindElement(document, path, indexFieldName, index,
										   element);

		Datum values[1];
		bool nulls[1];

		values[0] = PointerGetDatum(result);
		nulls[0] = false;
		tuplestore_putvalues(tupleStore, *tupleDescriptor, values, nulls);

		index++;
	}

	if (index == 0 && preserveNullAndEmpty)
	{
		Datum values[1];
		bool isNull = false;

		/* Empty arrays are removed if the document is preserved in the output */
		values[0] = PointerGetDatum(BsonUnwindEmptyArray(
										document, path,
										indexFieldName));
		tuplestore_putvalues(tupleStore, *tupleDescriptor, values, &isNull);
	}

	PG_FREE_IF_COPY(document, 0);
	PG_RETURN_VOID();
}


/*
 * BsonUnwindElement produces the output document when element
 * at the unwind target
 *    document -> source document
 *    path -> path being unwound
 *    indexFieldName -> optional name for the index field to be added
 *    element -> the value found at the unwind target
 */
static pgbson *
BsonUnwindElement(pgbson *document, char *path, char *indexFieldName, long index, const
				  bson_value_t *element)
{
	/*
	 *  Document:   {  "a" :  [ 1,  [1,2], { "c": "value"}, "x"] }
	 *  Unwind Spec: { "$unwind" : "a" }
	 *
	 *  Expected Result: {  "a" :  1}
	 *                   {  "a" :  [1,2] }
	 *                   {  "a" :  { "c": "value"} }
	 *                   {  "a" :  "x" }
	 *
	 *  This is achieved by performing AddFields() 4 times on the original source document
	 *  using the following 4 AddFields spec. Basically, we replace the array path with the
	 *  elements of the array.
	 *      1. {"addFields" : { "a" : 1}}
	 *      2. {"addFields" : { "a" : [1,2] }}
	 *      3. {"addFields" : { "a" : { "c": "value"} }}
	 *      4. {"addFields" : { "a" : "x" }}
	 *
	 *  We also, instruct the addFields spec to treat the elemnts to be the final value without
	 *  any need for recursive expression evaluation.
	 *
	 *  Note: All other fields in the document (not shown here) gets projected as it is.
	 *
	 */

	BsonIntermediatePathNode *root = MakeRootNode();

	/* unwound elements come from arrays in documents which will already be evaluated in a previous stage or directly from a collection, */
	/* so we can safely treat the values as constants and no need to pay the cost to parse them as expressions. */
	bool treatLeafDataAsConstant = true;

	/* Create the node for unwound element */
	if (element->value_type != BSON_TYPE_EOD)
	{
		StringView pathView = CreateStringViewFromString(path);
		TraverseDottedPathAndAddLeafFieldNode(&pathView,
											  element,
											  root,
											  BsonDefaultCreateLeafNode,
											  treatLeafDataAsConstant);
	}

	/* Create the node for the new indexField name */
	if (indexFieldName != NULL)
	{
		bson_value_t indexValue;
		memset(&indexValue, 0, sizeof(bson_value_t));
		if (index > -1)
		{
			indexValue.value_type = BSON_TYPE_INT64;
			indexValue.value.v_int64 = index;
		}
		else
		{
			indexValue.value_type = BSON_TYPE_NULL;
		}

		StringView indexFieldView = CreateStringViewFromString(indexFieldName);
		TraverseDottedPathAndAddLeafFieldNode(&indexFieldView,
											  &indexValue,
											  root,
											  BsonDefaultCreateLeafNode,
											  treatLeafDataAsConstant);
	}

	pgbson_writer writer;
	bson_iter_t documentIterator;
	PgbsonWriterInit(&writer);
	PgbsonInitIterator(document, &documentIterator);
	bool projectNonMatchingField = true;
	ProjectDocumentState projectDocState = {
		.isPositionalAlreadyEvaluated = false,
		.parentDocument = document,
		.pendingProjectionState = NULL,
		.skipIntermediateArrayFields = false,
	};

	bool isInNestedArray = false;
	TraverseObjectAndAppendToWriter(&documentIterator, root, &writer,
									projectNonMatchingField,
									&projectDocState, isInNestedArray);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * BsonUnwindEmptyArray produces the output document when an empty array is found
 * at the unwind target
 *    document -> source document
 *    path -> path being unwound
 *    indexFieldName -> optional name for the index field to be added
 */
static pgbson *
BsonUnwindEmptyArray(pgbson *document, char *path, char *indexFieldName)
{
	pgbson_writer projectSpecWriter;
	PgbsonWriterInit(&projectSpecWriter);

	bool value = false;
	PgbsonWriterAppendBool(&projectSpecWriter, path, strlen(path),
						   value);

	if (indexFieldName != NULL)
	{
		/* Single elements have null index */
		PgbsonWriterAppendNull(&projectSpecWriter, indexFieldName, strlen(
								   indexFieldName));
	}

	bson_iter_t projectSpec;
	PgbsonWriterGetIterator(&projectSpecWriter, &projectSpec);

	bool forceProjectId = true;
	bool allowInclusionExclusion = true;

	const BsonProjectionQueryState *projectionState =
		GetProjectionStateForBsonProject(&projectSpec,
										 forceProjectId, allowInclusionExclusion);
	return ProjectDocumentWithState(document, projectionState);
}


/*
 * Whether or not to process intermediate arrays encountered in traversal.
 * Always true for distinct.
 */
static bool
DistinctContinueProcessIntermediateArray(void *state, const bson_value_t *value)
{
	return true;
}


/*
 * Handles non-existent paths and type mismatches - ignored for Distinct.
 */
static void
DistinctSetTraverseResult(void *state, TraverseBsonResult result)
{ }


/*
 * Adds the current value to the tuple store inside the DistinctTraverseState.
 */
inline static void
AddToDistinctTupleStore(const bson_value_t *bsonValue,
						DistinctTraverseState *traverseState)
{
	Datum values[1];
	bool nulls[1];

	values[0] = PointerGetDatum(BsonValueToDocumentPgbson(bsonValue));
	nulls[0] = false;
	tuplestore_putvalues(traverseState->tupleStore, traverseState->tupleDescriptor,
						 values, nulls);
}


/*
 * Adds the current element of the array field to the tuple store.
 */
static bool
DistinctVisitArrayField(pgbsonelement *element, const StringView *traversePath, int
						arrayIndex, void *state)
{
	DistinctTraverseState *traverseState = (DistinctTraverseState *) state;
	AddToDistinctTupleStore(&element->bsonValue, traverseState);

	/* Continue traversing */
	return true;
}


/*
 * Adds the current top level field to the tuple store if and only if it's not an array.
 * Array fields are added by DistinctVisitArrayField instead.
 */
static bool
DistinctVisitTopLevelField(pgbsonelement *element, const StringView *traversePath,
						   void *state)
{
	if (element->bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		/* Continue traversing */
		return true;
	}

	DistinctTraverseState *traverseState = (DistinctTraverseState *) state;
	AddToDistinctTupleStore(&element->bsonValue, traverseState);

	/* Continue traversing */
	return true;
}
