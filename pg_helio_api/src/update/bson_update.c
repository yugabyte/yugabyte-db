/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_update.c
 *
 * Implementation of the update operation.
 *
 *-------------------------------------------------------------------------
 */
#include "math.h"
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"
#include "datatype/timestamp.h"
#include "funcapi.h"

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "aggregation/bson_project.h"
#include "utils/mongo_errors.h"
#include "update/bson_update_common.h"
#include "update/bson_update.h"
#include "utils/fmgr_utils.h"
#include "aggregation/bson_query.h"
#include "commands/commands_common.h"

#include "api_hooks.h"
#include "api_hooks_def.h"

CreateBsonUpdateTracker_HookType create_update_tracker_hook = NULL;
BuildUpdateDescription_HookType build_update_description_hook = NULL;

/*
 * Metadata pertaining to update processing
 * that can be cached and reused across executions
 * of the function.
 */
typedef struct BsonUpdateMetadata
{
	/* The Update Type */
	UpdateType updateType;

	/* The source document used in handling upserts */
	pgbson *sourceDocOnUpsert;

	/* Cached state based on the update type */
	union
	{
		/* Update type state if it's an aggregation pipeline update */
		struct AggregationPipelineUpdateState *aggregationState;

		/*
		 * Update type state if it's an operator update state - if the cached
		 * update tree is not supported - this is NULL
		 */
		const BsonIntermediatePathNode *operatorState;
	};
} BsonUpdateMetadata;

/* Context used in ProcessQueryProjectionValue*/
typedef struct
{
	/* root node of Update Spec Tree */
	BsonIntermediatePathNode *root;

	/* Type of update */
	UpdateType updateType;
} QueryProjectionContext;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void BuildBsonUpdateMetadata(BsonUpdateMetadata *metadata, pgbson *updateSpec,
									pgbson *querySpec, pgbson *arrayFilters,
									bool buildSourceDocOnUpsert);

static pgbson * BsonUpdateDocumentCore(pgbson *sourceDocument, pgbson *updateSpec,
									   BsonUpdateMetadata *metadata,
									   pgbson **updateDescription);

static pgbson * ProcessReplaceDocument(pgbson *sourceDoc, pgbson *updateSpec,
									   bool isUpsert);

static pgbson * BuildBsonDocumentFromQuery(pgbson *sourceDoc, pgbson *querySpec,
										   UpdateType updateType);

static void ProcessQueryProjectionValue(void *context, const char *path, const
										bson_value_t *value);

/*
 * Global state in the process to capture if the call to bson_update_document
 * performed an update or not. This is an optimization in order to be able to
 * calculate the number of matched documents vs the number of actually
 * updated documents when the update command is called. There are
 * ways to do it without this, but with more work for the worker nodes
 * making update a lot slower. Because we call the UDF that sets this value
 * and the UDF that consumes it as part of the same UPDATE query,
 * concurrency shouldn't be a concern, this will be local
 * to the process executing the update on a shard, and Postgres porcesses
 * only one thing at a time.
 */
static bool LastBsonUpdateReturnedNewValue = false;


/*
 * Throws an error that the _id has been detected as changed in the process of updating the document.
 * Call it when UpdateType is Replace Document and _id has changed.
 */
inline static void
ThrowIdPathModifiedError(void)
{
	ereport(ERROR, (errcode(MongoImmutableField),
					errmsg(
						"After applying the update, the (immutable) field '_id' was found to have been altered")));
}


/*
 * Ensures that in Update Type Replacement the _id field in a write document conforms to the requirements of Mongo
 * Right.
 */
inline static void
ValidateIdForUpdateTypeReplacement(const bson_value_t *idValue)
{
	if (idValue->value_type == BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoNotSingleValueField),
						errmsg(
							"After applying the update to the document, the (immutable) field"
							" '_id' was found to be an array or array descendant.")));
	}
	ValidateIdField(idValue);
}


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_update_document);
PG_FUNCTION_INFO_V1(bson_update_returned_value);


