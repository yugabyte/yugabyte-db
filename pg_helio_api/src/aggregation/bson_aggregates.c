/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_aggregates.c
 *
 * Aggregation implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <catalog/pg_type.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/heap_utils.h>
#include "utils/helio_errors.h"
#include "metadata/collection.h"
#include "commands/insert.h"
#include "sharding/sharding.h"
#include "utils/hashset_utils.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_write.h"
#include "aggregation/bson_sorted_accumulator.h"
#include "operators/bson_expression_operators.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

typedef struct BsonNumericAggState
{
	bson_value_t sum;
	int64_t count;
} BsonNumericAggState;

typedef struct BsonArrayGroupAggState
{
	pgbson_writer writer;
	pgbson_array_writer arrayWriter;
} BsonArrayGroupAggState;

/*
 * Window aggregation state for bson_array_agg, contains the
 * list of contents for bson array
 */
typedef struct BsonArrayWindowAggState
{
	List *aggregateList;
} BsonArrayWindowAggState;

typedef struct BsonArrayAggState
{
	union
	{
		/* Transition state when used a regular aggregate with $group */
		BsonArrayGroupAggState group;

		/* Transition state when used as window aggregate with $setWindowFields */
		BsonArrayWindowAggState window;
	} aggState;
	int64_t currentSizeWritten;
	bool isWindowAggregation;
} BsonArrayAggState;

typedef struct BsonObjectAggState
{
	BsonIntermediatePathNode *tree;
	int64_t currentSizeWritten;
	bool addEmptyPath;
} BsonObjectAggState;

typedef struct BsonAddToSetState
{
	HTAB *set;
	int64_t currentSizeWritten;
	bool isWindowAggregation;
} BsonAddToSetState;

typedef struct BsonOutAggregateState
{
	/*
	 * Cached collection object to which documents are added via the
	 * transaction function. If the $out collection does not exist
	 * it will be the new collection created. Otherwise, it's a temporary
	 * collection where the data is parked before we copy the data over to
	 * the target collection. The copying is done via renaming if the target
	 * collection has a different name than the source collection. Otherwise,
	 * we drop all rows from the source collection and then copy from the temp
	 * collection. Renaming is a DDL change which is not allowed in the same
	 * transaction where we are also reading from the collection.
	 */
	MongoCollection *stagingCollection;

	/* Object holding that write error that we will send to the user*/
	WriteError *writeError;

	/* Final $out collection name. This can be same as the input collection */
	char targetCollectioName[MAX_COLLECTION_NAME_LENGTH];

	/* If the target collection already exists.*/
	bool collectionExists;

	/* If the target collection exists and it's the same collection on which
	 * the aggregation pipeline is being run.*/
	bool sameSourceAndTarget;
	int64_t docsWritten;
	bool hasFailure;
} BsonOutAggregateState;

/* state used for maxN and minN both */
typedef struct BinaryHeapState
{
	BinaryHeap *heap;
	bool isMaxN;
} BinaryHeapState;

const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static bytea * AllocateBsonNumericAggState(void);
static void CheckAggregateIntermediateResultSize(uint32_t size);
static void GenerateUnqiqueStagingCollectionNameForOut(char *collectionName);
static void CreateObjectAggTreeNodes(BsonObjectAggState *currentState,
									 pgbson *currentValue);
static void ValidateMergeObjectsInput(pgbson *input);
static Datum ParseAndReturnMergeObjectsTree(BsonObjectAggState *state);
static Datum bson_maxminn_transition(PG_FUNCTION_ARGS, bool isMaxN);

void DeserializeBinaryHeapState(bytea *byteArray, BinaryHeapState *state);
bytea * SerializeBinaryHeapState(MemoryContext aggregateContext, BinaryHeapState *state,
								 bytea *byteArray);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_sum_avg_transition);
PG_FUNCTION_INFO_V1(bson_sum_final);
PG_FUNCTION_INFO_V1(bson_avg_final);
PG_FUNCTION_INFO_V1(bson_sum_avg_combine);
PG_FUNCTION_INFO_V1(bson_sum_avg_minvtransition);
PG_FUNCTION_INFO_V1(bson_min_transition);
PG_FUNCTION_INFO_V1(bson_max_transition);
PG_FUNCTION_INFO_V1(bson_min_max_final);
PG_FUNCTION_INFO_V1(bson_min_combine);
PG_FUNCTION_INFO_V1(bson_max_combine);
PG_FUNCTION_INFO_V1(bson_build_distinct_response);
PG_FUNCTION_INFO_V1(bson_array_agg_transition);
PG_FUNCTION_INFO_V1(bson_array_agg_minvtransition);
PG_FUNCTION_INFO_V1(bson_array_agg_final);
PG_FUNCTION_INFO_V1(bson_distinct_array_agg_transition);
PG_FUNCTION_INFO_V1(bson_distinct_array_agg_final);
PG_FUNCTION_INFO_V1(bson_object_agg_transition);
PG_FUNCTION_INFO_V1(bson_object_agg_final);
PG_FUNCTION_INFO_V1(bson_out_transition);
PG_FUNCTION_INFO_V1(bson_out_final);
PG_FUNCTION_INFO_V1(bson_add_to_set_transition);
PG_FUNCTION_INFO_V1(bson_add_to_set_final);
PG_FUNCTION_INFO_V1(bson_merge_objects_transition_on_sorted);
PG_FUNCTION_INFO_V1(bson_merge_objects_transition);
PG_FUNCTION_INFO_V1(bson_merge_objects_final);
PG_FUNCTION_INFO_V1(bson_maxn_transition);
PG_FUNCTION_INFO_V1(bson_maxminn_final);
PG_FUNCTION_INFO_V1(bson_minn_transition);
PG_FUNCTION_INFO_V1(bson_maxminn_combine);

