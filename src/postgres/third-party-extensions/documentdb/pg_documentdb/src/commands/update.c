/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/update.c
 *
 * Implementation of the update operation.
 *
 * We have 3 ways of doing an Update:
 *
 * 1) UpdateAllMatchingDocuments is used for multi:true scenarios and does a
 *    regular UPDATE across shards or with a shard_key_value filter. For
 *    unsharded collections we use shard_key_value = 0.
 *
 *    We perform a separate INSERT in case of upsert:true when the UPDATE
 *    matches 0 rows.
 *
 * 2) UpdateOne is used for multi:false scenarios and calls the update_one
 *    UDF, which can potentially get delegated to the worker nodes that
 *    stores the shard_key_value. In case of an unsharded collection it
 *    is called with shard_key_value 0.
 *
 *    update_one does retryable write bookeeping on the shard to skip retries
 *    and also has logic to deal with multi:false and shard key value updates.
 *
 *    The update_one UDF first does a `SELECT ctid, bson_update_document(..) ..
 *    LIMIT 1 FOR UPDATE` to find exactly 1 document that matches the query
 *    and lock the document. Since the SELECT already returns the updated
 *    document, we can then compute the new shard key value. If it is the same
 *    as the original shard key value, we perform an update on the TID (tuple
 *    identifier) using UpdateDocumentByTID. Otherwise, we perform a delete
 *    on the TID using DeleteDocumentByTID. These operations are fast since
 *    the TID points directly to the right page and tuple index.
 *
 *    If the document was deleted, update_one sets "o_reinsert_document".
 *    UpdateOne then calls InsertDocument to re-insert the document with
 *    its new shard key value.
 *
 *    We perform an INSERT within update_one in case of upsert:true when the
 *    SELECT .. FOR UPDATE matches 0 rows. If the shard key value changes,
 *    we use o_reinsert_document to perform the insert via coordinator.
 *
 * 3) UpdateOneObjectId is used for multi:false scenarios involving an _id
 *    equals query on a sharded collection that is not sharded by _id.
 *    Since we do not know where the _id lives, and because there could be
 *    multiple documents with the same _id as long as they have different
 *    shard key values, we first do a regular SELECT to find a document
 *    with the given _id.
 *
 *    If found, we use the update_one UDF to update the document. However,
 *    since we do a regular SELECT the document can change/disappear before
 *    we manage to update it. Therefore, we repeat SELECT+update_one several
 *    times until we successfully updated a document.
 *
 *    To handle retryable writes, we look across all shards in the retry
 *    records table for the transaction ID before starting the process.
 *
 *    Upserts in this scenario are not supported currently.
 *
 *    Overall, updates in this scenario are very expensive and best avoided,
 *    but provided for compatibility.
 *
 * Batch updates are handled by ProcessBatchUpdate. Each update runs in
 * a subtransaction to be able to continue when the batch specifies
 * ordered:false and to return separate errors for each failed update.
 * We do not yet support retryable writes in a batch update.
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "access/xact.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/typcache.h"

#include "io/bson_core.h"
#include "aggregation/bson_project.h"
#include "aggregation/bson_query.h"
#include "update/bson_update.h"
#include "commands/commands_common.h"
#include "commands/insert.h"
#include "commands/parse_error.h"
#include "commands/update.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "infrastructure/documentdb_plan_cache.h"
#include "query/query_operator.h"
#include "sharding/sharding.h"
#include "commands/retryable_writes.h"
#include "io/pgbsonsequence.h"
#include "utils/error_utils.h"
#include "utils/feature_counter.h"
#include "utils/query_utils.h"
#include "utils/version_utils.h"
#include "schema_validation/schema_validation.h"

#include "api_hooks.h"

/* YB includes */
#include "pg_yb_utils.h"

/* from tid.c */
#define DatumGetItemPointer(X) ((ItemPointer) DatumGetPointer(X))
#define ItemPointerGetDatum(X) PointerGetDatum(X)
#define NeedExistingDocForValidation(state, collection) \
	((state != NULL) && \
	 (collection->schemaValidator.validationLevel == ValidationLevel_Moderate))

extern int NumBsonDocumentsUpdated;

/* This guc is temporary and is used to handle whether the parameter “bypassDocumentValidation” could be set in the request command.*/
extern bool EnableBypassDocumentValidation;
extern bool EnableSchemaValidation;

/*
 * UpdateSpec describes a single update operation.
 */
typedef struct
{
	UpdateOneParams updateOneParams;

	/* whether to update multiple documents */
	int isMulti;
} UpdateSpec;


/*
 * UpdateCandidate represents an update candidate (might
 * turn into a delete if shard key value changes).
 */
typedef struct
{
	/* TID of the document */
	ItemPointer tid;

	/* object ID of the document */
	Datum objectId;

	/* old value of the document */
	Datum originalDocument;

	/* updated value of the document */
	Datum updatedDocument;

	/* ybctid of the document */
	Datum ybctid;
} UpdateCandidate;


/*
 * UpdateResult reflects the result of a single update command.
 */
typedef struct
{
	/* number of rows modified */
	uint64 rowsModified;

	/* number of rows that matched the query for update */
	uint64 rowsMatched;

	/* whether an upsert was performed as part of the update */
	bool performedUpsert;

	/* in case of an upsert, the object ID that was inserted */
	pgbson *upsertedObjectId;
} UpdateResult;


/*
 * UpsertResult represents an upsert result that is added to the
 * final output.
 */
typedef struct
{
	/* index of the update that resulted in an upsert */
	int index;

	/* object ID that was inserted */
	pgbson *objectId;
} UpsertResult;

/*
 * UpdateAllMAtchingDocsResult represents the result of updating
 * multiple documents in a single operation.
 */
typedef struct
{
	/* the number of documents that were actually updated */
	uint64 rowsUpdated;

	/* the number of documents that matched the query
	 * this can be greater than rowsUpdated if more docs
	 * matched the query, but weren't affected by the update */
	uint64 matchedDocs;
} UpdateAllMatchingDocsResult;


/*
 * BatchUpdateSpec describes a batch of update operations.
 */
typedef struct
{
	/* collection in which to perform updates */
	char *collectionName;

	/* A pointer to the update value in the updateSpec */
	bson_value_t updateValue;

	/* A pointer to the bson sequence containing updates */
	pgbsonsequence *updateSequence;

	/* UpdateSpec list describing the updates (processed from one of the above) */
	List *updates;

	/* if ordered, stop after the first failure */
	bool isOrdered;

	/* whether any of the updates have upsert:true */
	bool hasUpsert;

	/* The optional shard level table OID for the update */
	Oid shardTableOid;

	/* False by default. It can be set to true in request command. */
	bool bypassDocumentValidation;
} BatchUpdateSpec;


/*
 * BatchUpdateResult contains the results that are sent to the
 * client after an update command.
 */
typedef struct
{
	/* response status (seems to always be 1?) */
	double ok;

	/* number of rows that matched the query for update (matched + upserted) */
	uint64 rowsMatched;

	/* number of rows modified */
	uint64 rowsModified;

	/* list of write errors for each update, or NIL */
	List *writeErrors;

	/* list of upserts */
	List *upserted;
} BatchUpdateResult;


typedef struct
{
	bool isUpdateOne;
	pgbson *shardKeyBson;
	bool isOrdered;

	union
	{
		UpdateOneParams updateOne;
		bson_value_t updateBatch;
	} param;

	/* False by default. It can be set to true in request command. */
	bool bypassDocumentValidation;
} WorkerUpdateParam;


extern bool UseLocalExecutionShardQueries;

static BatchUpdateSpec * BuildBatchUpdateSpec(bson_iter_t *updateCommandIter,
											  pgbsonsequence *updateDocs);
static List * BuildUpdateSpecList(bson_iter_t *updateArrayIter, bool *hasUpsert);
static List * BuildUpdateSpecListFromSequence(pgbsonsequence *updateDocs,
											  bool *hasUpsert);
static UpdateSpec * BuildUpdateSpec(bson_iter_t *updateIterator);
static void ProcessBatchUpdate(MongoCollection *collection,
							   BatchUpdateSpec *batchSpec,
							   text *transactionId,
							   BatchUpdateResult *batchResult,
							   ExprEvalState *stateForSchemaValidation);
static void ProcessBatchUpdateCore(MongoCollection *collection, List *updates,
								   text *transactionId, BatchUpdateResult *batchResult,
								   bool isOrdered, bool forceInlineWrites,
								   ExprEvalState *stateForSchemaValidation);
static pgbson * ProcessBatchUpdateUnsharded(MongoCollection *collection,
											BatchUpdateSpec *batchSpec,
											text *transactionId, bool *hasWriteErrors);
static void ProcessUpdate(MongoCollection *collection, UpdateSpec *updateSpec,
						  text *transactionId, UpdateResult *result,
						  bool forceInlineWrites,
						  ExprEvalState *stateForSchemaValidation);
static UpdateAllMatchingDocsResult UpdateAllMatchingDocuments(MongoCollection *collection,
															  pgbson *query,
															  pgbson *update,
															  pgbson *arrayFilters, bool
															  hasShardKeyValueFilter,
															  int64 shardKeyHash,
															  ExprEvalState *
															  stateForSchemaValidation,
															  bool *hasOnlyObjectIdFilter);
static void CallUpdateOne(MongoCollection *collection, UpdateOneParams *updateOneParams,
						  int64 shardKeyHash, text *transactionId,
						  UpdateOneResult *result, bool forceInlineWrites,
						  ExprEvalState *stateForSchemaValidation);
static void UpdateOneInternal(MongoCollection *collectionId,
							  UpdateOneParams *updateOneParams,
							  int64 shardKeyHash, UpdateOneResult *result,
							  ExprEvalState *stateForSchemaValidation);
static void UpdateOneInternalWithRetryRecord(MongoCollection *collection, int64
											 shardKeyHash,
											 text *transactionId,
											 UpdateOneParams *updateOneParams,
											 UpdateOneResult *result,
											 ExprEvalState *stateForSchemaValidation);
static bool SelectUpdateCandidate(uint64 collectionId, const char *shardTableName, int64
								  shardKeyHash, pgbson *query,
								  pgbson *update, pgbson *arrayFilters, pgbson *sort,
								  UpdateCandidate *updateCandidate,
								  bool getOriginalDocument, bool *hasOnlyObjectIdFilter);
static bool UpdateDocumentByTID(uint64 collectionId, const char *shardTableName, int64
								shardKeyHash,
								ItemPointer tid, pgbson *updatedDocument);
static bool DeleteDocumentByTID(uint64 collectionId, int64 shardKeyHash,
								ItemPointer tid);
static void UpdateOneObjectId(MongoCollection *collection,
							  UpdateOneParams *updateOneParams,
							  bson_value_t *objectId, text *transactionId,
							  UpdateOneResult *result,
							  ExprEvalState *stateForSchemaValidation);
static pgbson * UpsertDocument(MongoCollection *collection, pgbson *update,
							   pgbson *query, pgbson *arrayFilters,
							   ExprEvalState *stateForSchemaValidation,
							   bool hasOnlyObjectIdFilter);
static List * ValidateQueryAndUpdateDocuments(BatchUpdateSpec *batchSpec);
static pgbson * BuildResponseMessage(BatchUpdateResult *batchResult);
static void BuildUpdates(BatchUpdateSpec *spec);
static void DeserializeUpdateWorkerSpec(pgbson *updateInternalSpec,
										WorkerUpdateParam *params);
static pgbson * SerializeBatchUpdateResult(BatchUpdateResult *result);
static pgbson * DeserializeBatchUpdateWorkerResponse(pgbson *response,
													 bool *hasWriteErrors);
static pgbson * SerializeUpdateOneResult(UpdateOneResult *result);
static pgbson * SerializeUpdateOneParams(UpdateOneParams *params, pgbson *shardKeyBson);
static void DeserializeUpdateOneResult(pgbson *resultBson, UpdateOneResult *result);
static pgbson * SerializeUnshardedUpdateParams(const bson_value_t *updateSpec,
											   bool isOrdered, bool
											   bypassDocumentValidation);
static Datum CallUpdateWorker(MongoCollection *collection, pgbson *serializedSpec,
							  pgbsonsequence *updateDocs, int64 shardKeyHash,
							  text *transactionId);
static pgbson * ProcessUnshardedUpdateBatchWorker(MongoCollection *collection,
												  List *updates, bool isOrdered,
												  int64 shardKeyHash,
												  text *transactionId,
												  ExprEvalState *stateForSchemaValidation);
static void CallUpdateWorkerForUpdateOne(MongoCollection *collection,
										 UpdateOneParams *updateOneParams,
										 int64 shardKeyHash, text *transactionId,
										 UpdateOneResult *result);