/*
 * bson_update_document processes the update operation on a given document.
 * The first argument is the source document to apply the updates on.
 * If an empty document is specified, it is assumed to be an 'upsert'
 * The second argument is a bson element that is the update spec. This is of the form:
 * { "": <update|replace|pipeline> }.
 * The third argument is the query spec used to form this update. This is the document
 * that is generally passed to the @@ operator and is of the form { "$and": [ { "a": 1}, { "b": 1 }]}
 */
Datum
bson_update_document(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(2))
	{
		/* be on the safe side fwiw */
		ereport(ERROR, (errmsg("sourceDocument / updateSpec / querySpec "
							   "cannot be NULL")));
	}

	pgbson *sourceDocument = PG_GETARG_PGBSON(0);
	pgbson *updateSpec = PG_GETARG_PGBSON(1);
	pgbson *querySpec = PG_GETARG_PGBSON(2);
	pgbson *arrayFilters = PG_GETARG_MAYBE_NULL_PGBSON(3);
	bool buildUpdateDesc = PG_GETARG_BOOL(4);

	/* an empty document is treated as an upsert. */
	bool buildSourceDocOnUpsert = IsPgbsonEmptyDocument(sourceDocument);

	/* Build any cacheable state for processing updates */
	int stateArgPositions[3] = { 1, 2, 3 };
	BsonUpdateMetadata *metadata;

	SetCachedFunctionStateMultiArgs(
		metadata,
		BsonUpdateMetadata,
		&stateArgPositions[0],
		3,
		BuildBsonUpdateMetadata,
		updateSpec, querySpec, arrayFilters,
		buildSourceDocOnUpsert);

	if (metadata == NULL)
	{
		ereport(ERROR, (errmsg(
							"bson update should only be called with consts or params")));
	}

	/* Returns (newDocument bson, updateDesc bson) */
	Datum values[2];
	bool nulls[2];
	memset(values, 0, sizeof(values));
	memset(nulls, 0, sizeof(nulls));

	TupleDesc tupleDescriptor = NULL;
	if (get_call_result_type(fcinfo, NULL, &tupleDescriptor) != TYPEFUNC_COMPOSITE)
	{
		elog(ERROR, "return type must be a row type");
	}

	if (tupleDescriptor->natts != 2)
	{
		elog(ERROR, "incorrect number of output arguments");
	}

	/* build updateDesc if requested */
	pgbson *updateDesc = NULL;
	pgbson *document = BsonUpdateDocumentCore(sourceDocument, updateSpec, metadata,
											  buildUpdateDesc ? &updateDesc : NULL);

	if (document != NULL)
	{
		LastBsonUpdateReturnedNewValue = true;
		values[0] = PointerGetDatum(document);

		if (updateDesc != NULL)
		{
			values[1] = PointerGetDatum(updateDesc);
		}
		else
		{
			/*
			 * Change streams is not enabled or updateDesc is not needed for this
			 * update operation. For example, updateDesc is not built for replace
			 * type of updates.
			 */
			nulls[1] = true;
		}
	}
	else
	{
		/* No update is needed */
		LastBsonUpdateReturnedNewValue = false;
		nulls[0] = true;
		nulls[1] = true;
	}

	HeapTuple ret = heap_form_tuple(tupleDescriptor, values, nulls);
	return HeapTupleGetDatum(ret);
}


/*
 * bson_update_returned_value is a helper function for the update command when multi:true is specified
 * this is used to get information if the last call to bson_update_document actually performed an update
 * or the resulting document was the same as the source document. This is used to avoid multiple CTEs in the
 * update query that would be pushed down to worker nodes in multi-shard scenarios making update a lot slower
 */
Datum
bson_update_returned_value(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(LastBsonUpdateReturnedNewValue ? 1 : 0);
}


/*
 * ValidateUpdateDocument is a wrapper around BsonUpdateDocument that
 * can be used to validate given update document.
 */