Datum
bson_out_transition(PG_FUNCTION_ARGS)
{
	BsonOutAggregateState *currentState = { 0 };
	bytea *bytes = { 0 };

	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	char *srcDatabaseName = text_to_cstring(PG_GETARG_TEXT_P(2));
	Datum srcDatabaseNameDatum = PG_GETARG_DATUM(2);
	char *srcCollectionName = text_to_cstring(PG_GETARG_TEXT_P(3));
	Datum srcCollectionNameDatum = PG_GETARG_DATUM(3);

	Datum destDatabaseNameDatum = PG_GETARG_DATUM(4);
	char *destDatabaseName = text_to_cstring(PG_GETARG_TEXT_P(4));
	Datum destCollectionNameDatum = PG_GETARG_DATUM(5);
	char *destCollectionName = text_to_cstring(PG_GETARG_TEXT_P(5));

	if (PG_ARGISNULL(0)) /* First arg is the running aggregated state*/
	{
		/*
		 *  Staging collection is where we park the data for $out stage.
		 *  If the destination collection does not exist, we create the
		 *  destination collection and the staging collection points to that.
		 *
		 *  Otherwise, we create a new intermediate collection to be the staging
		 *  collection. See details in the 'else' block.
		 *
		 *  Please see the design doc for more details.
		 *  https://eng.ms/docs/cloud-ai-platform/azure-data/azure-data-azure-databases/cosmos-db-and-postgresql/azure-cosmos-db/mongo-on-citus/aggregation_stages/out
		 *
		 */
		MongoCollection *stagingCollection;
		MongoCollection *destCollection = GetMongoCollectionByNameDatum(
			destDatabaseNameDatum,
			destCollectionNameDatum,
			NoLock);
		bool isSourceSameAsTarget = false;
		bool collectionExists = false;

		if (destCollection == NULL)
		{
			MongoCollection *sourceCollection = GetMongoCollectionByNameDatum(
				srcDatabaseNameDatum,
				srcCollectionNameDatum,
				NoLock);

			if (sourceCollection != NULL && sourceCollection->shardKey != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION28769),
								errmsg(
									"For a sharded cluster, the specified output database must already exist")));
			}


			ValidateCollectionNameForUnauthorizedSystemNs(destCollectionName,
														  destDatabaseNameDatum);
			CreateCollection(destDatabaseNameDatum,
							 destCollectionNameDatum);
			stagingCollection = GetMongoCollectionByNameDatum(destDatabaseNameDatum,
															  destCollectionNameDatum,
															  RowExclusiveLock);
		}
		else
		{
			char *stagingOutCollectionName = palloc0(45); /* Native Mongo Staging collections are 44 char long */
			collectionExists = true;

			if (destCollection->shardKey != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION28769),
								errmsg("%s.%s cannot be sharded", destDatabaseName,
									   destCollectionName)));
			}

			/*
			 *
			 * The destination collection exists so we need to create an intermediate
			 * staging collection before the newly written data is moved to destination collection.
			 * Note that, Mongo semantics require that $out stage overwrites the destination collection.
			 *
			 * The data movement to the destination is done by renaming the staging collection
			 * to the destination collection (with the instruction to drop the target). This is
			 * an efficient process.
			 *
			 * However, this option is not available when src == dest. We can't rename the collection
			 * we are reading from in the same transaction. In this case we create a shell 'TEMP TABLE'
			 * which only holds the newly written raw documents. At the end, we empty the dest collection
			 * and copy the data from the TEMP table.
			 *
			 */
			isSourceSameAsTarget = strcmp(destCollectionName,
										  srcCollectionName) == 0 &&
								   strcmp(destDatabaseName,
										  srcDatabaseName) == 0 ?
								   true :
								   false;

			GenerateUnqiqueStagingCollectionNameForOut(stagingOutCollectionName);

			SetupCollectionForOut(destDatabaseName, destCollectionName, destDatabaseName,
								  stagingOutCollectionName, isSourceSameAsTarget);

			if (isSourceSameAsTarget)
			{
				/*
				 * Temporary staging collection. This is shell Mongo collection. This is
				 * created when rename option is not available (i.e., source == target)
				 *
				 * The shell collections are not backed by ApiCatalogSchemaName.collections or
				 * ApiCatalogSchemaName.collection_indexes. We also don't renable retryable writes
				 * or change tracking for them. Since, eventually we will just copy data
				 * from the temp collection to the original mongo collection which has all
				 * the plumbing already.
				 *
				 */
				stagingCollection = GetTempMongoCollectionByNameDatum(
					destDatabaseNameDatum,
					CStringGetTextDatum(stagingOutCollectionName),
					stagingOutCollectionName,
					RowExclusiveLock);
			}
			else
			{
				/*
				 * Renameable staging collection. This is a true mongo collection that can
				 * renamed to target collection.
				 *
				 * This collection has all the necessary plumbing to be elevated to the
				 * destination collection.
				 *
				 */
				stagingCollection = GetMongoCollectionByNameDatum(destDatabaseNameDatum,
																  CStringGetTextDatum(
																	  stagingOutCollectionName),
																  RowExclusiveLock);
			}

			if (stagingCollection == NULL)
			{
				/* failed to create the staging collection. We use the word 'temporary' to match Mongo error message */
				ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION16994),
								errmsg(
									"failed to create temporary $out collection")));
			}
		}

		int result_size = sizeof(BsonOutAggregateState) + VARHDRSZ;
		bytea *combinedStateBytes = (bytea *) palloc0(result_size);
		SET_VARSIZE(combinedStateBytes, result_size);
		bytes = combinedStateBytes;

		currentState = (BsonOutAggregateState *) VARDATA(bytes);
		currentState->docsWritten = 0;

		if (collectionExists)
		{
			sprintf(currentState->targetCollectioName, "%s",
					destCollectionName);
			currentState->collectionExists = true;
			currentState->sameSourceAndTarget = isSourceSameAsTarget;
		}


		currentState->stagingCollection = stagingCollection;
		currentState->writeError = palloc0(sizeof(WriteError));
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonOutAggregateState *) VARDATA_ANY(bytes);

		if (currentState->hasFailure)
		{
			PG_RETURN_POINTER(bytes);
		}
	}

	pgbson *document = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *insertDoc = RewriteDocumentAddObjectId(document);

	/*
	 * It's possible that the document does not specify the full shard key.
	 * In that case we will only hash the parts that are specified. That
	 * is not problematic in terms of querying, because it means this
	 * object can only be found by queries that do not specify a full
	 * shard key filter and those queries scan all the shards.
	 */
	int64 shardKeyHash = ComputeShardKeyHashForDocument(
		currentState->stagingCollection->shardKey,
		currentState->stagingCollection->
		collectionId,
		insertDoc);

	bool isSuccess = TryInsertOne(currentState->stagingCollection, insertDoc,
								  shardKeyHash,
								  currentState->sameSourceAndTarget,
								  currentState->writeError);
	currentState->hasFailure = !isSuccess;
	currentState->docsWritten++;

	MemoryContextSwitchTo(oldContext);


	PG_RETURN_POINTER(bytes);
}


Datum
bson_out_final(PG_FUNCTION_ARGS)
{
	bytea *currentState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);
	int64_t nDocsWritten = 0;

	if (currentState != NULL)
	{
		BsonOutAggregateState *state = (BsonOutAggregateState *) VARDATA_ANY(
			currentState);
		nDocsWritten = state->docsWritten;

		/* This is where we clean up the staging collection. Note that we only need to
		 * do that if the destination collection already existed. */
		if (state->collectionExists)
		{
			if (state->hasFailure)
			{
				/* If there was a failure, drop the staging collection and report error. */
				nDocsWritten = 0;
				DropStagingCollectionForOut(CStringGetTextDatum(
												state->stagingCollection->name.
												databaseName),
											CStringGetTextDatum(
												state->stagingCollection->name.
												collectionName));

				ereport(ERROR, errmsg("%s", state->writeError->errmsg));
			}
			else
			{
				/* Otherwise if theere were no write error */
				if (state->sameSourceAndTarget)
				{
					/*
					 * If src == dest of the $out request, i.e., the staging collection created was a temporary collection,
					 * we overwrite the data in the dest collection from the temp collection. We also drop the source collection
					 * , i.e., the temporary collection
					 */
					bool drop_temp_collection = true;
					OverWriteDataFromStagingToDest(CStringGetTextDatum(
													   state->stagingCollection->name.
													   databaseName),
												   CStringGetTextDatum(
													   state->stagingCollection->name.
													   collectionName),
												   CStringGetTextDatum(
													   state->stagingCollection->name.
													   databaseName),
												   CStringGetTextDatum(
													   state->targetCollectioName),
												   drop_temp_collection);
				}
				else
				{
					/*
					 *  Otherwise, we rename the staging collection to be the destination collection, with the intention of
					 *  deleting the current destination collection.
					 */
					bool dropTarget = true;
					RenameCollection(CStringGetTextDatum(
										 state->stagingCollection->name.databaseName),
									 CStringGetTextDatum(
										 state->stagingCollection->name.collectionName),
									 CStringGetTextDatum(state->targetCollectioName),
									 dropTarget);
				}
			}
		}
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "nDocsWritten", 12, nDocsWritten);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