/* Yb specific functions */
static bool YbUpdateDocumentByYbctid(uint64 collectionId,
									 const char *shardTableName,
									 int64 shardKeyHash, Datum ybctid,
									 pgbson *updatedDocument);
static bool YbDeleteDocumentByYbctid(uint64 collectionId, int64 shardKeyHash,
									 Datum ybctid);

PG_FUNCTION_INFO_V1(command_update);
PG_FUNCTION_INFO_V1(command_update_one);
PG_FUNCTION_INFO_V1(command_update_worker);


/*
 * command_update handles a single update on a collection.
 */
Datum
command_update(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("database name cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("update document cannot be NULL")));
	}

	Datum databaseNameDatum = PG_GETARG_DATUM(0);
	pgbson *updateSpec = PG_GETARG_PGBSON(1);

	pgbsonsequence *updateDocs = PG_GETARG_MAYBE_NULL_PGBSON_SEQUENCE(2);

	text *transactionId = NULL;
	if (!PG_ARGISNULL(3))
	{
		transactionId = PG_GETARG_TEXT_P(3);
	}

	ReportFeatureUsage(FEATURE_COMMAND_UPDATE);

	/* fetch TupleDesc for return value, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc;
	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	bson_iter_t updateCommandIter;
	PgbsonInitIterator(updateSpec, &updateCommandIter);

	/*
	 * We first validate update command BSON and build a specification.
	 */
	BatchUpdateSpec *batchSpec = BuildBatchUpdateSpec(&updateCommandIter, updateDocs);

	BatchUpdateResult batchResult;
	memset(&batchResult, 0, sizeof(batchResult));

	Datum collectionNameDatum = CStringGetTextDatum(batchSpec->collectionName);
	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseNameDatum, collectionNameDatum,
									  RowExclusiveLock);

	Datum values[2];
	bool isNulls[2] = { false, false };
	HeapTuple resultTuple;
	if (collection == NULL)
	{
		/* We have to create the update spec here to ensure we track hasUpsert */
		BuildUpdates(batchSpec);

		ValidateCollectionNameForUnauthorizedSystemNs(batchSpec->collectionName,
													  databaseNameDatum);

		if (batchSpec->hasUpsert)
		{
			/* upsert on a non-existent collection creates the collection */
			collection = CreateCollectionForInsert(databaseNameDatum,
												   collectionNameDatum);
		}
		else
		{
			/*
			 * Pure update without upsert on non-existent collection is a noop,
			 * but we still need to report (write) errors due to invalid query
			 * / update documents.
			 */
			batchResult.ok = 1;
			batchResult.rowsMatched = 0;
			batchResult.rowsModified = 0;
			batchResult.writeErrors = ValidateQueryAndUpdateDocuments(batchSpec);
			batchResult.upserted = NIL;

			values[0] = PointerGetDatum(BuildResponseMessage(&batchResult));
			values[1] = BoolGetDatum(batchResult.writeErrors == NIL);
			resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
			PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
		}
	}

	Oid shardOid = TryGetCollectionShardTable(collection, NoLock);
	if (shardOid == InvalidOid)
	{
		/* Shard not valid on this node anymore (due to shard moves etc) */
		collection->shardTableName[0] = '\0';
	}

	/*
	 * for unsharded collections, we can push the whole update to the worker directly
	 * so we don't create eval state here.
	 */
	ExprEvalState *state = NULL;

	bool hasWriteErrors = false;
	pgbson *result = NULL;
	if (DefaultInlineWriteOperations ||
		collection->shardKey != NULL || collection->shardTableName[0] != '\0')
	{
		/* Document validation occurs regardless of whether the validation action is set to error or warn.
		 * If validation fails and the action is error, an error is thrown; if the action is warn, a warning is logged.
		 * Since we do not need to log a warning in this context, we will avoid calling ValidateSchemaOnDocumentInsert when the validation action is set to warn.
		 */
		if (CheckSchemaValidationEnabled(collection, batchSpec->bypassDocumentValidation))
		{
			state = PrepareForSchemaValidation(collection->schemaValidator.validator,
											   CurrentMemoryContext);
		}

		ProcessBatchUpdate(collection, batchSpec, transactionId,
						   &batchResult, state);
		result = BuildResponseMessage(&batchResult);
		hasWriteErrors = batchResult.writeErrors != NIL;
	}
	else
	{
		/* Unsharded and the shard table is in a remote node we can push the whole batch to the worker directly. */
		result = ProcessBatchUpdateUnsharded(collection, batchSpec, transactionId,
											 &hasWriteErrors);
	}

	if (EnableSchemaValidation && state != NULL)
	{
		FreeExprEvalState(state, CurrentMemoryContext);
	}

	values[0] = PointerGetDatum(result);
	values[1] = BoolGetDatum(!hasWriteErrors);
	resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
	PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
}


/*
 * Does an insert or upsert for an update. If the original query was for an object_id
 * Does an InsertOrReplace. Otherwise does an Insert to fail if there's an object_id
 * constraint that fails.
 */
static inline void
DoInsertForUpdate(MongoCollection *collection, uint64_t shardKeyHash, pgbson *objectId,
				  pgbson *newDocument, bool hasOnlyObjectIdFilter)
{
	if (hasOnlyObjectIdFilter)
	{
		InsertOrReplaceDocument(collection->collectionId, collection->shardTableName,
								shardKeyHash, objectId, newDocument);
	}
	else
	{
		InsertDocument(collection->collectionId, collection->shardTableName,
					   shardKeyHash, objectId, newDocument);
	}
}


/*
 * Reentrant function that looks at the update spec
 * and parses the update list OR update sequence of documents
 * and builds a batch update spec.
 * This is done as a second pass so that in subsequent changes,
 * for single shard updates, we can skip the parse stage and just
 * send the entire spec to the worker.
 */
static void
BuildUpdates(BatchUpdateSpec *spec)
{
	if (spec->updates != NIL)
	{
		return;
	}

	List *updates = NIL;
	if (spec->updateSequence != NULL)
	{
		updates = BuildUpdateSpecListFromSequence(spec->updateSequence, &spec->hasUpsert);
	}
	else if (spec->updateValue.value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t updateArrayIter;
		BsonValueInitIterator(&spec->updateValue, &updateArrayIter);
		updates = BuildUpdateSpecList(&updateArrayIter, &spec->hasUpsert);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg("BSON field 'update.updates' is missing but is "
							   "a required field")));
	}

	int updateCount = list_length(updates);
	if (updateCount == 0 || updateCount > MaxWriteBatchSize)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDLENGTH),
						errmsg("Write batch sizes must be between 1 and %d. "
							   "Got %d operations.", MaxWriteBatchSize, updateCount),
						errdetail_log("Write batch sizes must be between 1 and %d. "
									  "Got %d operations.", MaxWriteBatchSize,
									  updateCount)));
	}

	spec->updates = updates;
}


/*
 * BuildBatchUpdateSpec validates the update command BSON and builds
 * a BatchUpdateSpec.
 */
static BatchUpdateSpec *
BuildBatchUpdateSpec(bson_iter_t *updateCommandIter, pgbsonsequence *updateDocs)
{
	const char *collectionName = NULL;
	bool isOrdered = true;
	bool bypassDocumentValidation = false;

	bson_value_t updateValue = { 0 };
	while (bson_iter_next(updateCommandIter))
	{
		const char *field = bson_iter_key(updateCommandIter);

		if (strcmp(field, "update") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(updateCommandIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("collection name has invalid type %s",
									   BsonIterTypeName(updateCommandIter))));
			}

			collectionName = bson_iter_utf8(updateCommandIter, NULL);
		}
		else if (strcmp(field, "updates") == 0)
		{
			EnsureTopLevelFieldType("update.updates", updateCommandIter, BSON_TYPE_ARRAY);

			/* if both docs and spec are provided, fail */
			if (updateDocs != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("Unexpected additional updates")));
			}

			updateValue = *bson_iter_value(updateCommandIter);
		}
		else if (strcmp(field, "ordered") == 0)
		{
			EnsureTopLevelFieldType("update.ordered", updateCommandIter, BSON_TYPE_BOOL);

			isOrdered = bson_iter_bool(updateCommandIter);
		}
		else if (strcmp(field, "bypassDocumentValidation") == 0)
		{
			/* TODO: unsupport by default */
			if (!EnableBypassDocumentValidation)
			{
				continue;
			}

			EnsureTopLevelFieldType("update.bypassDocumentValidation", updateCommandIter,
									BSON_TYPE_BOOL);

			bypassDocumentValidation = bson_iter_bool(updateCommandIter);
		}
		else if (strcmp(field, "maxTimeMS") == 0)
		{
			EnsureTopLevelFieldIsNumberLike("update.maxTimeMS", bson_iter_value(
												updateCommandIter));
			SetExplicitStatementTimeout(BsonValueAsInt32(bson_iter_value(
															 updateCommandIter)));
		}
		else if (IsCommonSpecIgnoredField(field))
		{
			elog(DEBUG1, "Unrecognized command field: update.%s", field);

			/*
			 *  Silently ignore now, so that clients don't break
			 *  TODO: implement me
			 *      writeConcern
			 *      let
			 */
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg("BSON field 'update.%s' is an unknown field",
								   field)));
		}
	}

	if (collectionName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg("BSON field 'update.update' is missing but "
							   "a required field")));
	}


	BatchUpdateSpec *batchSpec = palloc0(sizeof(BatchUpdateSpec));

	batchSpec->collectionName = (char *) collectionName;
	batchSpec->updateValue = updateValue;
	batchSpec->updateSequence = updateDocs;
	batchSpec->isOrdered = isOrdered;
	batchSpec->bypassDocumentValidation = bypassDocumentValidation;

	return batchSpec;
}


/*
 * BuildUpdateSpecList iterates over an array of update operations and
 * builds a Update for each object.
 */
static List *
BuildUpdateSpecList(bson_iter_t *updateArrayIter, bool *hasUpsert)
{
	List *updates = NIL;

	while (bson_iter_next(updateArrayIter))
	{
		StringInfo fieldNameStr = makeStringInfo();
		int arrIdx = list_length(updates);
		appendStringInfo(fieldNameStr, "update.updates.%d", arrIdx);

		EnsureTopLevelFieldType(fieldNameStr->data, updateArrayIter, BSON_TYPE_DOCUMENT);

		bson_iter_t updateOperationIter;
		bson_iter_recurse(updateArrayIter, &updateOperationIter);

		UpdateSpec *update = BuildUpdateSpec(&updateOperationIter);

		updates = lappend(updates, update);

		if (update->updateOneParams.isUpsert)
		{
			*hasUpsert = true;
		}
	}

	return updates;
}


/*
 * BuildUpdateSpecListFromSequence builds a list of UpdateSpecs from a BsonSequence.
 */
static List *
BuildUpdateSpecListFromSequence(pgbsonsequence *updateDocs, bool *hasUpsert)
{
	List *updates = NIL;

	List *documents = PgbsonSequenceGetDocumentBsonValues(updateDocs);
	ListCell *documentCell;
	foreach(documentCell, documents)
	{
		bson_iter_t updateOperationIter;
		BsonValueInitIterator(lfirst(documentCell), &updateOperationIter);

		UpdateSpec *update = BuildUpdateSpec(&updateOperationIter);

		updates = lappend(updates, update);

		if (update->updateOneParams.isUpsert)
		{
			*hasUpsert = true;
		}
	}

	return updates;
}


/*
 * BuildUpdateSpec builds a UpdateSpec from the BSON of a single update
 * operation.
 */