void
ValidateUpdateDocument(pgbson *updateSpec, pgbson *querySpec, pgbson *arrayFilters)
{
	BsonUpdateMetadata metadata = { 0 };
	bool buildSourceDocOnUpsert = false;
	BuildBsonUpdateMetadata(&metadata, updateSpec, querySpec, arrayFilters,
							buildSourceDocOnUpsert);
}


/*
 * BsonUpdateDocument contains the internal implementation of bson_update_document.
 * returns NULL if no update is needed.
 */
pgbson *
BsonUpdateDocument(pgbson *sourceDocument, pgbson *updateSpec,
				   pgbson *querySpec, pgbson *arrayFilters)
{
	BsonUpdateMetadata metadata = { 0 };

	/* an empty document is treated as an upsert. */
	bool buildSourceDocOnUpsert = IsPgbsonEmptyDocument(sourceDocument);

	BuildBsonUpdateMetadata(&metadata, updateSpec, querySpec, arrayFilters,
							buildSourceDocOnUpsert);
	return BsonUpdateDocumentCore(sourceDocument, updateSpec, &metadata, NULL);
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

/*
 * The core implementation of BsonUpdateDocument.
 * Processes an update based on the updateMetadata on the sourceDocument and updateSpec.
 *
 * Returns NULL if update was a no-op.
 */
pgbson *
BsonUpdateDocumentCore(pgbson *sourceDocument, pgbson *updateSpec,
					   BsonUpdateMetadata *updateMetadata,
					   pgbson **updateDescription)
{
	bson_iter_t sourceDocumentIterator;
	PgbsonInitIterator(sourceDocument, &sourceDocumentIterator);

	/* an empty document is treated as an upsert. */
	bool isUpsert = !bson_iter_next(&sourceDocumentIterator);

	if (isUpsert)
	{
		sourceDocument = updateMetadata->sourceDocOnUpsert;
	}

	/* first look up the updateSpec to determine what kind of update it is. */
	pgbson *document;
	switch (updateMetadata->updateType)
	{
		case UpdateType_ReplaceDocument:
		{
			document = ProcessReplaceDocument(sourceDocument, updateSpec,
											  isUpsert);
			if (PgbsonEquals(sourceDocument, document) && !isUpsert)
			{
				/* signal that update was a no-op */
				document = NULL;
			}
			break;
		}

		case UpdateType_Operator:
		{
			if (updateDescription != NULL && create_update_tracker_hook != NULL)
			{
				BsonUpdateTracker *updateTracker = create_update_tracker_hook();
				document = ProcessUpdateOperatorWithState(sourceDocument,
														  updateMetadata->operatorState,
														  isUpsert,
														  updateTracker);

				/*
				 * Don't build updateDescription unnecessarily if document is NULL
				 * because it means that update was a no-op.
				 */
				if (document && build_update_description_hook != NULL)
				{
					*updateDescription = build_update_description_hook(updateTracker);
				}
			}
			else
			{
				document = ProcessUpdateOperatorWithState(sourceDocument,
														  updateMetadata->operatorState,
														  isUpsert,
														  NULL);
			}

			break;
		}

		case UpdateType_AggregationPipeline:
		{
			document = ProcessAggregationPipelineUpdate(sourceDocument,
														updateMetadata->
														aggregationState,
														isUpsert);

			/*
			 * TODO: Using PgbsonEquals() here might result in incorrectly deciding
			 *       that the document has been updated. For example, since
			 *       {"_id": 1, "a": 1, "b": 1} != {"_id": 1, "b": 1, "a": 1}, we
			 *       would report that the document has been updated in following
			 *       case, but this actually shouldn't be the case:
			 *
			 *         document = {"_id": 1, "a": 1, "b": 1}
			 *         u = [ {"$unset": ["a"]}, {"$set": {"a": 1}} ]
			 *
			 *       Instead, we need to decide whether the update was a no-op by
			 *       taking all the stages of the aggregation-pipelined update.
			 *       To do that, we might want to use a single update-spec tree
			 *       across all the stages in ProcessAggregationPipelineUpdate().
			 *
			 *       See https://msdata.visualstudio.com/CosmosDB/_workitems/edit/1963348.
			 */
			if (PgbsonEquals(sourceDocument, document) && !isUpsert)
			{
				/* signal that update was a no-op */
				document = NULL;
			}
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_CHECK_VIOLATION), errmsg(
								"Unrecognized update type %d",
								updateMetadata->updateType)));
			break;
		}
	}

	if (document != NULL)
	{
		uint32_t size = PgbsonGetBsonSize(document);
		if (size > BSON_MAX_ALLOWED_SIZE)
		{
			int errorCode = isUpsert ? MongoDocumentToUpsertLargerThanMaxSize :
							MongoDocumentAfterUpdateLargerThanMaxSize;
			ereport(ERROR, (errcode(errorCode),
							errmsg("Size %u is larger than MaxDocumentSize %u",
								   size, BSON_MAX_ALLOWED_SIZE)));
		}
	}

	return document;
}