inline static Datum
BsonArrayAggTransitionCore(PG_FUNCTION_ARGS, bool handleSingleValueElement,
						   const char *path)
{
	BsonArrayAggState *currentState = { 0 };
	bytea *bytes;
	MemoryContext aggregateContext;
	int aggregationContext = AggCheckCallContext(fcinfo, &aggregateContext);
	if (aggregationContext == 0)
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	bool isWindowAggregation = aggregationContext == AGG_CONTEXT_WINDOW;

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0)) /* First arg is the running aggregated state*/
	{
		int bson_size = sizeof(BsonArrayAggState) + VARHDRSZ;
		bytea *combinedStateBytes = (bytea *) palloc0(bson_size);
		SET_VARSIZE(combinedStateBytes, bson_size);
		bytes = combinedStateBytes;

		currentState = (BsonArrayAggState *) VARDATA(bytes);
		currentState->isWindowAggregation = isWindowAggregation;
		currentState->currentSizeWritten = 0;

		if (isWindowAggregation)
		{
			currentState->aggState.window.aggregateList = NIL;
		}
		else
		{
			PgbsonWriterInit(&currentState->aggState.group.writer);
			PgbsonWriterStartArray(&currentState->aggState.group.writer, path, strlen(
									   path),
								   &currentState->aggState.group.arrayWriter);
		}
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonArrayAggState *) VARDATA_ANY(bytes);
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON_PACKED(1);
	bool isMissingValue = IsPgbsonEmptyDocument(currentValue);

	if (currentValue == NULL)
	{
		if (isWindowAggregation)
		{
			currentState->aggState.window.aggregateList = lappend(
				currentState->aggState.window.aggregateList, NULL);
		}
		else
		{
			PgbsonArrayWriterWriteNull(&currentState->aggState.group.arrayWriter);
		}
	}
	else
	{
		CheckAggregateIntermediateResultSize(currentState->currentSizeWritten +
											 PgbsonGetBsonSize(currentValue));

		if (isWindowAggregation)
		{
			pgbson *copiedPgbson = CopyPgbsonIntoMemoryContext(currentValue,
															   aggregateContext);
			currentState->aggState.window.aggregateList = lappend(
				currentState->aggState.window.aggregateList, copiedPgbson);
		}
		else if (!isMissingValue)
		{
			pgbsonelement singleBsonElement;
			if (handleSingleValueElement &&
				TryGetSinglePgbsonElementFromPgbson(currentValue, &singleBsonElement) &&
				singleBsonElement.pathLength == 0)
			{
				/* If it's a bson that's { "": value } */
				PgbsonArrayWriterWriteValue(&currentState->aggState.group.arrayWriter,
											&singleBsonElement.bsonValue);
			}
			else
			{
				PgbsonArrayWriterWriteDocument(&currentState->aggState.group.arrayWriter,
											   currentValue);
			}
		}
		currentState->currentSizeWritten += PgbsonGetBsonSize(currentValue);
	}

	if (currentValue != NULL)
	{
		PG_FREE_IF_COPY(currentValue, 1);
	}
	MemoryContextSwitchTo(oldContext);
	PG_RETURN_POINTER(bytes);
}


Datum
bson_array_agg_transition(PG_FUNCTION_ARGS)
{
	char *path = text_to_cstring(PG_GETARG_TEXT_P(2));

	/* We currently have 2 implementations of bson_array_agg. The newest has a parameter for handleSingleValueElement. */
	bool handleSingleValueElement = PG_NARGS() == 4 ? PG_GETARG_BOOL(3) : false;

	return BsonArrayAggTransitionCore(fcinfo, handleSingleValueElement, path);
}


Datum
bson_array_agg_minvtransition(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
	{
		ereport(ERROR, errmsg(
					"window aggregate function called in non-window-aggregate context"));
	}

	if (PG_ARGISNULL(0))
	{
		/* Returning NULL is an indiacation that inverse can't be applied and the aggregation needs to be redone */
		PG_RETURN_NULL();
	}

	bytea *bytes = PG_GETARG_BYTEA_P(0);
	BsonArrayAggState *currentState = (BsonArrayAggState *) VARDATA_ANY(bytes);

	if (!currentState->isWindowAggregation)
	{
		ereport(ERROR, errmsg(
					"window aggregate function received an invalid state for $push"));
	}

	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue != NULL)
	{
		/*
		 * Inverse function is called in sequence in which the row are added using the transition function,
		 * so we don't need to find the `currentValue` pgbson in the list, it can be safely assume that this
		 * is always present at the head of the list.
		 * We only assert that these values are equal to make sure that we are deleting the correct value
		 *
		 * TODO: Maybe move to DLL in future to avoid memory moves when removing first entry
		 */

		Assert(PgbsonEquals(currentValue, (pgbson *) linitial(
								currentState->aggState.window.aggregateList)));
		currentState->currentSizeWritten -= PgbsonGetBsonSize(currentValue);
	}

	currentState->aggState.window.aggregateList = list_delete_first(
		currentState->aggState.window.aggregateList);


	MemoryContextSwitchTo(oldContext);

	PG_RETURN_POINTER(bytes);
}


Datum
bson_distinct_array_agg_transition(PG_FUNCTION_ARGS)
{
	bool handleSingleValueElement = true;
	char *path = "values";
	return BsonArrayAggTransitionCore(fcinfo, handleSingleValueElement, path);
}


Datum
bson_array_agg_final(PG_FUNCTION_ARGS)
{
	bytea *currentArrayAgg = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	if (currentArrayAgg != NULL)
	{
		BsonArrayAggState *state = (BsonArrayAggState *) VARDATA_ANY(
			currentArrayAgg);
		if (state->isWindowAggregation)
		{
			pgbson_writer writer;
			pgbson_array_writer arrayWriter;
			PgbsonWriterInit(&writer);
			PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

			ListCell *cell;
			foreach(cell, state->aggState.window.aggregateList)
			{
				pgbson *currentValue = lfirst(cell);

				/* Empty pgbson values are missing field values which should not be pushed to the array */
				bool isMissingValue = IsPgbsonEmptyDocument(currentValue);
				if (currentValue != NULL && !isMissingValue)
				{
					pgbsonelement singleBsonElement;
					if (TryGetSinglePgbsonElementFromPgbson(currentValue,
															&singleBsonElement) &&
						singleBsonElement.pathLength == 0)
					{
						/* If it's a bson that's { "": value } */
						PgbsonArrayWriterWriteValue(&arrayWriter,
													&singleBsonElement.bsonValue);
					}
					else
					{
						PgbsonArrayWriterWriteDocument(&arrayWriter, currentValue);
					}
				}
			}
			PgbsonWriterEndArray(&writer, &arrayWriter);
			PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
		}
		else
		{
			PgbsonWriterEndArray(&state->aggState.group.writer,
								 &state->aggState.group.arrayWriter);
			PG_RETURN_POINTER(PgbsonWriterGetPgbson(&state->aggState.group.writer));
		}
	}
	else
	{
		MemoryContext aggregateContext;
		int aggContext = AggCheckCallContext(fcinfo, &aggregateContext);
		if (aggContext == AGG_CONTEXT_WINDOW)
		{
			/*
			 * We will need to return the default value of $push accumulator which is empty array in case
			 * where the window doesn't select any document.
			 *
			 * e.g ["unbounded", -1] => For the first row it doesn't select any rows.
			 */
			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendEmptyArray(&writer, "", 0);
			PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
		}
		PG_RETURN_NULL();
	}
}


/*
 * The finalfunc for distinct array aggregation.
 * Similar to array_agg but also writes "ok": 1
 * Also returns an empty array with "ok": 1 if never initialized.
 */
Datum
bson_distinct_array_agg_final(PG_FUNCTION_ARGS)
{
	bytea *currentArrayAgg = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	if (currentArrayAgg != NULL)
	{
		BsonArrayAggState *state = (BsonArrayAggState *) VARDATA_ANY(
			currentArrayAgg);

		if (state->isWindowAggregation)
		{
			ereport(ERROR, errmsg(
						"distinct array aggregate can't be used in a window context"));
		}
		PgbsonWriterEndArray(&state->aggState.group.writer,
							 &state->aggState.group.arrayWriter);

		PgbsonWriterAppendDouble(&state->aggState.group.writer, "ok", 2, 1);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&state->aggState.group.writer));
	}
	else
	{
		pgbson_writer emptyWriter;
		PgbsonWriterInit(&emptyWriter);
		PgbsonWriterAppendEmptyArray(&emptyWriter, "values", 6);

		PgbsonWriterAppendDouble(&emptyWriter, "ok", 2, 1);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&emptyWriter));
	}
}