static UpdateSpec *
BuildUpdateSpec(bson_iter_t *updateIter)
{
	pgbson *query = NULL;
	pgbson *update = NULL;
	pgbson *arrayFilters = NULL;
	bool isMulti = false;
	bool isUpsert = false;

	while (bson_iter_next(updateIter))
	{
		const char *field = bson_iter_key(updateIter);

		if (strcmp(field, "q") == 0)
		{
			EnsureTopLevelFieldType("update.updates.q", updateIter, BSON_TYPE_DOCUMENT);

			query = PgbsonInitFromIterDocumentValue(updateIter);
		}
		else if (strcmp(field, "u") == 0)
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(updateIter) &&
				!BSON_ITER_HOLDS_ARRAY(updateIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg("BSON field 'update.updates.u' is the wrong type "
									   "'%s', expected type 'object' or 'array'",
									   BsonIterTypeName(updateIter))));
			}

			const bson_value_t *bsonValue = bson_iter_value(updateIter);

			/* we keep update documents in projected form to preserve the type */
			update = BsonValueToDocumentPgbson(bsonValue);
		}
		else if (strcmp(field, "multi") == 0)
		{
			EnsureTopLevelFieldType("update.updates.multi", updateIter, BSON_TYPE_BOOL);

			isMulti = bson_iter_bool(updateIter);
		}
		else if (strcmp(field, "upsert") == 0)
		{
			EnsureTopLevelFieldType("update.updates.upsert", updateIter, BSON_TYPE_BOOL);

			isUpsert = bson_iter_bool(updateIter);
		}
		else if (strcmp(field, "arrayFilters") == 0)
		{
			EnsureTopLevelFieldType("update.updates.arrayFilters", updateIter,
									BSON_TYPE_ARRAY);
			const bson_value_t *bsonValue = bson_iter_value(updateIter);

			/* we keep arrayFilters in projected form to preserve the type */
			arrayFilters = BsonValueToDocumentPgbson(bsonValue);
		}
		else if (strcmp(field, "collation") == 0)
		{
			/* We error on collation by default unlike FindAndModify. So we don't need to condition on EnableCollation GUC here. */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("BSON field 'update.updates.collation' is not yet "
								   "supported")));
		}
		else if (strcmp(field, "hint") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("BSON field 'update.updates.hint' is not yet "
								   "supported")));
		}
		else if (strcmp(field, "comment") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("BSON field 'update.updates.comment' is not yet "
								   "supported")));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg("BSON field 'update.updates.%s' is an unknown field",
								   field)));
		}
	}

	if (query == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg("BSON field 'update.updates.q' is missing but "
							   "a required field")));
	}

	if (update == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg("BSON field 'update.updates.u' is missing but "
							   "a required field")));
	}

	UpdateSpec *updateSpec = palloc0(sizeof(UpdateSpec));
	updateSpec->updateOneParams.query = query;
	updateSpec->updateOneParams.update = update;
	updateSpec->updateOneParams.isUpsert = isUpsert;
	updateSpec->updateOneParams.arrayFilters = arrayFilters;
	updateSpec->updateOneParams.returnDocument = UPDATE_RETURNS_NONE;
	updateSpec->isMulti = isMulti;

	return updateSpec;
}


/*
 * Updates the result of a single update into the outer batchResult
 * for the multi-update scenario.
 */
inline static void
UpdateResultInBatch(BatchUpdateResult *batchResult, UpdateResult *updateResult,
					MemoryContext context, int updateIndex)
{
	batchResult->rowsMatched += updateResult->rowsMatched;
	batchResult->rowsModified += updateResult->rowsModified;

	if (updateResult->performedUpsert)
	{
		Assert(updateResult->upsertedObjectId != NULL);

		batchResult->rowsMatched += 1;

		MemoryContext currentContext = MemoryContextSwitchTo(context);
		UpsertResult *upsertResult = palloc0(sizeof(UpsertResult));
		upsertResult->index = updateIndex;
		upsertResult->objectId = updateResult->upsertedObjectId;

		batchResult->upserted = lappend(batchResult->upserted, upsertResult);
		MemoryContextSwitchTo(currentContext);
	}
}


/*
 * Updates a batch of updates in a single transaction. This is done optimistically
 * For scenarios where it's successful. Rolls back the sub-transaction in case of
 * failure.
 */
static bool
DoMultiUpdate(MongoCollection *collection, List *updates, text *transactionId,
			  BatchUpdateResult *batchResult, int updateIndex, bool forceInlineWrites,
			  int *recordsUpdated, ExprEvalState *stateForSchemaValidation)
{
	/*
	 * Execute the query inside a sub-transaction, so we can restore order
	 * after a failure.
	 */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile int updateInnerIndex = updateIndex;
	volatile int updateCount = 0;

	BatchUpdateResult batchResultInner;
	memset(&batchResultInner, 0, sizeof(batchResultInner));

	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		ListCell *updateCell;
		while (updateInnerIndex < list_length(updates) &&
			   updateCount < BatchWriteSubTransactionCount)
		{
			CHECK_FOR_INTERRUPTS();
			updateCell = list_nth_cell(updates, updateInnerIndex);
			UpdateSpec *updateSpec = lfirst(updateCell);
			UpdateResult updateResult = { 0 };
			ProcessUpdate(collection, updateSpec, transactionId, &updateResult,
						  forceInlineWrites, stateForSchemaValidation);
			UpdateResultInBatch(&batchResultInner, &updateResult, oldContext,
								updateInnerIndex);
			updateInnerIndex++;
			updateCount++;
		}

		/* Commit the inner transaction, return to outer xact context */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
		batchResult->rowsMatched += batchResultInner.rowsMatched;
		batchResult->rowsModified += batchResultInner.rowsModified;
		batchResult->upserted = list_concat(batchResult->upserted,
											batchResultInner.upserted);
		list_free(batchResultInner.upserted);
		*recordsUpdated = updateCount;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		if (IsOperatorInterventionError(errorData))
		{
			ReThrowError(errorData);
		}

		*recordsUpdated = 0;
		updateCount = 0;
	}
	PG_END_TRY();
	return updateCount != 0;
}


/*
 * Updates a single update in a single sub-transaction.
 */
static bool
DoSingleUpdate(MongoCollection *collection, UpdateSpec *updateSpec, text *transactionId,
			   BatchUpdateResult *batchResult, int updateIndex, bool forceInlineWrites,
			   ExprEvalState *volatile stateForSchemaValidation)
{
	/*
	 * Execute the query inside a sub-transaction, so we can restore order
	 * after a failure.
	 */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool isSuccess = false;

	UpdateResult updateResult;
	memset(&updateResult, 0, sizeof(updateResult));

	/* use a subtransaction to correctly handle failures */
	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		ProcessUpdate(collection, updateSpec, transactionId, &updateResult,
					  forceInlineWrites, stateForSchemaValidation);

		/* Commit the inner transaction, return to outer xact context */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		UpdateResultInBatch(batchResult, &updateResult, oldContext, updateIndex);
		isSuccess = true;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		batchResult->writeErrors = lappend(batchResult->writeErrors,
										   GetWriteErrorFromErrorData(errorData,
																	  updateIndex));
		FreeErrorData(errorData);
		isSuccess = false;
	}
	PG_END_TRY();

	return isSuccess;
}


static pgbson *
DeserializeBatchUpdateWorkerResponse(pgbson *response, bool *hasWriteErrors)
{
	pgbson *result = NULL;
	bson_iter_t iter;
	PgbsonInitIterator(response, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		if (strcmp(key, "response") == 0)
		{
			result = PgbsonInitFromDocumentBsonValue(bson_iter_value(&iter));
		}
		else if (strcmp(key, "ok") == 0)
		{
			*hasWriteErrors = !bson_iter_bool(&iter);
		}
	}

	return result;
}


static pgbson *
ProcessBatchUpdateUnsharded(MongoCollection *collection, BatchUpdateSpec *batchSpec,
							text *transactionId, bool *hasWriteErrors)
{
	Assert(collection->shardKey == NULL);

	pgbsonsequence *updateSequence = batchSpec->updateSequence;
	const bson_value_t *updateValue = updateSequence == NULL ? &batchSpec->updateValue :
									  NULL;

	pgbson *updateSpecs = SerializeUnshardedUpdateParams(updateValue,
														 batchSpec->isOrdered,
														 batchSpec->
														 bypassDocumentValidation);

	/* since this is unsharded, the keyHash is just the collection id. */
	int shardKeyHash = collection->collectionId;
	Datum workerResult = CallUpdateWorker(collection, updateSpecs, updateSequence,
										  shardKeyHash, transactionId);

	pgbson *response = DatumGetPgBson(workerResult);
	return DeserializeBatchUpdateWorkerResponse(response, hasWriteErrors);
}


static void
ProcessBatchUpdateCore(MongoCollection *collection, List *updates, text *transactionId,
					   BatchUpdateResult *batchResult, bool isOrdered,
					   bool forceInlineWrites, ExprEvalState *stateForSchemaValidation)
{
	batchResult->ok = 1;
	batchResult->rowsMatched = 0;
	batchResult->rowsModified = 0;
	batchResult->writeErrors = NIL;
	batchResult->upserted = NIL;

	text *subTransactionId = transactionId;
	if (list_length(updates) > 1)
	{
		/*
		 * We cannot pass the same transactionId to ProcessUpdate when there are
		 * multiple updates, since they would be considered retries of each
		 * other. We pass NULL for now to disable retryable writes.
		 */
		subTransactionId = NULL;
	}

	int updateIndex = 0;
	bool hasBatchUpdateFailed = false;

	ListCell *updateCell = NULL;
	while (updateIndex < list_length(updates))
	{
		CHECK_FOR_INTERRUPTS();

		bool isSuccess = false;
		if (list_length(updates) > 1 && !hasBatchUpdateFailed)
		{
			/* Optimistically try to do multiple updates together, if it fails, try again one by one to figure out which one failed */
			int incrementCount = 0;
			isSuccess = DoMultiUpdate(collection, updates, subTransactionId,
									  batchResult, updateIndex, forceInlineWrites,
									  &incrementCount, stateForSchemaValidation);
			if (!isSuccess || incrementCount == 0)
			{
				hasBatchUpdateFailed = true;
			}
			else
			{
				updateIndex += incrementCount;
			}

			continue;
		}

		updateCell = list_nth_cell(updates, updateIndex);
		UpdateSpec *updateSpec = lfirst(updateCell);
		isSuccess = DoSingleUpdate(collection, updateSpec, subTransactionId,
								   batchResult, updateIndex, forceInlineWrites,
								   stateForSchemaValidation);
		updateIndex++;

		if (!isSuccess && isOrdered)
		{
			/* stop trying update operations after a failure if using ordered:true */
			break;
		}
	}
}


/*
 * ProcessBatchUpdate iterates over the updates array and executes each
 * update in a subtransaction, to allow us to continue after an error.
 *
 * If batchSpec->isOrdered is false, we continue with remaining tasks on
 * error.
 *
 * Using subtransactions is slightly different from Mongo, which effectively
 * does each update operation in a separate transaction, but it has roughly
 * the same overall UX.
 */
static void
ProcessBatchUpdate(MongoCollection *collection, BatchUpdateSpec *batchSpec,
				   text *transactionId, BatchUpdateResult *batchResult,
				   ExprEvalState *stateForSchemaValidation)
{
	BuildUpdates(batchSpec);
	List *updates = batchSpec->updates;
	bool isOrdered = batchSpec->isOrdered;

	/* Check if we can inline the updates */
	if (transactionId == NULL && collection->shardKey == NULL)
	{
		/* Technically ShareLock is okay here since we use SPI internally for the updates
		 * However, if we end up building the AST directly, then easier to use RowExclusiveLock.
		 */
		batchSpec->shardTableOid = TryGetCollectionShardTable(collection,
															  RowExclusiveLock);
	}

	/* We are in sharded scenario so we need to go through the planner to do the writes and then call the worker. */
	bool forceInlineWrites = false;
	ProcessBatchUpdateCore(collection, updates, transactionId, batchResult, isOrdered,
						   forceInlineWrites, stateForSchemaValidation);
}


/*
 * ProcessUpdate processes a single update operation defined in
 * updateSpec on the given collection.
 */