/*
 * Builds and sets metadata pertaining to the update based on the updateSpec and querySpec
 * into the metadata value provided.
 */
static void
BuildBsonUpdateMetadata(BsonUpdateMetadata *metadata, pgbson *updateSpec,
						pgbson *querySpec, pgbson *arrayFilters,
						bool buildSourceDocOnUpsert)
{
	metadata->updateType = DetermineUpdateType(updateSpec);

	/* BuildBsonDocumentFromQuery only gets called for upsert */
	if (buildSourceDocOnUpsert)
	{
		pgbson *emptyDoc = PgbsonInitEmpty();
		metadata->sourceDocOnUpsert = BuildBsonDocumentFromQuery(emptyDoc, querySpec,
																 metadata->updateType);
	}

	/* Build and cache any state pertaining to the update type */
	switch (metadata->updateType)
	{
		case UpdateType_AggregationPipeline:
		{
			if (arrayFilters != NULL)
			{
				pgbsonelement arrayFiltersElement = { 0 };
				PgbsonToSinglePgbsonElement(arrayFilters, &arrayFiltersElement);
				if (!IsBsonValueEmptyArray(&arrayFiltersElement.bsonValue))
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg(
										"arrayFilters may not be specified for pipeline-style updates")));
				}
			}

			metadata->aggregationState = GetAggregationPipelineUpdateState(updateSpec);
			break;
		}

		case UpdateType_Operator:
		{
			metadata->operatorState = GetOperatorUpdateState(updateSpec, querySpec,
															 arrayFilters);
			break;
		}

		case UpdateType_ReplaceDocument:
		{
			/* Simply validate the replace doc */
			bson_iter_t updateIterator;
			PgbsonInitIteratorAtPath(updateSpec, "", &updateIterator);
			if (!BSON_ITER_HOLDS_DOCUMENT(&updateIterator))
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"Replace should be a document")));
			}
			break;
		}

		default:
		{
			break;
		}
	}
}


/*
 * Traverses the updateSpec to determine whether the update is a replace,
 * an operator based update, or an aggregation pipeline update.
 * If the update is an array, it is assumed to be an aggregation pipeline.
 * If the update is an object, if any keys have a '$' - it's an operator update
 * Otherwise it's a replace.
 */
UpdateType
DetermineUpdateType(pgbson *updateSpec)
{
	bson_iter_t updateIterator;
	bson_iter_t updateDocumentIterator;
	bool isUpdateTypeReplacement = false;

	if (!PgbsonInitIteratorAtPath(updateSpec, "", &updateIterator))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"Update should be specified")));
	}

	if (BSON_ITER_HOLDS_ARRAY(&updateIterator))
	{
		return UpdateType_AggregationPipeline;
	}
	else if (BSON_ITER_HOLDS_DOCUMENT(&updateIterator) &&
			 bson_iter_recurse(&updateIterator, &updateDocumentIterator))
	{
		while (bson_iter_next(&updateDocumentIterator))
		{
			const char *path = bson_iter_key(&updateDocumentIterator);
			uint32_t pathLength = bson_iter_key_len(&updateDocumentIterator);

			/* found an operator. */
			if (pathLength > 1 && path[0] == '$')
			{
				if (!isUpdateTypeReplacement)
				{
					return UpdateType_Operator;
				}
				else
				{
					ereport(ERROR, (errcode(MongoDollarPrefixedFieldName),
									errmsg(
										"The dollar ($) prefixed field '%s' in '%s' is not allowed in the context of an update's"
										" replacement document. Consider using an aggregation pipeline with $replaceWith.",
										path, path)));
				}
			}
			else
			{
				isUpdateTypeReplacement = true;
			}
		}

		/* if no operators specified then it's a replace. */
		return UpdateType_ReplaceDocument;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"Update should be a document or an array")));
	}
}