/*
 * Core implementation of the object aggregation stage. This is used by both object_agg and merge_objects.
 * Both have the same implementation but differ in validations made inside the caller method.
 */
inline static Datum
AggregateObjectsCore(PG_FUNCTION_ARGS)
{
	BsonObjectAggState *currentState;
	bytea *bytes;

	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0)) /* First arg is the running aggregated state*/
	{
		int bson_size = sizeof(BsonObjectAggState) + sizeof(int64_t) + VARHDRSZ;
		bytea *combinedStateBytes = (bytea *) palloc0(bson_size);
		SET_VARSIZE(combinedStateBytes, bson_size);
		bytes = combinedStateBytes;

		currentState = (BsonObjectAggState *) VARDATA(bytes);
		currentState->currentSizeWritten = 0;
		currentState->tree = MakeRootNode();
		currentState->addEmptyPath = false;
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonObjectAggState *) VARDATA_ANY(bytes);
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (currentValue != NULL)
	{
		CheckAggregateIntermediateResultSize(currentState->currentSizeWritten +
											 PgbsonGetBsonSize(currentValue));

		/*
		 * We need to copy the whole pgbson because otherwise the pointers we store
		 * in the tree will reference an address in the stack. These are released after
		 * the function resolves and will point to garbage.
		 */
		currentValue = PgbsonCloneFromPgbson(currentValue);
		CreateObjectAggTreeNodes(currentState, currentValue);
		currentState->currentSizeWritten += PgbsonGetBsonSize(currentValue);
	}

	MemoryContextSwitchTo(oldContext);
	PG_RETURN_POINTER(bytes);
}


Datum
bson_object_agg_transition(PG_FUNCTION_ARGS)
{
	return AggregateObjectsCore(fcinfo);
}


/*
 * Merge objects transition function for pipelines without a sort spec.
 */
Datum
bson_merge_objects_transition_on_sorted(PG_FUNCTION_ARGS)
{
	pgbson *input = PG_GETARG_MAYBE_NULL_PGBSON(1);
	ValidateMergeObjectsInput(input);
	return AggregateObjectsCore(fcinfo);
}


/*
 * Merge objects transition function for pipelines that contain a sort spec.
 */
Datum
bson_merge_objects_transition(PG_FUNCTION_ARGS)
{
	bool isLast = false;
	bool isSingle = false;
	bool storeInputExpression = true;

	/* If there is a sort spec, we push it to the mergeObjects accumulator stage. */
	return BsonOrderTransition(fcinfo, isLast, isSingle, storeInputExpression);
}


/*
 * Merge objects final function for aggregation pipelines that contain a sort spec.
 */
Datum
bson_merge_objects_final(PG_FUNCTION_ARGS)
{
	BsonOrderAggState orderState = { 0 };
	BsonObjectAggState mergeObjectsState = { 0 };

	/*
	 * Here we initialize BsonObjectAggState.
	 * It is necessary to build the bson tree used by $mergeObjects.
	 */
	mergeObjectsState.currentSizeWritten = 0;
	mergeObjectsState.tree = MakeRootNode();
	mergeObjectsState.addEmptyPath = false;

	/* Deserializing the structure used to sort data. */
	DeserializeOrderState(PG_GETARG_BYTEA_P(0), &orderState);

	/* Preparing expressionData to evaluate expression against each sorted bson value. */
	pgbsonelement expressionElement;
	pgbson_writer writer;

	AggregationExpressionData expressionData;
	memset(&expressionData, 0, sizeof(AggregationExpressionData));
	ParseAggregationExpressionContext parseContext = { 0 };
	PgbsonToSinglePgbsonElement(orderState.inputExpression, &expressionElement);
	ParseAggregationExpressionData(&expressionData, &expressionElement.bsonValue,
								   &parseContext);
	const AggregationExpressionData *state = &expressionData;
	StringView path = {
		.length = expressionElement.pathLength,
		.string = expressionElement.path,
	};

	/* Populate tree with sorted documents. */
	for (int i = 0; i < orderState.currentCount; i++)
	{
		/* No more results*/
		if (orderState.currentResult[i] == NULL)
		{
			break;
		}

		/* Check for null value*/
		if (orderState.currentResult[i]->value != NULL)
		{
			PgbsonWriterInit(&writer);
			EvaluateAggregationExpressionDataToWriter(state,
													  orderState.currentResult[i]->value,
													  path, &writer,
													  NULL, false);

			pgbson *evaluatedDoc = PgbsonWriterGetPgbson(&writer);

			/* We need to validate the result here since we sorted the original documents first. */
			ValidateMergeObjectsInput(evaluatedDoc);

			/* Feed the tree with the evaluated bson. */
			CreateObjectAggTreeNodes(&mergeObjectsState,
									 evaluatedDoc);
		}
	}

	return ParseAndReturnMergeObjectsTree(&mergeObjectsState);
}


Datum
bson_object_agg_final(PG_FUNCTION_ARGS)
{
	bytea *currentArrayAgg = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	if (currentArrayAgg != NULL)
	{
		BsonObjectAggState *state = (BsonObjectAggState *) VARDATA_ANY(
			currentArrayAgg);
		return ParseAndReturnMergeObjectsTree(state);
	}
	else
	{
		PG_RETURN_POINTER(PgbsonInitEmpty());
	}
}


/*
 * Applies the "state transition" (SFUNC) for sum and average.
 * This counts the sum of the values encountered as well as the count
 * It ignores non-numeric values, and manages type upgrades and coercion
 * to the right types as documents are encountered.
 */
Datum
bson_sum_avg_transition(PG_FUNCTION_ARGS)
{
	bytea *bytes;
	BsonNumericAggState *currentState;

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0))
	{
		MemoryContext aggregateContext;
		if (!AggCheckCallContext(fcinfo, &aggregateContext))
		{
			ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
		}

		/* Create the aggregate state in the aggregate context. */
		MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

		bytes = AllocateBsonNumericAggState();

		currentState = (BsonNumericAggState *) VARDATA(bytes);
		currentState->count = 0;
		currentState->sum.value_type = BSON_TYPE_INT32;
		currentState->sum.value.v_int32 = 0;

		MemoryContextSwitchTo(oldContext);
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonNumericAggState *) VARDATA_ANY(bytes);
	}
	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue == NULL)
	{
		PG_RETURN_POINTER(bytes);
	}

	if (IsPgbsonEmptyDocument(currentValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);

	bool overflowedFromInt64Ignore = false;

	if (AddNumberToBsonValue(&currentState->sum, &currentValueElement.bsonValue,
							 &overflowedFromInt64Ignore))
	{
		currentState->count++;
	}

	PG_RETURN_POINTER(bytes);
}


/*
 * Applies the "inverse state transition" for sum and average.
 * This subtracts the sum of the values leaving the group and decrements the count
 * It ignores non-numeric values, and manages type upgrades and coercion
 * to the right types as documents are encountered.
 */
Datum
bson_sum_avg_minvtransition(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (AggCheckCallContext(fcinfo, &aggregateContext) != AGG_CONTEXT_WINDOW)
	{
		ereport(ERROR, errmsg(
					"window aggregate function called in non-window-aggregate context"));
	}

	bytea *bytes;
	BsonNumericAggState *currentState;

	if (PG_ARGISNULL(0))
	{
		/* Returning NULL is an indiacation that inverse can't be applied and the aggregation needs to be redone */
		PG_RETURN_NULL();
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonNumericAggState *) VARDATA_ANY(bytes);
	}
	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue == NULL || IsPgbsonEmptyDocument(currentValue))
	{
		PG_RETURN_POINTER(bytes);
	}

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);

	bool overflowedFromInt64Ignore = false;

	/* Aply the inverse of $sum and $avg */
	if (currentState->count > 0 &&
		SubtractNumberFromBsonValue(&currentState->sum, &currentValueElement.bsonValue,
									&overflowedFromInt64Ignore))
	{
		currentState->count--;
	}

	PG_RETURN_POINTER(bytes);
}