static void
ProcessUpdate(MongoCollection *collection, UpdateSpec *updateSpec,
			  text *transactionId, UpdateResult *result, bool forceInlineWrites,
			  ExprEvalState *stateForSchemaValidation)
{
	pgbson *query = updateSpec->updateOneParams.query;
	pgbson *update = updateSpec->updateOneParams.update;
	pgbson *arrayFilters = updateSpec->updateOneParams.arrayFilters;
	bool isUpsert = updateSpec->updateOneParams.isUpsert;
	bool isMulti = updateSpec->isMulti;

	/* determine whether query filters by a single shard key value */
	int64 shardKeyHash = 0;

	bool hasShardKeyValueFilter =
		ComputeShardKeyHashForQuery(collection->shardKey, collection->collectionId, query,
									&shardKeyHash);

	result->rowsMatched = 0;
	result->rowsModified = 0;
	result->performedUpsert = false;
	result->upsertedObjectId = NULL;

	if (isMulti)
	{
		if (DetermineUpdateType(update) == UpdateType_ReplaceDocument)
		{
			/* Mongo does not support this case */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("multi update is not supported for "
								   "replacement-style update")));
		}

		/*
		 * Update as many document as match the query. This is not a retryable
		 * operation, so we ignore transactionId.
		 */
		bool hasOnlyObjectIdFilter = false;
		UpdateAllMatchingDocsResult updateAllResult = UpdateAllMatchingDocuments(
			collection, query, update, arrayFilters,
			hasShardKeyValueFilter,
			shardKeyHash, stateForSchemaValidation,
			&hasOnlyObjectIdFilter);

		result->rowsMatched = updateAllResult.matchedDocs;
		result->rowsModified = updateAllResult.rowsUpdated;

		/*
		 * In case of an upsert,
		 */
		if (isUpsert && result->rowsMatched == 0)
		{
			/*
			 * If the update had a retry record, we must have performed the upsert
			 * as well, so skip it.
			 */
			result->performedUpsert = true;
			result->upsertedObjectId = UpsertDocument(collection, update, query,
													  arrayFilters,
													  stateForSchemaValidation,
													  hasOnlyObjectIdFilter);
		}
	}
	else
	{
		UpdateOneResult updateOneResult;
		memset(&updateOneResult, 0, sizeof(UpdateOneResult));

		if (hasShardKeyValueFilter)
		{
			/*
			 * Update at most 1 document that matches the query on a single shard.
			 *
			 * For unsharded collection, this is the shard that contains all the
			 * data.
			 */
			UpdateOne(collection, &updateSpec->updateOneParams, shardKeyHash,
					  transactionId, &updateOneResult, forceInlineWrites,
					  stateForSchemaValidation);
		}
		else if (isUpsert)
		{
			/*
			 * Upsert on a shard collection without a shard key filter is not supported currently.
			 *
			 * TODO: Use ErrorCodes.ShardKeyNotFound
			 */
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							errmsg("An {upsert:true} update on a sharded collection "
								   "must target a single shard")));
		}
		else
		{
			/* determine whether query filters by a single object ID */
			bson_iter_t queryDocIter;
			PgbsonInitIterator(query, &queryDocIter);
			bson_value_t idFromQueryDocument = { 0 };
			bool errorOnConflict = false;
			bool queryHasNonIdFilters = false;
			bool hasObjectIdFilter =
				TraverseQueryDocumentAndGetId(&queryDocIter, &idFromQueryDocument,
											  errorOnConflict, &queryHasNonIdFilters);

			if (hasObjectIdFilter)
			{
				/*
				 * Update at most 1 document that matches an _id equality filter from
				 * a sharded collection without specifying a a shard key filter.
				 */
				UpdateOneObjectId(collection, &updateSpec->updateOneParams,
								  &idFromQueryDocument, transactionId,
								  &updateOneResult, stateForSchemaValidation);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
								errmsg("A {multi:false} update on a sharded collection "
									   "must contain an exact match on _id or target a "
									   "single shard")));
			}
		}

		result->rowsModified = updateOneResult.isRowUpdated ? 1 : 0;
		result->rowsMatched = updateOneResult.isRowUpdated ||
							  updateOneResult.updateSkipped ? 1 : 0;

		if (isUpsert && !updateOneResult.isRowUpdated && !updateOneResult.updateSkipped)
		{
			result->performedUpsert = true;
			result->upsertedObjectId = updateOneResult.upsertedObjectId;
		}
	}
}


/*
 * UpdateAllMatchingDocuments updates documents that match the query
 * and need to be updated based on the update document. Returns the
 * number of updated rows and matched documents for the query.
 */
static UpdateAllMatchingDocsResult
UpdateAllMatchingDocuments(MongoCollection *collection, pgbson *queryDoc,
						   pgbson *updateDoc, pgbson *arrayFilters,
						   bool hasShardKeyValueFilter, int64 shardKeyHash,
						   ExprEvalState *schemaValidationExprEvalState,
						   bool *hasOnlyObjectIdFilter)
{
	bool queryHasNonIdFilters = false;
	pgbson *objectIdFilter = GetObjectIdFilterFromQueryDocument(queryDoc,
																&queryHasNonIdFilters);

	*hasOnlyObjectIdFilter = objectIdFilter != NULL && !queryHasNonIdFilters;

	const char *tableName = collection->tableName;
	bool isLocalShardQuery = false;
	if (collection->shardTableName[0] != '\0')
	{
		/* If we can push down to the local shard, then prefer that. */
		tableName = collection->shardTableName;
		isLocalShardQuery = true;
		NumBsonDocumentsUpdated = 0;
	}

	StringInfoData updateQuery;
	int argCount = 3;
	Oid argTypes[6];
	Datum argValues[6];
	char argNulls[6];

	UpdateAllMatchingDocsResult result;
	memset(&result, 0, sizeof(UpdateAllMatchingDocsResult));

	SPI_connect();
	initStringInfo(&updateQuery);

	/* We use a CTE with the UPDATE document and then select the count of number of rows to get the matched docs,
	 * and the sum of the RETURNING value, which returns 1 if a document was updated and 0
	 * if not, that way we can get the total updated documents in order to return the correct result.
	 *
	 * COUNT(*) = Total documents matched.
	 * SUM(updated) = Total documents updated.
	 *
	 * When calling bson_update_returned_value we need to pass a column as an argument so that the
	 * function call is not evaluated as a constant and it is executed as part of every row update
	 * in the UPDATE query.
	 *
	 * The reason behind having UPDATE being a CTE is so that we can use RETURNING and access the result
	 * via the CTE in a SELECT statement. We could've done this with a CTE that did a SELECT ApiInternalSchemaName.bson_update_document
	 * and then pass the result of that SELECT to the UPDATE statement, however that is very innefficient as the CTE would be
	 * pushed down to every worker node, whereas here we just execute the UPDATE in every worker node.
	 *
	 * With this approach we always update the row either to the new value or its current value, the only way to avoid writing the current
	 * value if no update is needed is with the multi CTE approach mentioned above, which is a lot slower.
	 *
	 */
	if (EnableSchemaValidation && schemaValidationExprEvalState != NULL &&
		IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		/*
		 * If schemaValidationExprEvalState is not NULL, we need to validate the document against the schema.
		 * We do this by calling the schema_validation_against_update function which will return true if the document matches the schema.
		 * We then use this result to determine if the document should be updated or not.
		 * We use the same approach as above, but we add a CTE to validate the document against the schema.
		 * A tricky here is that sourceDoc is not always necessary, only if validation level is moderate and newDoc does not match the schema.
		 * So there is a conditional to add sourceDoc to the CTE if validation level is moderate.
		 * The preformance of this approach is not ideal, but it is the best we can do with the current architecture.
		 * WITH filtered_documents AS (
		 * SELECT
		 * object_id,
		 * shard_key_value,
		 * document,
		 * (
		 *  SELECT COALESCE(newDocument, document)
		 *  FROM bson_update_document(
		 *      document,
		 *      $2::bson,
		 *      $1::bson,
		 *      $3::bson,
		 *      false
		 *  ) AS newDocument
		 * ) AS newDocument
		 * FROM documents_
		 * WHERE document OPERATOR(@@) $1::bson
		 * AND shard_key_value = $4
		 * ),
		 * v AS (
		 * SELECT
		 * object_id,
		 * shard_key_value,
		 * newDocument,
		 * ApiInternalSchemaName.schema_validation_against_update($5, filtered_documents.newDocument, filtered_documents.document, false)
		 * FROM filtered_documents
		 * ),
		 * u AS (
		 * UPDATE documents_
		 * SET document = newDocument
		 * FROM v
		 * WHERE documents_.object_id OPERATOR(=) v.object_id
		 * AND documents_.shard_key_value = v.shard_key_value
		 * RETURNING bson_update_returned_value(documents_.shard_key_value) AS updated
		 * )
		 * SELECT
		 * (SELECT COUNT(*) FROM filtered_documents) AS total_count,
		 * SUM(updated) AS total_updated
		 * FROM u;
		 */
		appendStringInfo(&updateQuery,
						 "WITH filtered_documents AS ("
						 "SELECT object_id, shard_key_value, document, (SELECT COALESCE(newDocument, document)"
						 " FROM %s.bson_update_document(document, $2::%s, "
						 "$1::%s, $3::%s, %s)) as newDocument FROM %s.%s"
						 " WHERE document OPERATOR(%s.@@) $1::%s ", ApiInternalSchemaName,
						 FullBsonTypeName, FullBsonTypeName,
						 FullBsonTypeName, "false", ApiDataSchemaName,
						 tableName, ApiCatalogSchemaName, FullBsonTypeName);


		if (hasShardKeyValueFilter)
		{
			appendStringInfoString(&updateQuery, "AND shard_key_value = $4 ");
		}

		if (objectIdFilter != NULL)
		{
			appendStringInfo(&updateQuery, "AND object_id OPERATOR(%s.=) $%d::%s",
							 CoreSchemaName,
							 hasShardKeyValueFilter ? 5 : 4,
							 FullBsonTypeName);
		}

		int paramIndex = hasShardKeyValueFilter ? objectIdFilter != NULL ? 6 : 5 :
						 objectIdFilter != NULL ? 5 : 4;

		if (collection->schemaValidator.validationLevel == ValidationLevel_Moderate)
		{
			appendStringInfo(&updateQuery,
							 "), v as (select object_id, shard_key_value, newDocument, %s.schema_validation_against_update($%d, filtered_documents.newDocument, filtered_documents.document, true) from filtered_documents), ",
							 ApiInternalSchemaName, paramIndex);
		}
		else
		{
			appendStringInfo(&updateQuery,
							 "), v as (select object_id, shard_key_value, newDocument, %s.schema_validation_against_update($%d, filtered_documents.newDocument, filtered_documents.document, false) from filtered_documents), ",
							 ApiInternalSchemaName, paramIndex);
		}

		appendStringInfo(&updateQuery,
						 " u as (update %s.%s set document = newDocument from v where %s.%s.object_id OPERATOR(%s.=) v.object_id and %s.%s.shard_key_value = v.shard_key_value",
						 ApiDataSchemaName, tableName,
						 ApiDataSchemaName, tableName,
						 CoreSchemaName, ApiDataSchemaName, tableName);

		appendStringInfo(&updateQuery,
						 " RETURNING %s.bson_update_returned_value(%s.%s.shard_key_value) as updated)"
						 " SELECT (SELECT COUNT(*) FROM filtered_documents) as total_count, SUM(updated) FROM u",
						 ApiInternalSchemaName, ApiDataSchemaName, tableName);
	}
	else
	{
		appendStringInfo(&updateQuery,
						 "WITH u AS (UPDATE %s.%s"
						 " SET document = (SELECT COALESCE(newDocument, document)"
						 " FROM %s.bson_update_document(document, $2::%s, "
						 "$1::%s, $3::%s, %s)) WHERE document OPERATOR(%s.@@) $1::%s ",
						 ApiDataSchemaName, tableName,
						 ApiInternalSchemaName,
						 FullBsonTypeName, FullBsonTypeName, FullBsonTypeName,
						 "false",
						 ApiCatalogSchemaName,
						 FullBsonTypeName);

		if (hasShardKeyValueFilter)
		{
			appendStringInfoString(&updateQuery, "AND shard_key_value = $4 ");
		}

		if (objectIdFilter != NULL)
		{
			appendStringInfo(&updateQuery, "AND object_id OPERATOR(%s.=) $%d::%s",
							 CoreSchemaName,
							 hasShardKeyValueFilter ? 5 : 4,
							 FullBsonTypeName);
		}

		appendStringInfo(&updateQuery,
						 " RETURNING %s.bson_update_returned_value(shard_key_value) as updated)"
						 " SELECT COUNT(*), SUM(updated) FROM u",
						 ApiInternalSchemaName);
	}

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[0] = BYTEAOID;
	argValues[0] = PointerGetDatum(CastPgbsonToBytea(queryDoc));
	argNulls[0] = ' ';

	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(updateDoc));
	argNulls[1] = ' ';

	argTypes[2] = BYTEAOID;
	if (arrayFilters == NULL)
	{
		argValues[2] = 0;
		argNulls[2] = 'n';
	}
	else
	{
		argValues[2] = PointerGetDatum(CastPgbsonToBytea(arrayFilters));
		argNulls[2] = ' ';
	}


	/* if the query has a full shard key value filter, add a shard_key_value filter */
	int objectIdParamIndex = 3;
	if (hasShardKeyValueFilter)
	{
		objectIdParamIndex = 4;
		argTypes[3] = INT8OID;
		argValues[3] = Int64GetDatum(shardKeyHash);
		argNulls[3] = ' ';
		argCount++;
	}

	if (objectIdFilter != NULL)
	{
		argTypes[objectIdParamIndex] = BYTEAOID;
		argValues[objectIdParamIndex] = PointerGetDatum(CastPgbsonToBytea(
															objectIdFilter));
		argNulls[objectIdParamIndex] = ' ';
		argCount++;
	}

	if (schemaValidationExprEvalState != NULL)
	{
		int paramIndex = hasShardKeyValueFilter ? objectIdFilter != NULL ? 5 : 4 :
						 objectIdFilter != NULL ? 4 : 3;
		bytea *input_bytea = (bytea *) palloc(VARHDRSZ + sizeof(ExprEvalState));
		SET_VARSIZE(input_bytea, VARHDRSZ + sizeof(ExprEvalState));
		memcpy(VARDATA(input_bytea), schemaValidationExprEvalState,
			   sizeof(ExprEvalState));

		argTypes[paramIndex] = BYTEAOID;
		argValues[paramIndex] = PointerGetDatum(input_bytea);
		argNulls[paramIndex] = ' ';
		argCount++;
	}

	bool readOnly = false;
	long maxTupleCount = 0;

	SPI_execute_with_args(updateQuery.data, argCount, argTypes, argValues, argNulls,
						  readOnly, maxTupleCount);

	if (SPI_processed > 0)
	{
		bool isNull = false;

		int columnNumber = 1;

		/* matched_docs */
		Datum matchedDocsDatum = SPI_getbinval(SPI_tuptable->vals[0],
											   SPI_tuptable->tupdesc,
											   columnNumber, &isNull);

		Assert(!isNull);

		result.matchedDocs = DatumGetUInt64(matchedDocsDatum);

		columnNumber = 2;

		/* updated_rows */
		Datum updatedRowsDatum = SPI_getbinval(SPI_tuptable->vals[0],
											   SPI_tuptable->tupdesc,
											   columnNumber, &isNull);

		result.rowsUpdated = isNull ? 0 : DatumGetUInt64(updatedRowsDatum);
	}

	SPI_finish();

	if (isLocalShardQuery && result.rowsUpdated == 0)
	{
		result.rowsUpdated = NumBsonDocumentsUpdated;
	}

	return result;
}


