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
#include "utils/mongo_errors.h"
#include "metadata/collection.h"
#include "commands/insert.h"
#include "sharding/sharding.h"
#include "utils/hashset_utils.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

typedef struct BsonNumericAggState
{
	bson_value_t sum;
	int64_t count;
} BsonNumericAggState;

typedef struct BsonArrayAggState
{
	pgbson_writer writer;
	pgbson_array_writer arrayWriter;
	int64_t currentSizeWritten;
} BsonArrayAggState;

typedef struct BsonObjectAggState
{
	pgbson_writer writer;
	int64_t currentSizeWritten;
} BsonObjectAggState;

typedef struct BsonAddToSetState
{
	HTAB *set;
	int64_t currentSizeWritten;
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

const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static bytea * AllocateBsonNumericAggState(void);
static void CheckAggregateIntermediateResultSize(uint32_t size);
static void GenerateUnqiqueStagingCollectionNameForOut(char *collectionName);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_sum_avg_transition);
PG_FUNCTION_INFO_V1(bson_sum_final);
PG_FUNCTION_INFO_V1(bson_avg_final);
PG_FUNCTION_INFO_V1(bson_sum_avg_combine);
PG_FUNCTION_INFO_V1(bson_min_transition);
PG_FUNCTION_INFO_V1(bson_max_transition);
PG_FUNCTION_INFO_V1(bson_min_max_final);
PG_FUNCTION_INFO_V1(bson_min_combine);
PG_FUNCTION_INFO_V1(bson_max_combine);
PG_FUNCTION_INFO_V1(bson_build_distinct_response);
PG_FUNCTION_INFO_V1(bson_array_agg_transition);
PG_FUNCTION_INFO_V1(bson_array_agg_final);
PG_FUNCTION_INFO_V1(bson_distinct_array_agg_transition);
PG_FUNCTION_INFO_V1(bson_distinct_array_agg_final);
PG_FUNCTION_INFO_V1(bson_object_agg_transition);
PG_FUNCTION_INFO_V1(bson_object_agg_final);
PG_FUNCTION_INFO_V1(bson_out_transition);
PG_FUNCTION_INFO_V1(bson_out_final);
PG_FUNCTION_INFO_V1(bson_add_to_set_transition);
PG_FUNCTION_INFO_V1(bson_add_to_set_final);

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
				ereport(ERROR, (errcode(MongoLocation28769),
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
				ereport(ERROR, (errcode(MongoLocation28769),
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
				ereport(ERROR, (errcode(MongoDollarOutTempCollectionCantBeCreated),
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
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

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

		PgbsonWriterInit(&currentState->writer);
		currentState->currentSizeWritten = 0;
		PgbsonWriterStartArray(&currentState->writer, path, strlen(path),
							   &currentState->arrayWriter);
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonArrayAggState *) VARDATA_ANY(bytes);
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue == NULL)
	{
		PgbsonArrayWriterWriteNull(&currentState->arrayWriter);
	}
	else
	{
		CheckAggregateIntermediateResultSize(currentState->currentSizeWritten +
											 PgbsonGetBsonSize(currentValue));

		pgbsonelement singleBsonElement;
		if (handleSingleValueElement &&
			TryGetSinglePgbsonElementFromPgbson(currentValue, &singleBsonElement) &&
			singleBsonElement.pathLength == 0)
		{
			/* If it's a bson that's { "": value } */
			PgbsonArrayWriterWriteValue(&currentState->arrayWriter,
										&singleBsonElement.bsonValue);
		}
		else
		{
			PgbsonArrayWriterWriteDocument(&currentState->arrayWriter, currentValue);
		}

		currentState->currentSizeWritten += PgbsonGetBsonSize(currentValue);
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
		PgbsonWriterEndArray(&state->writer, &state->arrayWriter);

		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&state->writer));
	}
	else
	{
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
		PgbsonWriterEndArray(&state->writer, &state->arrayWriter);

		PgbsonWriterAppendDouble(&state->writer, "ok", 2, 1);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&state->writer));
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


Datum
bson_object_agg_transition(PG_FUNCTION_ARGS)
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
		PgbsonWriterInit(&currentState->writer);
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonObjectAggState *) VARDATA_ANY(bytes);
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);

	if (currentValue != NULL)
	{
		bson_iter_t pathSpecIter;
		PgbsonInitIterator(currentValue, &pathSpecIter);
		while (bson_iter_next(&pathSpecIter))
		{
			const char *path = bson_iter_key(&pathSpecIter);
			CheckAggregateIntermediateResultSize(currentState->currentSizeWritten +
												 strlen(
													 path) * 8);

			PgbsonWriterAppendValue(&currentState->writer, path, strlen(path),
									bson_iter_value(
										&pathSpecIter));

			currentState->currentSizeWritten += strlen(path) * 8;
		}
	}

	/* Null is ignored */

	MemoryContextSwitchTo(oldContext);

	PG_RETURN_POINTER(bytes);
}


Datum
bson_object_agg_final(PG_FUNCTION_ARGS)
{
	bytea *currentArrayAgg = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);

	if (currentArrayAgg != NULL)
	{
		BsonObjectAggState *state = (BsonObjectAggState *) VARDATA_ANY(
			currentArrayAgg);

		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&state->writer));
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
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, errmsg("aggregate function called in non-aggregate context"));
	}

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
	}
	else
	{
		bytes = PG_GETARG_BYTEA_P(0);
		currentState = (BsonAddToSetState *) VARDATA_ANY(bytes);
	}

	pgbson *currentValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	if (currentValue != NULL)
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
			ereport(ERROR, (errcode(MongoInternalError),
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

		hash_destroy(state->set);
		PgbsonWriterEndArray(&writer, &arrayWriter);

		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
	}
	else
	{
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
		ereport(ERROR, (errcode(MongoIntermediateResultTooLarge),
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