/*
 * Applies the "final calculation" (FINALFUNC) for sum.
 * This takes the final value created and outputs a bson "sum"
 * with the appropriate type.
 */
Datum
bson_sum_final(PG_FUNCTION_ARGS)
{
	bytea *currentSum = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (currentSum != NULL)
	{
		BsonNumericAggState *state = (BsonNumericAggState *) VARDATA_ANY(
			currentSum);
		finalValue.bsonValue = state->sum;
	}
	else
	{
		/* Mongo returns 0 for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_INT32;
		finalValue.bsonValue.value.v_int32 = 0;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "final calculation" (FINALFUNC) for average.
 * This takes the final value created and outputs a bson "average"
 */
Datum
bson_avg_final(PG_FUNCTION_ARGS)
{
	bytea *avgIntermediateState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbsonelement finalValue;
	finalValue.path = "";
	finalValue.pathLength = 0;
	if (avgIntermediateState != NULL)
	{
		BsonNumericAggState *averageState = (BsonNumericAggState *) VARDATA_ANY(
			avgIntermediateState);
		if (averageState->count == 0)
		{
			/* Mongo returns $null for empty sets */
			finalValue.bsonValue.value_type = BSON_TYPE_NULL;
		}
		else
		{
			double sum = BsonValueAsDouble(&averageState->sum);
			finalValue.bsonValue.value_type = BSON_TYPE_DOUBLE;
			finalValue.bsonValue.value.v_double = sum / averageState->count;
		}
	}
	else
	{
		/* Mongo returns $null for empty sets */
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
}


/*
 * Applies the "final calculation" (FINALFUNC) for min and max.
 * This takes the final value fills in a null bson for empty sets
 */
Datum
bson_min_max_final(PG_FUNCTION_ARGS)
{
	pgbson *current = PG_GETARG_MAYBE_NULL_PGBSON(0);

	if (current != NULL)
	{
		PG_RETURN_POINTER(current);
	}
	else
	{
		/* Mongo returns $null for empty sets */
		pgbsonelement finalValue;
		finalValue.path = "";
		finalValue.pathLength = 0;
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;

		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
}


/*
 * Applies the "state transition" (SFUNC) for max.
 * This returns the max value of the currently computed max
 * and the next candidate value.
 * if the current max is null, returns the next candidate value
 * If the candidate is null, returns the current max.
 */
Datum
bson_max_transition(PG_FUNCTION_ARGS)
{
	pgbson *left = PG_GETARG_MAYBE_NULL_PGBSON(0);
	pgbson *right = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (left == NULL)
	{
		if (right == NULL)
		{
			PG_RETURN_NULL();
		}

		PG_RETURN_POINTER(right);
	}
	else if (right == NULL)
	{
		PG_RETURN_POINTER(left);
	}

	int32_t compResult = ComparePgbson(left, right);
	if (compResult > 0)
	{
		PG_RETURN_POINTER(left);
	}

	PG_RETURN_POINTER(right);
}


/*
 * Applies the "state transition" (SFUNC) for min.
 * This returns the min value of the currently computed min
 * and the next candidate value.
 * if the current min is null, returns the next candidate value
 * If the candidate is null, returns the current min.
 */
Datum
bson_min_transition(PG_FUNCTION_ARGS)
{
	pgbson *left = PG_GETARG_MAYBE_NULL_PGBSON(0);
	pgbson *right = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (left == NULL)
	{
		if (right == NULL)
		{
			PG_RETURN_NULL();
		}

		PG_RETURN_POINTER(right);
	}
	else if (right == NULL)
	{
		PG_RETURN_POINTER(left);
	}

	int32_t compResult = ComparePgbson(left, right);
	if (compResult < 0)
	{
		PG_RETURN_POINTER(left);
	}

	PG_RETURN_POINTER(right);
}


/*
 * Applies the "combine function" (COMBINEFUNC) for sum and average.
 * takes two of the aggregate state structures (bson_numeric_agg_state)
 * and combines them to form a new bson_numeric_agg_state that has the combined
 * sum and count.
 */
Datum
bson_sum_avg_combine(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	bytea *combinedStateBytes = AllocateBsonNumericAggState();
	BsonNumericAggState *currentState = (BsonNumericAggState *) VARDATA_ANY(
		combinedStateBytes);

	MemoryContextSwitchTo(oldContext);

	/* Handle either left or right being null. A new state needs to be allocated regardless */
	currentState->count = 0;

	if (PG_ARGISNULL(0))
	{
		if (PG_ARGISNULL(1))
		{
			PG_RETURN_NULL();
		}
		memcpy(VARDATA(combinedStateBytes), VARDATA_ANY(PG_GETARG_BYTEA_P(1)),
			   sizeof(BsonNumericAggState));
	}
	else if (PG_ARGISNULL(1))
	{
		if (PG_ARGISNULL(0))
		{
			PG_RETURN_NULL();
		}
		memcpy(VARDATA(combinedStateBytes), VARDATA_ANY(PG_GETARG_BYTEA_P(0)),
			   sizeof(BsonNumericAggState));
	}
	else
	{
		BsonNumericAggState *leftState = (BsonNumericAggState *) VARDATA_ANY(
			PG_GETARG_BYTEA_P(0));
		BsonNumericAggState *rightState = (BsonNumericAggState *) VARDATA_ANY(
			PG_GETARG_BYTEA_P(1));

		currentState->count = leftState->count + rightState->count;
		currentState->sum = leftState->sum;

		bool overflowedFromInt64Ignore = false;

		AddNumberToBsonValue(&currentState->sum, &rightState->sum,
							 &overflowedFromInt64Ignore);
	}

	PG_RETURN_POINTER(combinedStateBytes);
}


/*
 * Applies the "combine function" (COMBINEFUNC) for min.
 * takes two bsons
 * makes a new bson equal to the minimum
 */
Datum
bson_min_combine(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	pgbson *left = PG_GETARG_MAYBE_NULL_PGBSON(0);
	pgbson *right = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *result;
	if (left == NULL)
	{
		if (right == NULL)
		{
			result = NULL;
		}
		else
		{
			result = PgbsonCloneFromPgbson(right);
		}
	}
	else if (right == NULL)
	{
		result = PgbsonCloneFromPgbson(left);
	}
	else
	{
		int32_t compResult = ComparePgbson(left, right);
		if (compResult < 0)
		{
			result = PgbsonCloneFromPgbson(left);
		}
		else
		{
			result = PgbsonCloneFromPgbson(right);
		}
	}

	MemoryContextSwitchTo(oldContext);

	if (result == NULL)
	{
		PG_RETURN_NULL();
	}

	PG_RETURN_POINTER(result);
}


/*
 * Applies the "combine function" (COMBINEFUNC) for max.
 * takes two bsons
 * makes a new bson equal to the maximum
 */
Datum
bson_max_combine(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	pgbson *left = PG_GETARG_MAYBE_NULL_PGBSON(0);
	pgbson *right = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *result;
	if (left == NULL)
	{
		if (right == NULL)
		{
			result = NULL;
		}
		else
		{
			result = PgbsonCloneFromPgbson(right);
		}
	}
	else if (right == NULL)
	{
		result = PgbsonCloneFromPgbson(left);
	}
	else
	{
		int32_t compResult = ComparePgbson(left, right);
		if (compResult > 0)
		{
			result = PgbsonCloneFromPgbson(left);
		}
		else
		{
			result = PgbsonCloneFromPgbson(right);
		}
	}

	MemoryContextSwitchTo(oldContext);

	if (result == NULL)
	{
		PG_RETURN_NULL();
	}

	PG_RETURN_POINTER(result);
}


/*
 * Builds the final distinct response to be sent to the client.
 * Formats the response as
 * { "value": [ array_elements ], "ok": 1 }
 * This allows the gateway to serialize the response directly to the client
 * without reconverting the response on the Gateway.
 */
Datum
bson_build_distinct_response(PG_FUNCTION_ARGS)
{
	ArrayType *val_array = PG_GETARG_ARRAYTYPE_P(0);

	Datum *val_datums;
	bool *val_is_null_marker;
	int val_count;

	deconstruct_array(val_array,
					  ARR_ELEMTYPE(val_array), -1, false, TYPALIGN_INT,
					  &val_datums, &val_is_null_marker, &val_count);

	/* Distinct never has SQL NULL in the array */
	pfree(val_is_null_marker);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "values", 6, &arrayWriter);
	for (int i = 0; i < val_count; i++)
	{
		pgbsonelement singleElement;
		PgbsonToSinglePgbsonElement((pgbson *) val_datums[i], &singleElement);
		PgbsonArrayWriterWriteValue(&arrayWriter, &singleElement.bsonValue);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);

	PgbsonWriterAppendDouble(&writer, "ok", 2, 1);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Transition function for the BSON_ADD_TO_SET aggregate.
 */
Datum
bson_add_to_set_transition(PG_FUNCTION_ARGS)
{
	BsonAddToSetState *currentState = { 0 };
	bytea *bytes;
	MemoryContext aggregateContext;
	int aggregationContext = AggCheckCallContext(fcinfo, &aggregateContext);
	if (aggregationContext == 0)
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

	bool isWindowAggregation = aggregationContext == AGG_CONTEXT_WINDOW;

	/* Create the aggregate state in the aggregate context. */
	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0)) /* First arg is the running aggregated state*/
	{
		int bson_size = sizeof(BsonAddToSetState) + VARHDRSZ;
		bytea *combinedStateBytes = (bytea *) palloc0(bson_size);
		SET_VARSIZE(combinedStateBytes, bson_size);
		bytes = combinedStateBytes;

		currentState = (BsonAddToSetState *) VARDATA(bytes);
		currentState->currentSizeWritten = 0;
		currentState->set = CreateBsonValueHashSet();
		currentState->isWindowAggregation = isWindowAggregation;
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonAddToSetState *) VARDATA_ANY(bytes);
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (currentValue != NULL && !IsPgbsonEmptyDocument(currentValue))
	{
		CheckAggregateIntermediateResultSize(currentState->currentSizeWritten +
											 PgbsonGetBsonSize(currentValue));

		/*
		 * We need to copy the whole pgbson because otherwise the pointers we store
		 * in the hash table will reference an address in the stack. These are released
		 * after the function resolves and will point to garbage.
		 */
		currentValue = PgbsonCloneFromPgbson(currentValue);
		pgbsonelement singleBsonElement;

		/* If it's a bson that's { "": value } */
		if (TryGetSinglePgbsonElementFromPgbson(currentValue, &singleBsonElement) &&
			singleBsonElement.pathLength == 0)
		{
			bool found = false;
			hash_search(currentState->set, &singleBsonElement.bsonValue,
						HASH_ENTER, &found);

			/*
			 * If the BSON was not found in the hash table, add its size to the current
			 * state object.
			 */
			if (!found)
			{
				currentState->currentSizeWritten += PgbsonGetBsonSize(currentValue);
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg("Bad input format for addToSet transition.")));
		}
	}

	MemoryContextSwitchTo(oldContext);
	PG_RETURN_POINTER(bytes);
}


/*
 * Final function for the BSON_ADD_TO_SET aggregate.
 */
Datum
bson_add_to_set_final(PG_FUNCTION_ARGS)
{
	bytea *currentState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);
	if (currentState != NULL)
	{
		BsonAddToSetState *state = (BsonAddToSetState *) VARDATA_ANY(
			currentState);

		HASH_SEQ_STATUS seq_status;
		const bson_value_t *entry;
		hash_seq_init(&seq_status, state->set);

		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		pgbson_array_writer arrayWriter;
		PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

		while ((entry = hash_seq_search(&seq_status)) != NULL)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, entry);
		}

		/*
		 * For window aggregation, with the HASHTBL destroyed (on the call for the first group),
		 * subsequent calls to this final function for other groups will fail
		 * for certain bounds such as ["unbounded", constant].
		 * This is because the head never moves and the aggregation is not restarted.
		 * Thus, the table is expected to hold something valid.
		 */
		if (!state->isWindowAggregation)
		{
			hash_destroy(state->set);
		}

		PgbsonWriterEndArray(&writer, &arrayWriter);

		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
	}
	else
	{
		MemoryContext aggregateContext;
		int aggContext = AggCheckCallContext(fcinfo, &aggregateContext);
		if (aggContext == AGG_CONTEXT_WINDOW)
		{
			/*
			 * We will need to return the default value of $addToSet accumulator which is empty array in case
			 * where the window doesn't select any document.
			 *
			 * e.g ["unbounded", -1] => For the first row it doesn't select any rows.
			 */
			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendEmptyArray(&writer, "", 0);
			PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
		}
		PG_RETURN_NULL();
	}
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