/*
 * UpdateOne is the top-level function for updates with multi:false. It internally
 * calls ApiInternalSchemaName.update_one(..) to perform an update or delete of a single
 * row on a specific shard. If update_one returns a reinsert flag, which indicates
 * a change of shard_key_value, it additionally reinserts the document.
 */
void
UpdateOne(MongoCollection *collection, UpdateOneParams *updateOneParams,
		  int64 shardKeyHash, text *transactionId, UpdateOneResult *result,
		  bool forceInlineWrites, ExprEvalState *stateForSchemaValidation)
{
	CallUpdateOne(collection, updateOneParams, shardKeyHash, transactionId, result,
				  forceInlineWrites, stateForSchemaValidation);

	/* check for shard key value changes */
	if (result->reinsertDocument)
	{
		/* compute new shard key hash */
		int64 newShardKeyHash =
			ComputeShardKeyHashForDocument(collection->shardKey, collection->collectionId,
										   result->reinsertDocument);

		/* extract object ID */
		pgbson *objectId = PgbsonGetDocumentId(result->reinsertDocument);

		/* If we have to reinsert the document in a different shard */
		bool queryHasNonIdFilters = false;
		pgbson *objectIdFilter = GetObjectIdFilterFromQueryDocument(
			updateOneParams->query, &queryHasNonIdFilters);
		bool hasOnlyObjectIdFilter = objectIdFilter != NULL && !queryHasNonIdFilters;

		DoInsertForUpdate(collection, newShardKeyHash, objectId, result->reinsertDocument,
						  hasOnlyObjectIdFilter);
	}
}


static void
CallUpdateOne(MongoCollection *collection, UpdateOneParams *updateOneParams,
			  int64 shardKeyHash, text *transactionId, UpdateOneResult *result,
			  bool forceInlineWrites, ExprEvalState *stateForSchemaValidation)
{
	/* If we can simply call the updateOne here, don't bother trying to spin up an SPI runtime
	 * to call UpdateOne again.
	 * In the scenarios where we can thunk directly to the table since the table (shard) is on the same
	 * node as the query coordinator, call the functions directly here.
	 */
	if (forceInlineWrites || DefaultInlineWriteOperations ||
		collection->shardTableName[0] != '\0')
	{
		if (transactionId != NULL)
		{
			UpdateOneInternalWithRetryRecord(collection, shardKeyHash,
											 transactionId, updateOneParams,
											 result, stateForSchemaValidation);
		}
		else
		{
			UpdateOneInternal(collection, updateOneParams,
							  shardKeyHash, result, stateForSchemaValidation);
		}
	}
	else
	{
		/* Otherwise, call the worker via worker update one */
		/* pass down bypassDocumentValidation to updateOne*/
		updateOneParams->bypassDocumentValidation = !EnableSchemaValidation ||
													stateForSchemaValidation == NULL;
		CallUpdateWorkerForUpdateOne(collection, updateOneParams, shardKeyHash,
									 transactionId, result);
	}
}


static Datum
CallUpdateWorker(MongoCollection *collection, pgbson *serializedSpec,
				 pgbsonsequence *updateDocs, int64 shardKeyHash, text *transactionId)
{
	int argCount = 6;
	Datum argValues[6];

	/* whitespace means not null, n means null */
	char argNulls[6] = { ' ', ' ', ' ', ' ', 'n', 'n' };
	Oid argTypes[6] = { INT8OID, INT8OID, REGCLASSOID, BYTEAOID, BYTEAOID, TEXTOID };

	const char *updateQuery = FormatSqlQuery(
		" SELECT %s.update_worker($1, $2, $3, $4::%s.bson, $5::%s.bsonsequence, $6) FROM %s.documents_"
		UINT64_FORMAT " WHERE shard_key_value = %ld",
		DocumentDBApiInternalSchemaName, CoreSchemaNameV2, CoreSchemaNameV2,
		ApiDataSchemaName, collection->collectionId,
		shardKeyHash);

	argValues[0] = UInt64GetDatum(collection->collectionId);

	/* p_shard_key_value */
	argValues[1] = Int64GetDatum(shardKeyHash);

	/* p_shard_oid: We set this to InvalidOid here. The planner hook on the worker node will set this to
	 * non-InvalidOid before the actual function is executed.
	 */
	argValues[2] = ObjectIdGetDatum(InvalidOid);
	argValues[3] = PointerGetDatum(serializedSpec);

	if (updateDocs != NULL)
	{
		argValues[4] = PointerGetDatum(updateDocs);
		argNulls[4] = ' ';
	}

	if (transactionId != NULL)
	{
		argValues[5] = PointerGetDatum(transactionId);
		argNulls[5] = ' ';
	}

	bool readOnly = false;

	Datum resultDatum[1] = { 0 };
	bool isNulls[1] = { false };
	int numResults = 1;

	/* forceDelegation assumes nested distribution */
	RunMultiValueQueryWithNestedDistribution(updateQuery, argCount, argTypes, argValues,
											 argNulls,
											 readOnly, SPI_OK_SELECT, resultDatum,
											 isNulls, numResults);

	if (isNulls[0])
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("update_worker should not return null")));
	}

	return resultDatum[0];
}


static void
CallUpdateWorkerForUpdateOne(MongoCollection *collection,
							 UpdateOneParams *updateOneParams,
							 int64 shardKeyHash, text *transactionId,
							 UpdateOneResult *result)
{
	/* initialize result */
	result->isRowUpdated = false;
	result->updateSkipped = false;
	result->isRetry = false;
	result->reinsertDocument = NULL;
	result->resultDocument = NULL;
	result->upsertedObjectId = NULL;

	Datum resultDatum = CallUpdateWorker(collection, SerializeUpdateOneParams(
											 updateOneParams, collection->shardKey), NULL,
										 shardKeyHash, transactionId);

	pgbson *resultPgbson = DatumGetPgBson(resultDatum);

	DeserializeUpdateOneResult(resultPgbson, result);
}


static void
UpdateOneInternalWithRetryRecord(MongoCollection *collection, int64 shardKeyHash,
								 text *transactionId, UpdateOneParams *updateOneParams,
								 UpdateOneResult *result,
								 ExprEvalState *stateForSchemaValidation)
{
	RetryableWriteResult writeResult;

	/* if a retry record exists, delete it since only a single retry is allowed */
	if (DeleteRetryRecord(collection->collectionId, shardKeyHash, transactionId,
						  &writeResult))
	{
		/* get rows affected from the retry record */
		result->isRowUpdated = writeResult.rowsAffected;

		/* even if we reinserted the first time, there is no need to do more work */
		result->reinsertDocument = NULL;

		/* this is a retry */
		result->isRetry = true;

		result->resultDocument = writeResult.resultDocument;

		/* return the _id generated in the first try */
		result->upsertedObjectId = writeResult.objectId;
	}
	else
	{
		/* no retry record exists, update the row and get the object ID */
		UpdateOneInternal(collection, updateOneParams, shardKeyHash,
						  result, stateForSchemaValidation);

		pgbson *objectId = NULL;

		/* we only care about object ID in case of upsert */
		if (updateOneParams->isUpsert)
		{
			objectId = result->upsertedObjectId;
		}

		/*
		 * Remember that we performed a retryable write with the given
		 * transaction ID.
		 */
		InsertRetryRecord(collection->collectionId, shardKeyHash, transactionId,
						  objectId, result->isRowUpdated, result->resultDocument);
	}
}


/*
 * command_update_one handles a single update on a shard.
 */
Datum
command_update_one(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errmsg("This function is deprecated and should not be called")));
}


/*
 * Worker function for the update on a remote shard.
 */
Datum
command_update_worker(PG_FUNCTION_ARGS)
{
	uint64 collectionId = PG_GETARG_INT64(0);
	int64 shardKeyHash = PG_GETARG_INT64(1);
	Oid shardOid = PG_GETARG_OID(2);

	pgbson *updateInternalSpec = PG_GETARG_MAYBE_NULL_PGBSON(3);
	pgbsonsequence *updateSequence = PG_GETARG_MAYBE_NULL_PGBSON_SEQUENCE(4);
	text *transactionId = PG_ARGISNULL(5) ? NULL : PG_GETARG_TEXT_P(5);

	if (shardOid == InvalidOid)
	{
		/* The planner is expected to replace this */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Explicit shardOid must be set - this is a server bug"),
						errdetail_log(
							"Explicit shardOid must be set - this is a server bug")));
	}

	if (updateSequence == NULL && updateInternalSpec == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"update spec or update documents argument must be not null.")));
	}

	WorkerUpdateParam params;
	memset(&params, 0, sizeof(WorkerUpdateParam));
	DeserializeUpdateWorkerSpec(updateInternalSpec, &params);

	MongoCollection *mongoCollection = GetMongoCollectionByColId(collectionId, NoLock);

	if (mongoCollection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Collection not found for collectionId %lu on command_update_worker. This could be a metadata sync issue",
							collectionId),
						errdetail_log(
							"Collection not found for collectionId %lu on command_update_worker. This could be a metadata sync issue.",
							collectionId)));
	}

	UpdateMongoCollectionUsingIds(mongoCollection, collectionId, shardOid);

	mongoCollection->shardKey = params.shardKeyBson;

	/* Document validation occurs regardless of whether the validation action is set to error or warn.
	 * If validation fails and the action is error, an error is thrown; if the action is warn, a warning is logged.
	 * Since we do not need to log a warning in this context, we will avoid calling ValidateSchemaOnDocumentInsert when the validation action is set to warn.
	 * To reduce unnecessary overhead, we create evalState separately for updateOne and updateBatch.
	 */
	ExprEvalState *stateForSchemaValidation = NULL;

	if (CheckSchemaValidationEnabled(mongoCollection, params.bypassDocumentValidation))
	{
		stateForSchemaValidation = PrepareForSchemaValidation(
			mongoCollection->schemaValidator.validator,
			CurrentMemoryContext);
	}

	if (params.isUpdateOne)
	{
		UpdateOneResult result;
		memset(&result, 0, sizeof(result));

		if (transactionId != NULL)
		{
			/* transaction ID specified, use retryable write path */
			UpdateOneInternalWithRetryRecord(mongoCollection, shardKeyHash,
											 transactionId,
											 &params.param.updateOne, &result,
											 stateForSchemaValidation);
		}
		else
		{
			/* no transaction ID specified, do regular update */
			UpdateOneInternal(mongoCollection, &params.param.updateOne,
							  shardKeyHash, &result, stateForSchemaValidation);
		}

		pgbson *serializedResult = SerializeUpdateOneResult(&result);

		if (EnableSchemaValidation && stateForSchemaValidation != NULL)
		{
			FreeExprEvalState(stateForSchemaValidation, CurrentMemoryContext);
		}

		PG_RETURN_POINTER(serializedResult);
	}

	/* we have a batch unsharded update. */
	bool hasUpsertIgnore = false;
	List *updates = NIL;
	if (updateSequence != NULL)
	{
		updates = BuildUpdateSpecListFromSequence(updateSequence, &hasUpsertIgnore);
	}
	else
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(&params.param.updateBatch, &arrayIter);
		updates = BuildUpdateSpecList(&arrayIter, &hasUpsertIgnore);
	}

	pgbson *result = ProcessUnshardedUpdateBatchWorker(mongoCollection,
													   updates, params.isOrdered,
													   shardKeyHash,
													   transactionId,
													   stateForSchemaValidation);

	if (EnableSchemaValidation && stateForSchemaValidation != NULL)
	{
		FreeExprEvalState(stateForSchemaValidation, CurrentMemoryContext);
	}

	PG_RETURN_POINTER(result);
}