/*
 * Given an update spec that is a replace document, writes the replacement document
 * processing the necessary update. This covers scenarios around _id validation, and ensuring
 * the _id is propagated from source to target document. It also means for upserts, extracting the _id
 * from the filters into the target document.
 */
static pgbson *
ProcessReplaceDocument(pgbson *sourceDoc, pgbson *updateSpec,
					   bool isUpsert)
{
	bson_iter_t sourceDocIterator;
	bson_iter_t updateIterator;
	bson_iter_t replaceDocumentIterator;
	pgbson_writer writer;
	PgbsonInitIteratorAtPath(updateSpec, "", &updateIterator);
	if (!BSON_ITER_HOLDS_DOCUMENT(&updateIterator))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Replace should be a document")));
	}

	uint32_t documentLength;
	const uint8_t *documentBytes;
	bson_iter_document(&updateIterator, &documentLength, &documentBytes);

	/* validate the replace document. */
	ValidateInputBsonBytes(documentBytes,
						   documentLength,
						   BSON_VALIDATE_NONE);

	PgbsonWriterInit(&writer);

	/* write the object_id of the document. */
	const bson_value_t *sourceIdValue = NULL;
	if (!isUpsert)
	{
		if (!PgbsonInitIteratorAtPath(sourceDoc, "_id", &sourceDocIterator))
		{
			ereport(ERROR, (errmsg(
								"Unexpected: Document to update did not have an _id")));
		}

		sourceIdValue = bson_iter_value(&sourceDocIterator);
		PgbsonWriterAppendValue(&writer, "_id", 3, sourceIdValue);
	}
	else
	{
		/* first we look up the _id from the document. */
		const bson_value_t *idFromReplaceDocument = NULL;
		bson_iter_init_from_data(&replaceDocumentIterator, documentBytes, documentLength);

		if (bson_iter_find_w_len(&replaceDocumentIterator, "_id", 3))
		{
			idFromReplaceDocument = bson_iter_value(&replaceDocumentIterator);
		}

		/* next we look up the id value from the query document. */
		bson_iter_t queryDocumentIterator;
		bson_value_t idFromQueryDocument = { 0 };
		if (PgbsonInitIteratorAtPath(sourceDoc, "_id",
									 &queryDocumentIterator))
		{
			idFromQueryDocument = *bson_iter_value(&queryDocumentIterator);
		}

		/* if both are specified make sure they're equal */
		if (idFromReplaceDocument != NULL &&
			idFromQueryDocument.value_type != BSON_TYPE_EOD &&
			!BsonValueEquals(idFromReplaceDocument, &idFromQueryDocument))
		{
			ThrowIdPathModifiedError();
		}

		/* now set the new id in priority order. */
		if (idFromReplaceDocument != NULL)
		{
			ValidateIdForUpdateTypeReplacement(idFromReplaceDocument);
			PgbsonWriterAppendValue(&writer, "_id", 3, idFromReplaceDocument);
		}
		else if (idFromQueryDocument.value_type != BSON_TYPE_EOD)
		{
			ValidateIdForUpdateTypeReplacement(&idFromQueryDocument);
			PgbsonWriterAppendValue(&writer, "_id", 3, &idFromQueryDocument);
		}
		else
		{
			/* generate a new value. */
			bson_value_t newIdValue;
			newIdValue.value_type = BSON_TYPE_OID;
			bson_oid_init(&(newIdValue.value.v_oid), NULL);
			PgbsonWriterAppendValue(&writer, "_id", 3, &newIdValue);
		}
	}

	/* now walk the document */
	bson_iter_init_from_data(&replaceDocumentIterator, documentBytes, documentLength);
	while (bson_iter_next(&replaceDocumentIterator))
	{
		const char *key = bson_iter_key(&replaceDocumentIterator);
		uint32_t keyLength = bson_iter_key_len(&replaceDocumentIterator);

		/* ensure we're not rewriting the _id to something else. */
		if (strcmp(key, "_id") == 0)
		{
			if (sourceIdValue != NULL &&
				!BsonValueEquals(sourceIdValue,
								 bson_iter_value(&replaceDocumentIterator)))
			{
				ThrowIdPathModifiedError();
			}

			continue;
		}

		PgbsonWriterAppendValue(&writer, key, keyLength, bson_iter_value(
									&replaceDocumentIterator));
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Given a query spec, walks the query and builds a document that
 * will be used in the upsert case as the initial document.
 */
static pgbson *
BuildBsonDocumentFromQuery(pgbson *sourceDoc, pgbson *querySpec,
						   UpdateType updateType)
{
	BsonIntermediatePathNode *root = MakeRootNode();

	bson_iter_t queryDocIterator;
	PgbsonInitIterator(querySpec, &queryDocIterator);
	QueryProjectionContext context = { .root = root, .updateType = updateType };

	bool isUpsert = true;
	TraverseQueryDocumentAndProcess(&queryDocIterator, &context,
									&ProcessQueryProjectionValue,
									isUpsert);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	bson_iter_t sourceDocIterator;
	PgbsonInitIterator(sourceDoc, &sourceDocIterator);
	bool projectNonMatchingFields = true;
	ProjectDocumentState projectDocState = {
		.isPositionalAlreadyEvaluated = false,
		.parentDocument = sourceDoc,
		.pendingProjectionState = NULL,
	};

	bool isInNestedArray = false;
	TraverseObjectAndAppendToWriter(&sourceDocIterator, root, &writer,
									projectNonMatchingFields,
									&projectDocState, isInNestedArray);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * For single projection from query spec pointed by path, this method try to add that path to the tree
 * and validate path correctness as well
 */
static void
ProcessQueryProjectionValue(void *context, const char *path, const bson_value_t *value)
{
	QueryProjectionContext *contextData = (QueryProjectionContext *) context;
	BsonIntermediatePathNode *tree = contextData->root;
	bool isUpdateTypeReplacement = contextData->updateType == UpdateType_ReplaceDocument;

	StringView pathView = { .string = path, .length = strlen(path) };

	bool nodeCreated = false;
	void *nodeCreationState = NULL;

	/*
	 * Even though this method is processing a new path (say "x") in the query spec,
	 * we cannot assume that "x" will end up in a leaf node as, the same path could have been
	 * specified before (say, via "x.y") in that spec.
	 */
	bool treatLeafDataAsConstant = true;
	TraverseDottedPathAndGetOrAddField(&pathView,
									   value,
									   tree,
									   BsonDefaultCreateIntermediateNode,
									   BsonDefaultCreateLeafNode,
									   treatLeafDataAsConstant,
									   nodeCreationState,
									   &nodeCreated);

	bool isDocumentDottedIdField = strncmp(path, "_id.", 4) == 0;
	bool isDocumentIdField = isDocumentDottedIdField || strcmp(path, "_id") == 0;

	/* Native mongo gives an error when update type is replacement and querySpec has dotted id field */
	if (isUpdateTypeReplacement && isDocumentDottedIdField)
	{
		ereport(ERROR, (errcode(MongoNotExactValueField),
						errmsg(
							"field at '_id' must be exactly specified, field at sub-path '%s'found",
							path)));
	}

	if ((!isUpdateTypeReplacement || isDocumentIdField) && !nodeCreated)
	{
		ereport(ERROR, (errcode(MongoNotSingleValueField),
						errmsg(
							"cannot infer query fields to set, path '%s' is matched twice",
							path)));
	}
}