bytea *
AllocateBsonNumericAggState()
{
	int bson_size = sizeof(BsonNumericAggState) + VARHDRSZ;
	bytea *combinedStateBytes = (bytea *) palloc0(bson_size);
	SET_VARSIZE(combinedStateBytes, bson_size);

	return combinedStateBytes;
}


void
CheckAggregateIntermediateResultSize(uint32_t size)
{
	if (size > BSON_MAX_ALLOWED_SIZE_INTERMEDIATE)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERMEDIATERESULTTOOLARGE),
						errmsg(
							"Size %u is larger than maximum size allowed for an intermediate document %u",
							size, BSON_MAX_ALLOWED_SIZE_INTERMEDIATE)));
	}
}


/*
 *  This is the format we that native mongo uses to generate
 *  the staging collection during execution of $out.
 *  db.tmp.agg_out.09237ebf-8897-4e52-8760-2e6cfce6a671
 */
static void
GenerateUnqiqueStagingCollectionNameForOut(char *collectionName)
{
	strcpy(collectionName, "tmp.agg_out.");
	srand(time(NULL));

	/* Generate a random 32-character string */
	for (int i = 12; i < 44; ++i)
	{
		if (i == 20 || i == 25 || i == 30 || i == 35)
		{
			collectionName[i] = '-';
		}
		else
		{
			collectionName[i] = charset[rand() % (sizeof(charset) - 1)];
		}
	}
	collectionName[44] = '\0';
}


/*
 * Helper method that iterates a pgbson writing its values to a bson tree. If a key already
 * exists in the tree, then it's overwritten.
 */
static void
CreateObjectAggTreeNodes(BsonObjectAggState *currentState, pgbson *currentValue)
{
	bson_iter_t docIter;
	pgbsonelement singleBsonElement;
	bool treatLeafDataAsConstant = true;
	ParseAggregationExpressionContext parseContext = { 0 };

	/*
	 * If currentValue has the form of { "": value } and value is a bson document,
	 * write only the value in the BsonTree. We need this because of how accumulators work
	 * with bson_repath_and_build.
	 */
	if (TryGetSinglePgbsonElementFromPgbson(currentValue, &singleBsonElement) &&
		singleBsonElement.pathLength == 0 &&
		singleBsonElement.bsonValue.value_type == BSON_TYPE_DOCUMENT)
	{
		BsonValueInitIterator(&singleBsonElement.bsonValue, &docIter);
		currentState->addEmptyPath = true;
	}
	else
	{
		PgbsonInitIterator(currentValue, &docIter);
	}

	while (bson_iter_next(&docIter))
	{
		StringView pathView = bson_iter_key_string_view(&docIter);
		const bson_value_t *docValue = bson_iter_value(&docIter);

		bool nodeCreated = false;
		const BsonLeafPathNode *treeNode = TraverseDottedPathAndGetOrAddLeafFieldNode(
			&pathView, docValue,
			currentState->tree, BsonDefaultCreateLeafNode,
			treatLeafDataAsConstant, &nodeCreated, &parseContext);

		/* If the node already exists we need to update the value as object agg and merge objects
		 * have the behavior that the last path spec (if duplicate) takes precedence. */
		if (!nodeCreated)
		{
			ResetNodeWithField(treeNode, NULL, docValue, BsonDefaultCreateLeafNode,
							   treatLeafDataAsConstant, &parseContext);
		}
	}
}