/*
 * This process an unsharded batch update on the worker itself. It constructs the mongo collection metadata based on the collection id
 * and iterates the list of update specs, applies them and serializes and returns the batch update result to send back to the coordinator.
 */
static pgbson *
ProcessUnshardedUpdateBatchWorker(MongoCollection *collection, List *updates, bool
								  isOrdered,
								  int64 shardKeyHash, text *transactionId,
								  ExprEvalState *stateForSchemaValidation)
{
	int updateCount = list_length(updates);
	if (updateCount == 0 || updateCount > MaxWriteBatchSize)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDLENGTH),
						errmsg("Write batch sizes must be between 1 and %d. "
							   "Got %d operations.", MaxWriteBatchSize, updateCount),
						errdetail_log("Write batch sizes must be between 1 and %d. "
									  "Got %d operations.", MaxWriteBatchSize,
									  updateCount)));
	}

	/* we're unsharded and at the worker so we can force inlining the writes when processing the batch. */
	bool forceInlineWrites = true;

	BatchUpdateResult batchUpdateResult;
	memset(&batchUpdateResult, 0, sizeof(BatchUpdateResult));
	ProcessBatchUpdateCore(collection, updates, transactionId, &batchUpdateResult,
						   isOrdered, forceInlineWrites, stateForSchemaValidation);

	return SerializeBatchUpdateResult(&batchUpdateResult);
}


/* Serializes the update spec if any and if it is ordered or not as a pgbson to send to the worker. */
static pgbson *
SerializeUnshardedUpdateParams(const bson_value_t *updateSpec, bool isOrdered, bool
							   bypassDocumentValidation)
{
	if (updateSpec != NULL && updateSpec->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg("BSON field 'update.updates' is missing but is "
							   "a required field")));
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_writer innerWriter;
	PgbsonWriterInit(&innerWriter);

	PgbsonWriterStartDocument(&writer, "updateUnsharded", -1, &innerWriter);

	if (updateSpec != NULL)
	{
		PgbsonWriterAppendValue(&innerWriter, "specs", -1, updateSpec);
	}

	PgbsonWriterAppendBool(&innerWriter, "isOrdered", -1, isOrdered);
	PgbsonWriterAppendBool(&innerWriter, "bypassDocumentValidation", -1,
						   bypassDocumentValidation);
	PgbsonWriterEndDocument(&writer, &innerWriter);

	return PgbsonWriterGetPgbson(&writer);
}


/* Serializes the update one params and shardkey bson as a pgbson to send down to the worker. */
static pgbson *
SerializeUpdateOneParams(UpdateOneParams *params, pgbson *shardKeyBson)
{
	pgbson_writer commandWriter;
	PgbsonWriterInit(&commandWriter);

	pgbson_writer writer;
	PgbsonWriterStartDocument(&commandWriter, "updateOne", -1, &writer);

	if (params->query != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "query", -1, params->query);
	}

	if (shardKeyBson != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "shardKeyBson", -1, shardKeyBson);
	}

	PgbsonWriterAppendDocument(&writer, "update", -1, params->update);

	PgbsonWriterAppendBool(&writer, "isUpsert", -1, params->isUpsert != 0);

	PgbsonWriterAppendBool(&writer, "bypassDocumentValidation", -1,
						   params->bypassDocumentValidation);

	if (params->sort != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "sort", 4, params->sort);
	}

	PgbsonWriterAppendInt32(&writer, "returnDocument", -1, (int) params->returnDocument);

	if (params->returnFields != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "returnFields", -1, params->returnFields);
	}

	if (params->arrayFilters != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "arrayFilters", -1, params->arrayFilters);
	}

	PgbsonWriterEndDocument(&commandWriter, &writer);
	return PgbsonWriterGetPgbson(&commandWriter);
}


/* Deserializes the unsharded update spec provided as a pgbson on the worker*/
static void
DeserializeUpdateUnshardedWorkerSpec(const bson_value_t *value, WorkerUpdateParam *params)
{
	bson_iter_t iter;
	BsonValueInitIterator(value, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		if (strcmp(key, "specs") == 0)
		{
			if (!BSON_ITER_HOLDS_ARRAY(&iter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																				"Update worker expects updateUnsharded.specs to be an array."))));
			}

			params->param.updateBatch = *bson_iter_value(&iter);
		}
		else if (strcmp(key, "isOrdered") == 0)
		{
			if (!BSON_ITER_HOLDS_BOOL(&iter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																				"Update worker expects updateUnsharded.isOrdered to be a bool."))));
			}

			params->isOrdered = BsonValueAsBool(bson_iter_value(&iter));
		}
		else if (strcmp(key, "bypassDocumentValidation") == 0)
		{
			if (!EnableBypassDocumentValidation)
			{
				continue;
			}

			if (!BSON_ITER_HOLDS_BOOL(&iter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																				"Update worker expects updateUnsharded.bypassDocumentValidation to be a bool."))));
			}

			params->bypassDocumentValidation = BsonValueAsBool(bson_iter_value(&iter));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																			"Unknown field to update worker for updateUnsharded document, '%s'.",
																			key),
																		errdetail_log(
																			"Unknown field to update worker for updateUnsharded document, '%s'.",
																			key))));
		}
	}
}


/* Deserializes the pgbson representing the update worker spec. It can have the following shapes:
 *  {"updateUnsharded": [{updateSpec1}, {updateSpec2}, { ... }]}
 *
 *  or for sharded updates:
 *  {"updateOne": {updateOneParams} }
 */
static void
DeserializeUpdateWorkerSpec(pgbson *updateInternalSpec,
							WorkerUpdateParam *params)
{
	pgbsonelement singleElement;
	bson_iter_t internalIter;

	params->shardKeyBson = NULL;
	params->isUpdateOne = false;
	params->bypassDocumentValidation = false;

	/* The top level is a pgbsonelement describing a type of update
	 * Right now the only supported mode is single doc update (updateOne)
	 */
	PgbsonToSinglePgbsonElement(updateInternalSpec, &singleElement);

	if (strcmp(singleElement.path, "updateUnsharded") == 0 &&
		singleElement.bsonValue.value_type == BSON_TYPE_DOCUMENT)
	{
		DeserializeUpdateUnshardedWorkerSpec(&singleElement.bsonValue, params);
		return;
	}
	else if (strcmp(singleElement.path, "updateOne") != 0 ||
			 singleElement.bsonValue.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																		"Update worker only supports updateOne or updateUnsharded as a document"))));
	}

	params->isUpdateOne = true;
	UpdateOneParams *updateOneParams = &params->param.updateOne;
	BsonValueInitIterator(&singleElement.bsonValue, &internalIter);
	while (bson_iter_next(&internalIter))
	{
		const char *key = bson_iter_key(&internalIter);
		if (strcmp(key, "shardKeyBson") == 0)
		{
			params->shardKeyBson = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																	   &internalIter));
		}
		else if (strcmp(key, "isUpsert") == 0)
		{
			updateOneParams->isUpsert = bson_iter_bool(&internalIter);
		}
		else if (strcmp(key, "arrayFilters") == 0)
		{
			updateOneParams->arrayFilters = PgbsonInitFromDocumentBsonValue(
				bson_iter_value(&internalIter));
		}
		else if (strcmp(key, "query") == 0)
		{
			updateOneParams->query = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		 &internalIter));
		}
		else if (strcmp(key, "sort") == 0)
		{
			updateOneParams->sort = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		&internalIter));
		}
		else if (strcmp(key, "update") == 0)
		{
			updateOneParams->update = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		  &internalIter));
		}
		else if (strcmp(key, "returnFields") == 0)
		{
			updateOneParams->returnFields = PgbsonInitFromDocumentBsonValue(
				bson_iter_value(&internalIter));
		}
		else if (strcmp(key, "returnDocument") == 0)
		{
			updateOneParams->returnDocument =
				(UpdateReturnValue) bson_iter_int32(&internalIter);
		}
		else if (strcmp(key, "bypassDocumentValidation") == 0)
		{
			params->bypassDocumentValidation = bson_iter_bool(&internalIter);
		}
	}
}


static pgbson *
SerializeUpdateOneResult(UpdateOneResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendBool(&writer, "isRowUpdated", -1, result->isRowUpdated);
	PgbsonWriterAppendBool(&writer, "updateSkipped", -1, result->updateSkipped);
	PgbsonWriterAppendBool(&writer, "isRetry", -1, result->isRetry);

	if (result->reinsertDocument != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "reinsertDocument", -1,
								   result->reinsertDocument);
	}

	if (result->resultDocument != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "resultDocument", -1, result->resultDocument);
	}

	if (result->upsertedObjectId != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "upsertedObjectId", -1,
								   result->upsertedObjectId);
	}

	return PgbsonWriterGetPgbson(&writer);
}


/* Serializes the batch update result as a pgbson to be sent back to the coordinator. We return the raw response and ok: true if there were no write errors otherwise false. */
static pgbson *
SerializeBatchUpdateResult(BatchUpdateResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendDocument(&writer, "response", -1, BuildResponseMessage(result));
	PgbsonWriterAppendBool(&writer, "ok", -1, result->writeErrors == NIL);

	return PgbsonWriterGetPgbson(&writer);
}


static void
DeserializeUpdateOneResult(pgbson *resultBson, UpdateOneResult *result)
{
	bson_iter_t updateIter;
	PgbsonInitIterator(resultBson, &updateIter);

	while (bson_iter_next(&updateIter))
	{
		const char *key = bson_iter_key(&updateIter);
		if (strcmp(key, "isRowUpdated") == 0)
		{
			result->isRowUpdated = bson_iter_bool(&updateIter);
		}
		else if (strcmp(key, "updateSkipped") == 0)
		{
			result->updateSkipped = bson_iter_bool(&updateIter);
		}
		else if (strcmp(key, "isRowUpdated") == 0)
		{
			result->isRowUpdated = bson_iter_bool(&updateIter);
		}
		else if (strcmp(key, "updateSkipped") == 0)
		{
			result->updateSkipped = bson_iter_bool(&updateIter);
		}
		else if (strcmp(key, "isRetry") == 0)
		{
			result->isRetry = bson_iter_bool(&updateIter);
		}
		else if (strcmp(key, "reinsertDocument") == 0)
		{
			result->reinsertDocument = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		   &updateIter));
		}
		else if (strcmp(key, "resultDocument") == 0)
		{
			result->resultDocument = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		 &updateIter));
		}
		else if (strcmp(key, "upsertedObjectId") == 0)
		{
			result->upsertedObjectId = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		   &updateIter));
		}
	}
}


/*
 * UpdateOneInternal updates a single document with a specific shard key value filter.
 * Returns whether a document was updated and if so sets the updatedDocument and
 * whether reinsertion will be required (due to shard key value change).
 */