/*
 * Validates $mergeObject input. It must be a non-null document.
 */
static void
ValidateMergeObjectsInput(pgbson *input)
{
	pgbsonelement singleBsonElement;

	/*
	 * The $mergeObjects accumulator expects a document in the form of
	 * { "": <document> }. This required by the bson_repath_and_build function.
	 * Hence we check for a document with a single element below.
	 */
	if (input == NULL || !TryGetSinglePgbsonElementFromPgbson(input, &singleBsonElement))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("Bad input format for mergeObjects transition.")));
	}


	/*
	 * We fail if the bson value type is not DOCUMENT or NULL.
	 */
	if (singleBsonElement.bsonValue.value_type != BSON_TYPE_DOCUMENT &&
		singleBsonElement.bsonValue.value_type != BSON_TYPE_NULL)
	{
		ereport(ERROR,
				errcode(ERRCODE_HELIO_DOLLARMERGEOBJECTSINVALIDTYPE),
				errmsg("$mergeObjects requires object inputs, but input %s is of type %s",
					   BsonValueToJsonForLogging(&singleBsonElement.bsonValue),
					   BsonTypeName(singleBsonElement.bsonValue.value_type)),
				errdetail_log(
					"$mergeObjects requires object inputs, but input is of type %s",
					BsonTypeName(singleBsonElement.bsonValue.value_type)));
	}
}


/*
 * Function used to parse and return a mergeObjects tree.
 */
static Datum
ParseAndReturnMergeObjectsTree(BsonObjectAggState *state)
{
	if (state != NULL)
	{
		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		/*
		 * If we removed the original empty path, then we need to include it
		 * again for bson_repath_and_build.
		 */
		if (state->addEmptyPath)
		{
			pgbson_writer childWriter;
			PgbsonWriterStartDocument(&writer, "", 0, &childWriter);
			TraverseTreeAndWrite(state->tree, &childWriter, NULL);
			PgbsonWriterEndDocument(&writer, &childWriter);
		}
		else
		{
			TraverseTreeAndWrite(state->tree, &writer, NULL);
		}


		pgbson *result = PgbsonWriterGetPgbson(&writer);
		FreeTree(state->tree);

		PG_RETURN_POINTER(result);
	}
	else
	{
		PG_RETURN_POINTER(PgbsonInitEmpty());
	}
}


/*
 * Comparator function for heap utils. For MaxN, we need to build min-heap
 */
static bool
HeapSortComparatorMaxN(const void *first, const void *second)
{
	bool ignoreIsComparisonValid = false; /* IsComparable ensures this is taken care of */
	return CompareBsonValueAndType((const bson_value_t *) first,
								   (const bson_value_t *) second,
								   &ignoreIsComparisonValid) < 0;
}


/*
 * Comparator function for heap utils. For MinN, we need to build max-heap
 */
static bool
HeapSortComparatorMinN(const void *first, const void *second)
{
	bool ignoreIsComparisonValid = false; /* IsComparable ensures this is taken care of */
	return CompareBsonValueAndType((const bson_value_t *) first,
								   (const bson_value_t *) second,
								   &ignoreIsComparisonValid) > 0;
}


/*
 * Applies the "state transition" (SFUNC) for maxN/minN accumulators.
 * The args in PG_FUNCTION_ARGS:
 *		Evaluated expression: input and N.
 *
 * For maxN:
 * we need to maintain a small root heap and compare the current value with the top of the heap (minimum value).
 * If the current value is greater than the top of the heap (minimum value), then we will pop the top of the heap and insert the current value.
 *
 * For minN:
 * we need to maintain a big root heap and compare the current value with the top of the heap (minimum value).
 * If the current value is less than the top of the heap (minimum value), then we will pop the top of the heap and insert the current value.
 */
Datum
bson_maxminn_transition(PG_FUNCTION_ARGS, bool isMaxN)
{
	bytea *bytes = NULL;
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg(
					"aggregate function %s transition called in non-aggregate context",
					isMaxN ? "maxN" : "minN"));
	}

	/* Create the aggregate state in the aggregate context. */
	/* MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext); */

	pgbson *copiedPgbson = PG_GETARG_MAYBE_NULL_PGBSON(1);
	pgbson *currentValue = CopyPgbsonIntoMemoryContext(copiedPgbson, aggregateContext);

	pgbsonelement currentValueElement;
	PgbsonToSinglePgbsonElement(currentValue, &currentValueElement);
	bson_value_t currentBsonValue = currentValueElement.bsonValue;

	/*input and N are both expression, so we evaluate them togather.*/
	bson_iter_t docIter;
	BsonValueInitIterator(&currentBsonValue, &docIter);
	bson_value_t inputBsonValue = { 0 };
	bson_value_t elementBsonValue = { 0 };
	while (bson_iter_next(&docIter))
	{
		const char *key = bson_iter_key(&docIter);
		if (strcmp(key, "input") == 0)
		{
			inputBsonValue = *bson_iter_value(&docIter);
		}
		else if (strcmp(key, "n") == 0)
		{
			elementBsonValue = *bson_iter_value(&docIter);
		}
	}

	/* Verify that N is an integer. */
	ValidateElementForNGroupAccumulators(&elementBsonValue, isMaxN == true ? "maxN" :
										 "minN");
	int element = BsonValueAsInt32(&elementBsonValue);

	BinaryHeapState *currentState = (BinaryHeapState *) palloc0(sizeof(BinaryHeapState));

	/* If the intermediate state has never been initialized, create it */
	if (PG_ARGISNULL(0))
	{
		currentState->isMaxN = isMaxN;

		/*
		 * For maxN, we need to maintain a small root heap.
		 * When currentValue is greater than the top of the heap, we need to remove the top of the heap and insert currentValue.
		 *
		 * For minN, we need to maintain a large root heap.
		 * When currentValue is less than the top of the heap, we need to remove the top of the heap and insert currentValue.
		 */
		currentState->heap = AllocateHeap(element, isMaxN == true ?
										  HeapSortComparatorMaxN :
										  HeapSortComparatorMinN);
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		DeserializeBinaryHeapState(bytes, currentState);
	}

	/*if the input is null or an undefined path, ignore it */
	if (!IsExpressionResultNullOrUndefined(&inputBsonValue))
	{
		if (currentState->heap->heapSize < element)
		{
			/* Heap is not full, insert value. */
			PushToHeap(currentState->heap, &inputBsonValue);
		}
		else
		{
			/* Heap is full, replace the top if the new value should be included instead */
			bson_value_t topHeap = TopHeap(currentState->heap);

			if (!currentState->heap->heapComparator(&inputBsonValue, &topHeap))
			{
				PopFromHeap(currentState->heap);
				PushToHeap(currentState->heap, &inputBsonValue);
			}
		}
	}

	bytes = SerializeBinaryHeapState(aggregateContext, currentState, PG_ARGISNULL(0) ?
									 NULL : bytes);

	PG_RETURN_POINTER(bytes);
}


/*
 * Converts a BinaryHeapState into a serialized form to allow the internal type to be bytea
 * Resulting bytes look like:
 * | Varlena Header | isMaxN | heapSize | heapSpace | heapNode * heapSpace |
 */
bytea *
SerializeBinaryHeapState(MemoryContext aggregateContext,
						 BinaryHeapState *state,
						 bytea *byteArray)
{
	int heapNodesSize = 0;
	pgbson **heapNodeList = NULL;

	if (state->heap->heapSize > 0)
	{
		heapNodeList = (pgbson **) palloc(sizeof(pgbson *) * state->heap->heapSize);
		for (int i = 0; i < state->heap->heapSize; i++)
		{
			heapNodeList[i] = BsonValueToDocumentPgbson(&state->heap->heapNodes[i]);

			pgbsonelement element;
			PgbsonToSinglePgbsonElement(heapNodeList[i], &element);

			heapNodesSize += VARSIZE(heapNodeList[i]);
		}
	}

	int requiredByteSize = VARHDRSZ +
						   sizeof(bool) +
						   sizeof(int64) +
						   sizeof(int64) +
						   heapNodesSize;

	char *bytes;
	int existingByteSize = (byteArray == NULL) ? 0 : VARSIZE(byteArray);

	if (existingByteSize >= requiredByteSize)
	{
		/* Reuse existing bytes */
		bytes = (char *) byteArray;
	}
	else
	{
		bytes = (char *) MemoryContextAlloc(aggregateContext, requiredByteSize);
		SET_VARSIZE(bytes, requiredByteSize);
	}

	/* Copy in the currentValue */
	char *byteAllocationPointer = (char *) VARDATA(bytes);

	*((bool *) (byteAllocationPointer)) = state->isMaxN;
	byteAllocationPointer += sizeof(bool);

	*((int64 *) (byteAllocationPointer)) = state->heap->heapSize;
	byteAllocationPointer += sizeof(int64);

	*((int64 *) (byteAllocationPointer)) = state->heap->heapSpace;
	byteAllocationPointer += sizeof(int64);

	if (state->heap->heapSize > 0)
	{
		for (int i = 0; i < state->heap->heapSize; i++)
		{
			memcpy(byteAllocationPointer, heapNodeList[i], VARSIZE(heapNodeList[i]));
			byteAllocationPointer += VARSIZE(heapNodeList[i]);
		}
	}

	return (bytea *) bytes;
}


/*
 * Converts a BinaryHeapState from a serialized form to allow the internal type to be bytea
 * Incoming bytes look like:
 * | Varlena Header | isMaxN | heapSize | heapSpace | heapNode * heapSpace |
 */
void
DeserializeBinaryHeapState(bytea *byteArray,
						   BinaryHeapState *state)
{
	if (byteArray == NULL)
	{
		return;
	}

	char *bytes = (char *) VARDATA(byteArray);

	state->isMaxN = *(bool *) (bytes);
	bytes += sizeof(bool);

	int64 heapSize = *(int64 *) (bytes);
	bytes += sizeof(int64);

	int64 heapSpace = *(int64 *) (bytes);
	bytes += sizeof(int64);

	state->heap = (BinaryHeap *) palloc(sizeof(BinaryHeap));
	state->heap->heapSize = heapSize;
	state->heap->heapSpace = heapSpace;
	state->heap->heapNodes = (bson_value_t *) palloc(sizeof(bson_value_t) * heapSpace);

	if (state->heap->heapSize > 0)
	{
		for (int i = 0; i < state->heap->heapSize; i++)
		{
			pgbson *pgbsonValue = (pgbson *) bytes;
			bytes += VARSIZE(pgbsonValue);

			pgbsonelement element;
			PgbsonToSinglePgbsonElement(pgbsonValue, &element);
			state->heap->heapNodes[i] = element.bsonValue;
		}
	}
	state->heap->heapComparator = state->isMaxN ? HeapSortComparatorMaxN :
								  HeapSortComparatorMinN;
}


/*
 * Applies the "final" (FINALFUNC) for maxN/minN.
 * This takes the final value created and outputs a bson "maxN/minN"
 * with the appropriate type.
 */
Datum
bson_maxminn_final(PG_FUNCTION_ARGS)
{
	bytea *maxNIntermediateState = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	pgbson *finalPgbson = NULL;

	pgbson_writer writer;
	pgbson_array_writer arrayWriter;
	PgbsonWriterInit(&writer);
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	if (maxNIntermediateState != NULL)
	{
		BinaryHeapState *maxNState = (BinaryHeapState *) palloc(sizeof(BinaryHeapState));

		DeserializeBinaryHeapState(maxNIntermediateState, maxNState);

		int64_t numEntries = maxNState->heap->heapSize;
		bson_value_t *valueArray = (bson_value_t *) palloc(sizeof(bson_value_t) *
														   numEntries);

		while (maxNState->heap->heapSize > 0)
		{
			valueArray[maxNState->heap->heapSize - 1] = PopFromHeap(maxNState->heap);
		}

		for (int64_t i = 0; i < numEntries; i++)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, &valueArray[i]);
		}

		PgbsonWriterEndArray(&writer, &arrayWriter);
		finalPgbson = PgbsonWriterGetPgbson(&writer);

		pfree(valueArray);
		FreeHeap(maxNState->heap);
	}

	PG_RETURN_POINTER(finalPgbson);
}


/*
 * Applies the "state transition" (SFUNC) for maxN.
 * For maxN, we need to maintain a small root heap.
 * When currentValue is greater than the top of the heap, we need to remove the top of the heap and insert currentValue.
 */
Datum
bson_maxn_transition(PG_FUNCTION_ARGS)
{
	bool isMaxN = true;
	return bson_maxminn_transition(fcinfo, isMaxN);
}


/*
 * Applies the "state transition" (SFUNC) for minN.
 * For minN, we need to maintain a large root heap.
 * When currentValue is less than the top of the heap, we need to remove the top of the heap and insert currentValue.
 */
Datum
bson_minn_transition(PG_FUNCTION_ARGS)
{
	bool isMaxN = false;
	return bson_maxminn_transition(fcinfo, isMaxN);
}


/*
 * Applies the "combine" (COMBINEFUNC) for maxN/minN.
 */
Datum
bson_maxminn_combine(PG_FUNCTION_ARGS)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg(
					"aggregate function maxN/minN combine called in non-aggregate context"));
	}

	if (PG_ARGISNULL(0))
	{
		return PG_GETARG_DATUM(1);
	}

	if (PG_ARGISNULL(1))
	{
		return PG_GETARG_DATUM(0);
	}

	bytea *bytesLeft;
	bytea *bytesRight;
	BinaryHeapState *currentLeftState = (BinaryHeapState *) palloc(
		sizeof(BinaryHeapState));
	BinaryHeapState *currentRightState = (BinaryHeapState *) palloc(
		sizeof(BinaryHeapState));

	bytesLeft = PG_GETARG_BYTEA_P(0);
	DeserializeBinaryHeapState(bytesLeft, currentLeftState);

	bytesRight = PG_GETARG_BYTEA_P(1);
	DeserializeBinaryHeapState(bytesRight, currentRightState);


	/* Merge the left heap into the currentRightState heap. */
	while (currentLeftState->heap->heapSize > 0)
	{
		bson_value_t leftBsonValue = TopHeap(currentLeftState->heap);
		bson_value_t rightBsonValue = TopHeap(currentRightState->heap);

		/*
		 * For maxN, If the root of the left heap is greater than the root of the currentState heap,
		 * remove the root of the currentState heap and insert the root of the left heap.
		 *
		 * For minN, If the root of the left heap is less than the root of the currentState heap,
		 * remove the root of the currentState heap and insert the root of the left heap.
		 *
		 */
		if (currentRightState->heap->heapSize < currentRightState->heap->heapSpace)
		{
			PushToHeap(currentRightState->heap, &leftBsonValue);
		}
		else if (!currentLeftState->heap->heapComparator(&leftBsonValue, &rightBsonValue))
		{
			PopFromHeap(currentRightState->heap);
			PushToHeap(currentRightState->heap, &leftBsonValue);
		}
		PopFromHeap(currentLeftState->heap);
	}
	FreeHeap(currentLeftState->heap);

	bytesRight = SerializeBinaryHeapState(aggregateContext, currentRightState,
										  bytesRight);
	PG_RETURN_POINTER(bytesRight);
}