static void
UpdateOneInternal(MongoCollection *collection, UpdateOneParams *updateOneParams,
				  int64 shardKeyHash, UpdateOneResult *result,
				  ExprEvalState *stateForSchemaValidation)
{
	/* initialize result */
	result->isRowUpdated = false;
	result->updateSkipped = false;
	result->isRetry = false;
	result->reinsertDocument = NULL;
	result->resultDocument = NULL;
	result->upsertedObjectId = NULL;

	UpdateCandidate updateCandidate = { 0 };

	bool getExistingDoc = updateOneParams->returnDocument != UPDATE_RETURNS_NONE ||
						  updateOneParams->returnFields != NULL ||
						  NeedExistingDocForValidation(stateForSchemaValidation,
													   collection);
	bool hasOnlyObjectIdFilter = false;
	bool foundDocument = SelectUpdateCandidate(collection->collectionId,
											   collection->shardTableName,
											   shardKeyHash,
											   updateOneParams->query,
											   updateOneParams->update,
											   updateOneParams->arrayFilters,
											   updateOneParams->sort,
											   &updateCandidate,
											   getExistingDoc,
											   &hasOnlyObjectIdFilter);

	if (!foundDocument)
	{
		/* no documents matched the query */

		if (updateOneParams->isUpsert)
		{
			pgbson *emptyDocument = PgbsonInitEmpty();
			pgbson *newDoc = BsonUpdateDocument(emptyDocument, updateOneParams->update,
												updateOneParams->query,
												updateOneParams->arrayFilters);

			pgbson *objectId = PgbsonGetDocumentId(newDoc);

			int64 newShardKeyHash =
				ComputeShardKeyHashForDocument(collection->shardKey,
											   collection->collectionId, newDoc);

			if (EnableSchemaValidation && stateForSchemaValidation != NULL)
			{
				bson_value_t newDocValue = ConvertPgbsonToBsonValue(newDoc);
				ValidateSchemaOnDocumentInsert(stateForSchemaValidation, &newDocValue,
											   FAILED_VALIDATION_ERROR_MSG);
			}

			if (newShardKeyHash == shardKeyHash)
			{
				/* shard key unchanged, upsert now */
				DoInsertForUpdate(collection, newShardKeyHash, objectId, newDoc,
								  hasOnlyObjectIdFilter);
				result->reinsertDocument = NULL;
			}
			else
			{
				/* shard key changed, reinsert via coordinator */
				result->reinsertDocument = newDoc;
			}

			if (updateOneParams->returnDocument == UPDATE_RETURNS_NEW)
			{
				result->resultDocument = newDoc;
			}

			result->upsertedObjectId = objectId;
		}
		else
		{
			/*
			 * Update becomes a noop if we couldn't match any documents and
			 * upsert is false, but we still need to report errors due to
			 * invalid update documents.
			 */
			ValidateUpdateDocument(updateOneParams->update,
								   updateOneParams->query,
								   updateOneParams->arrayFilters);
		}
	}
	else
	{
		/* if NULL no update was performed */
		if (updateCandidate.updatedDocument != (Datum) 0)
		{
			pgbson *updatedPgbson = DatumGetPgBson(updateCandidate.updatedDocument);
			int64 newShardKeyHash =
				ComputeShardKeyHashForDocument(collection->shardKey,
											   collection->collectionId,
											   updatedPgbson);

			if (EnableSchemaValidation && stateForSchemaValidation != NULL)
			{
				pgbson *sourceDocument = updateCandidate.originalDocument != (Datum) 0
										 ? DatumGetPgBson(
					updateCandidate.originalDocument)
										 : NULL;
				ValidateSchemaOnDocumentUpdate(
					collection->schemaValidator.validationLevel, stateForSchemaValidation,
					sourceDocument,
					updatedPgbson, FAILED_VALIDATION_ERROR_MSG);
			}

			if (newShardKeyHash == shardKeyHash)
			{
				/*
				 * Shard key is not affected by the update. Do an "in-place" update
				 * (in the same shard placement).
				 */
				if (IsYugaByteEnabled())
					result->isRowUpdated = YbUpdateDocumentByYbctid(
						collection->collectionId, collection->shardTableName,
						shardKeyHash, updateCandidate.ybctid, updatedPgbson);
				else
					result->isRowUpdated = UpdateDocumentByTID(
						collection->collectionId, collection->shardTableName,
						shardKeyHash, updateCandidate.tid, updatedPgbson);

				result->reinsertDocument = NULL;
			}
			else
			{
				/*
				 * Shard key is changed by the update. Delete the row and request
				 * reinsertion.
				 */
				if (IsYugaByteEnabled())
					result->isRowUpdated = YbDeleteDocumentByYbctid(
						collection->collectionId, shardKeyHash,
						updateCandidate.ybctid);
				else
					result->isRowUpdated =
						DeleteDocumentByTID(collection->collectionId,
											shardKeyHash, updateCandidate.tid);

				result->reinsertDocument = updatedPgbson;
			}

			if (updateOneParams->returnDocument == UPDATE_RETURNS_NEW)
			{
				/* output of bson_update_document */
				result->resultDocument = updatedPgbson;
			}
			else if (updateOneParams->returnDocument == UPDATE_RETURNS_OLD)
			{
				/* input of bson_update_document */
				result->resultDocument = DatumGetPgBson(updateCandidate.originalDocument);
			}
		}
		else
		{
			/* No update was performed */
			result->reinsertDocument = NULL;
			result->updateSkipped = true;
			result->resultDocument = updateCandidate.originalDocument == (Datum) 0 ?
									 NULL :
									 DatumGetPgBson(updateCandidate.originalDocument);
		}
	}

	/* project result document if requested */
	if (updateOneParams->returnFields != NULL && result->resultDocument != NULL)
	{
		bool forceProjectId = false;
		bool allowInclusionExclusion = false;
		bson_iter_t projectIter;
		PgbsonInitIterator(updateOneParams->returnFields, &projectIter);

		const BsonProjectionQueryState *projectionState =
			GetProjectionStateForBsonProject(&projectIter,
											 forceProjectId, allowInclusionExclusion);
		result->resultDocument = ProjectDocumentWithState(result->resultDocument,
														  projectionState);
	}
}


/*
 * SelectUpdateCandidate finds at most 1 document to update, locks the row,
 * writes the updated value of the document to updateCandidate (if no update happened it sets it to NULL),
 * and returns whether a document was found.
 */
static bool
SelectUpdateCandidate(uint64 collectionId, const char *shardTableName, int64 shardKeyHash,
					  pgbson *query,
					  pgbson *update, pgbson *arrayFilters, pgbson *sort,
					  UpdateCandidate *updateCandidate, bool getOriginalDocument,
					  bool *hasOnlyObjectIdFilter)
{
	uint64 planId = QUERY_UPDATE_SELECT_UPDATE_CANDIDATE;
	StringInfoData updateQuery;
	List *sortFieldDocuments = sort != NULL ? PgbsonDecomposeFields(sort) : NIL;

	bool queryHasNonIdFilters = false;
	pgbson *objectIdFilter = GetObjectIdFilterFromQueryDocument(query,
																&queryHasNonIdFilters);
	*hasOnlyObjectIdFilter = objectIdFilter != NULL && !queryHasNonIdFilters;

	int argCount = 2 + list_length(sortFieldDocuments);
	argCount += objectIdFilter != NULL ? 1 : 0;

	Oid *argTypes = palloc(sizeof(Oid) * argCount);
	Datum *argValues = palloc(sizeof(Datum) * argCount);
	char *argNulls = palloc(sizeof(char) * argCount);

	/* Initialize all tonull */
	memset(argNulls, 'n', argCount);
	bool foundDocument = false;
	int varArgsOffset = 2;

	SPI_connect();

	initStringInfo(&updateQuery);

	if (IsYugaByteEnabled())
		appendStringInfo(&updateQuery, "SELECT ybctid, object_id, document FROM");
	else
		appendStringInfo(&updateQuery, "SELECT ctid, object_id, document FROM");

	if (shardTableName != NULL && shardTableName[0] != '\0')
	{
		appendStringInfo(&updateQuery, " %s.%s", ApiDataSchemaName, shardTableName);
	}
	else
	{
		appendStringInfo(&updateQuery, " %s.documents_" UINT64_FORMAT, ApiDataSchemaName,
						 collectionId);
	}

	appendStringInfo(&updateQuery, " WHERE shard_key_value = $1");

	if (queryHasNonIdFilters)
	{
		planId = QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_NON_OBJECT_ID;
		appendStringInfo(&updateQuery, " AND document OPERATOR(%s.@@) $2::%s",
						 ApiCatalogSchemaName, FullBsonTypeName);
	}

	if (objectIdFilter != NULL)
	{
		planId = queryHasNonIdFilters ?
				 QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_BOTH_FILTER :
				 QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_ONLY_OBJECT_ID;

		appendStringInfo(&updateQuery,
						 " AND object_id OPERATOR(%s.=) $3::%s", CoreSchemaName,
						 FullBsonTypeName);
		argTypes[varArgsOffset] = BYTEAOID;
		argValues[varArgsOffset] = PointerGetDatum(CastPgbsonToBytea(objectIdFilter));
		argNulls[varArgsOffset] = ' ';
		varArgsOffset++;
	}

	if (list_length(sortFieldDocuments) > 0)
	{
		planId = 0;
		appendStringInfoString(&updateQuery, " ORDER BY");

		for (int i = 0; i < list_length(sortFieldDocuments); i++)
		{
			int sqlArgNumber = i + varArgsOffset + 1;
			pgbson *sortDoc = list_nth(sortFieldDocuments, i);
			bool isAscending = ValidateOrderbyExpressionAndGetIsAscending(sortDoc);
			appendStringInfo(&updateQuery,
							 "%s %s.bson_orderby(document, $%d::%s) %s",
							 i > 0 ? "," : "", ApiCatalogSchemaName, sqlArgNumber,
							 FullBsonTypeName,
							 isAscending ? "ASC" : "DESC");

			argTypes[i + varArgsOffset] = BYTEAOID;
			argValues[i + varArgsOffset] =
				PointerGetDatum(CastPgbsonToBytea(sortDoc));
			argNulls[i + varArgsOffset] = ' ';
		}
	}

	appendStringInfo(&updateQuery,
					 " LIMIT 1 FOR UPDATE");

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyHash);
	argNulls[0] = ' ';

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(query));
	argNulls[1] = ' ';

	bool readOnly = false;
	long maxTupleCount = 1;

	if (planId == 0)
	{
		SPI_execute_with_args(updateQuery.data, argCount, argTypes, argValues, argNulls,
							  readOnly, maxTupleCount);
		Assert(SPI_processed <= 1);
	}
	else
	{
		SPIPlanPtr plan = GetSPIQueryPlanWithLocalShard(collectionId,
														shardTableName,
														planId, updateQuery.data,
														argTypes,
														argCount);

		SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
		Assert(SPI_processed <= 1);
	}

	foundDocument = SPI_processed > 0;
	if (foundDocument && updateCandidate != NULL)
	{
		int rowIndex = 0;

		bool typeByValue = false;
		bool isNull = false;

		int columnNumber = 1;
		Datum tidDatum = SPI_getbinval(SPI_tuptable->vals[rowIndex],
									   SPI_tuptable->tupdesc,
									   columnNumber, &isNull);
		Assert(!isNull);

		int typeLength = sizeof(ItemPointerData);

		if (IsYugaByteEnabled())
			typeLength = -1;

		tidDatum = SPI_datumTransfer(tidDatum, typeByValue, typeLength);

		if (IsYugaByteEnabled())
			updateCandidate->ybctid = tidDatum;
		else
			updateCandidate->tid = DatumGetItemPointer(tidDatum);

		columnNumber = 2;
		Datum objectIdDatum = SPI_getbinval(SPI_tuptable->vals[rowIndex],
											SPI_tuptable->tupdesc,
											columnNumber, &isNull);
		Assert(!isNull);

		typeLength = -1;
		objectIdDatum = SPI_datumTransfer(objectIdDatum, typeByValue, typeLength);

		updateCandidate->objectId = objectIdDatum;

		columnNumber = 3;
		Datum originalDocumentDatum = SPI_getbinval(SPI_tuptable->vals[rowIndex],
													SPI_tuptable->tupdesc,
													columnNumber, &isNull);
		Assert(!isNull);

		/* Do this inside the SPI context so that the memory gets cleaned up once we close the SPI session. */
		pgbson *originalDoc = DatumGetPgBson(originalDocumentDatum);
		pgbson *updatedDocument = BsonUpdateDocument(originalDoc, update, query,
													 arrayFilters);

		if (updatedDocument != NULL)
		{
			Datum updatedDatum = PointerGetDatum(updatedDocument);
			updateCandidate->updatedDocument = SPI_datumTransfer(updatedDatum,
																 typeByValue, typeLength);
		}
		else
		{
			updateCandidate->updatedDocument = (Datum) 0;
		}

		if (getOriginalDocument)
		{
			typeLength = -1;
			originalDocumentDatum = SPI_datumTransfer(originalDocumentDatum,
													  typeByValue, typeLength);
			updateCandidate->originalDocument = originalDocumentDatum;
		}
		else
		{
			updateCandidate->originalDocument = (Datum) 0;
		}
	}

	SPI_finish();
	return foundDocument;
}


/*
 * UpdateDocumentByTID performs a TID update on a single shard.
 *
 * The TID must be obtained via SelectUpdateCandidate in the current transaction.
 */
static bool
UpdateDocumentByTID(uint64 collectionId, const char *shardTableName,
					int64 shardKeyHash, ItemPointer tid,
					pgbson *updatedDocument)
{
	StringInfoData updateQuery;
	int argCount = 3;
	Oid argTypes[3];
	Datum argValues[3];

	/* whitespace means not null, n means null */
	char argNulls[3] = { ' ', ' ', ' ' };

	SPI_connect();

	initStringInfo(&updateQuery);

	appendStringInfo(&updateQuery, "UPDATE ");

	if (shardTableName != NULL && shardTableName[0] != '\0')
	{
		appendStringInfo(&updateQuery, "%s.%s", ApiDataSchemaName, shardTableName);
	}
	else
	{
		appendStringInfo(&updateQuery, "%s.documents_" UINT64_FORMAT,
						 ApiDataSchemaName, collectionId);
	}

	appendStringInfo(&updateQuery,
					 " SET document = $3::%s"
					 " WHERE ctid = $2 AND shard_key_value = $1",
					 FullBsonTypeName);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyHash);

	argTypes[1] = TIDOID;
	argValues[1] = ItemPointerGetDatum(tid);

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[2] = BYTEAOID;
	argValues[2] = PointerGetDatum(CastPgbsonToBytea(updatedDocument));

	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlanWithLocalShard(collectionId, shardTableName,
													QUERY_ID_UPDATE_BY_TID,
													updateQuery.data, argTypes, argCount);

	SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
	Assert(SPI_processed == 1);

	SPI_finish();

	return true;
}

/*
 * DeleteDocumentByTID performs a TID delete on a single shard.
 *
 * The TID must be obtained via SelectUpdateCandidate in the current transaction.
 */
static bool
DeleteDocumentByTID(uint64 collectionId, int64 shardKeyHash, ItemPointer tid)
{
	StringInfoData deleteQuery;
	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];

	SPI_connect();

	initStringInfo(&deleteQuery);
	appendStringInfo(&deleteQuery,
					 "DELETE FROM %s.documents_" UINT64_FORMAT
					 " WHERE ctid = $2 AND shard_key_value = $1",
					 ApiDataSchemaName, collectionId);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyHash);

	argTypes[1] = TIDOID;
	argValues[1] = ItemPointerGetDatum(tid);

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlan(collectionId, QUERY_ID_DELETE_BY_TID,
									  deleteQuery.data, argTypes, argCount);

	SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);

	Assert(SPI_processed == 1);

	SPI_finish();

	return true;
}

/*
 * UpdateOneObjectId handles the case where we are updating a single document
 * by _id from a collection that is sharded on some other key. In this case,
 * we need to look across all shards for a matching _id, then update only that
 * one.
 *
 * Citus does not support SELECT .. FOR UPDATE, and it is very difficult to
 * support efficiently without running into frequent deadlocks. Therefore,
 * we instead do a regular SELECT. The implication is that the document might
 * be deleted or updated concurrently. In that case, we try again.
 */
static void
UpdateOneObjectId(MongoCollection *collection, UpdateOneParams *updateOneParams,
				  bson_value_t *objectId, text *transactionId,
				  UpdateOneResult *result, ExprEvalState *stateForSchemaValidation)
{
	/* initialize result */
	result->isRowUpdated = false;
	result->updateSkipped = false;
	result->isRetry = false;
	result->reinsertDocument = NULL;
	result->resultDocument = NULL;
	result->upsertedObjectId = NULL;

	const int maxTries = 5;

	if (transactionId != NULL)
	{
		RetryableWriteResult writeResult;

		/*
		 * Try to find a retryable write record for the transaction ID in any shard.
		 */
		if (FindRetryRecordInAnyShard(collection->collectionId, transactionId,
									  &writeResult))
		{
			/* found a retry record, return the previous result */
			result->isRetry = true;
			result->isRowUpdated = writeResult.rowsAffected;

			/* these writes are never upserts */
			result->upsertedObjectId = NULL;

			return;
		}
	}

	for (int tryNumber = 0; tryNumber < maxTries; tryNumber++)
	{
		int64 shardKeyValue = 0;

		if (!FindShardKeyValueForDocumentId(collection, updateOneParams->query, objectId,
											&shardKeyValue))
		{
			/* no document matches both the query and the object ID */
			return;
		}

		/* we do not support upsert without shard key filter */
		Assert(updateOneParams->isUpsert == false);

		bool forceInlineWrites = false;
		CallUpdateOne(collection, updateOneParams, shardKeyValue,
					  transactionId, result, forceInlineWrites, stateForSchemaValidation);

		if (result->isRowUpdated || result->updateSkipped)
		{
			if (result->reinsertDocument != NULL)
			{
				/* we could easily reinsert here, but Mongo does not support it */
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("shard key value update is only supported when "
									   "filtering by the full shard key and specifying "
									   "multi:false")));
			}

			/* updated the document or no update is needed */
			return;
		}
	}

	ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
					errmsg("failed to update document after %d tries", maxTries)));
}


/*
 * UpsertDocument performs an insert when an update did not match any rows
 * and returns the inserted object ID.
 */
static pgbson *
UpsertDocument(MongoCollection *collection, pgbson *update,
			   pgbson *query, pgbson *arrayFilters,
			   ExprEvalState *stateForSchemaValidation,
			   bool hasOnlyObjectIdFilter)
{
	pgbson *emptyDocument = PgbsonInitEmpty();
	pgbson *newDoc = BsonUpdateDocument(emptyDocument, update, query, arrayFilters);

	int64 newShardKeyHash =
		ComputeShardKeyHashForDocument(collection->shardKey, collection->collectionId,
									   newDoc);

	pgbson *objectId = PgbsonGetDocumentId(newDoc);

	if (EnableSchemaValidation && stateForSchemaValidation != NULL)
	{
		bson_value_t newDocValue = ConvertPgbsonToBsonValue(newDoc);
		ValidateSchemaOnDocumentInsert(stateForSchemaValidation, &newDocValue,
									   FAILED_VALIDATION_ERROR_MSG);
	}

	DoInsertForUpdate(collection, newShardKeyHash, objectId, newDoc,
					  hasOnlyObjectIdFilter);
	return objectId;
}


/*
 * ValidateQueryAndUpdateDocuments validates query and update documents of
 * each update specified by given BatchUpdateSpec and returns a list of write
 * errors.
 *
 * Stops after the first failure if the update mode is ordered.
 *
 * This is useful for performing query / update document validations when we
 * certainly know that the update operation would become a noop due to
 * non-existent collection.
 *
 * Otherwise, i.e. if the update operaton wouldn't become a noop, then it
 * doesn't make sense to call this function both because we already perform
 * those validations at the runtime and also because this is quite expensive,
 * meaning that this function indeed processes "query" and "update" documents
 * as if we're in the run-time to implicitly perform necessary validations.
 */
static List *
ValidateQueryAndUpdateDocuments(BatchUpdateSpec *batchSpec)
{
	/* declared volatile because of the longjmp in PG_CATCH */
	List *volatile writeErrorList = NIL;

	/*
	 * Weirdly, compiler complains that writeErrorIdx might be clobbered by
	 * longjmp in PG_CATCH, so declare writeErrorIdx as volatile as well.
	 */
	for (volatile int writeErrorIdx = 0;
		 writeErrorIdx < list_length(batchSpec->updates);
		 writeErrorIdx++)
	{
		UpdateSpec *updateSpec = list_nth(batchSpec->updates, writeErrorIdx);

		/* declared volatile because of the longjmp in PG_CATCH */
		volatile bool isSuccess = false;

		MemoryContext oldContext = CurrentMemoryContext;
		PG_TRY();
		{
			ValidateQueryDocument(updateSpec->updateOneParams.query);
			ValidateUpdateDocument(updateSpec->updateOneParams.update,
								   updateSpec->updateOneParams.query,
								   updateSpec->updateOneParams.arrayFilters);
			isSuccess = true;
		}
		PG_CATCH();
		{
			MemoryContextSwitchTo(oldContext);
			ErrorData *errorData = CopyErrorDataAndFlush();

			writeErrorList = lappend(writeErrorList, GetWriteErrorFromErrorData(errorData,
																				writeErrorIdx));
			isSuccess = false;
		}
		PG_END_TRY();

		if (!isSuccess && batchSpec->isOrdered)
		{
			/*
			 * Stop validating query / update documents after a failure if
			 * using ordered:true.
			 */
			break;
		}
	}

	return writeErrorList;
}


/*
 * BuildResponseMessage builds the response BSON for an update command.
 */
static pgbson *
BuildResponseMessage(BatchUpdateResult *batchResult)
{
	pgbson_writer resultWriter;
	PgbsonWriterInit(&resultWriter);
	PgbsonWriterAppendDouble(&resultWriter, "ok", 2, batchResult->ok);
	PgbsonWriterAppendInt64(&resultWriter, "nModified", 9, batchResult->rowsModified);
	PgbsonWriterAppendInt64(&resultWriter, "n", 1, batchResult->rowsMatched);

	if (batchResult->upserted != NIL)
	{
		pgbson_array_writer upsertedArrayWriter;
		PgbsonWriterStartArray(&resultWriter, "upserted", 8, &upsertedArrayWriter);

		ListCell *upsertedCell = NULL;
		foreach(upsertedCell, batchResult->upserted)
		{
			UpsertResult *upsertResult = lfirst(upsertedCell);

			/* extract the object ID value */
			pgbsonelement objectIdElement;
			PgbsonToSinglePgbsonElement(upsertResult->objectId, &objectIdElement);

			pgbson_writer upsertResultWriter;
			PgbsonArrayWriterStartDocument(&upsertedArrayWriter, &upsertResultWriter);
			PgbsonWriterAppendInt32(&upsertResultWriter, "index", 5, upsertResult->index);
			PgbsonWriterAppendValue(&upsertResultWriter, "_id", 3,
									&objectIdElement.bsonValue);
			PgbsonArrayWriterEndDocument(&upsertedArrayWriter, &upsertResultWriter);
		}

		PgbsonWriterEndArray(&resultWriter, &upsertedArrayWriter);
	}


	if (batchResult->writeErrors != NIL)
	{
		pgbson_array_writer writeErrorsArrayWriter;
		PgbsonWriterStartArray(&resultWriter, "writeErrors", 11, &writeErrorsArrayWriter);

		ListCell *writeErrorCell = NULL;
		foreach(writeErrorCell, batchResult->writeErrors)
		{
			WriteError *writeError = lfirst(writeErrorCell);

			pgbson_writer writeErrorWriter;
			PgbsonArrayWriterStartDocument(&writeErrorsArrayWriter, &writeErrorWriter);
			PgbsonWriterAppendInt32(&writeErrorWriter, "index", 5, writeError->index);
			PgbsonWriterAppendInt32(&writeErrorWriter, "code", 4, writeError->code);
			PgbsonWriterAppendUtf8(&writeErrorWriter, "errmsg", 6, writeError->errmsg);
			PgbsonArrayWriterEndDocument(&writeErrorsArrayWriter, &writeErrorWriter);
		}

		PgbsonWriterEndArray(&resultWriter, &writeErrorsArrayWriter);
	}

	return PgbsonWriterGetPgbson(&resultWriter);
}

/*
 * YbUpdateDocumentByYbctid performs a ybctid based update. Borrows the logic from UpdateDocumentByTID.
 *
 * The ybctid must be obtained via SelectUpdateCandidate in the current
 * transaction.
 */
static bool
YbUpdateDocumentByYbctid(uint64 collectionId, const char *shardTableName,
						 int64 shardKeyHash, Datum ybctid,
						 pgbson *updatedDocument)
{
	StringInfoData updateQuery;
	int argCount = 3;
	Oid argTypes[3];
	Datum argValues[3];

	/* whitespace means not null, n means null */
	char argNulls[3] = {' ', ' ', ' '};

	SPI_connect();

	initStringInfo(&updateQuery);
	appendStringInfoString(&updateQuery, "UPDATE ");

	if (shardTableName != NULL && shardTableName[0] != '\0')
	{
		appendStringInfo(&updateQuery, "%s.%s", ApiDataSchemaName,
						 shardTableName);
	}
	else
	{
		appendStringInfo(&updateQuery, "%s.documents_" UINT64_FORMAT,
						 ApiDataSchemaName, collectionId);
	}

	appendStringInfo(&updateQuery,
					 " SET document = $3::%s"
					 " WHERE ybctid = $2 AND shard_key_value = $1",
					 FullBsonTypeName);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyHash);

	argTypes[1] = BYTEAOID;
	argValues[1] = ybctid;

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[2] = BYTEAOID;
	argValues[2] = PointerGetDatum(CastPgbsonToBytea(updatedDocument));

	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlanWithLocalShard(
		collectionId, shardTableName, QUERY_ID_UPDATE_BY_TID, updateQuery.data,
		argTypes, argCount);

	SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
	Assert(SPI_processed == 1);

	SPI_finish();

	return true;
}

/*
 * YbDeleteDocumentByYbctid performs a ybctid based delete. Borrows the logic from DeleteDocumentByTID.
 *
 * The ybctid must be obtained via SelectUpdateCandidate in the current
 * transaction.
 */
static bool
YbDeleteDocumentByYbctid(uint64 collectionId, int64 shardKeyHash, Datum ybctid)
{
	StringInfoData deleteQuery;
	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];

	SPI_connect();

	initStringInfo(&deleteQuery);
	appendStringInfo(&deleteQuery,
					 "DELETE FROM %s.documents_" UINT64_FORMAT " WHERE "
					 "ybctid = $2 AND shard_key_value = $1",
					 ApiDataSchemaName, collectionId);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyHash);

	argTypes[1] = BYTEAOID;
	argValues[1] = ybctid;

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlan(collectionId, QUERY_ID_DELETE_BY_TID,
									  deleteQuery.data, argTypes, argCount);

	SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);

	Assert(SPI_processed == 1);

	SPI_finish();

	return true;
}
