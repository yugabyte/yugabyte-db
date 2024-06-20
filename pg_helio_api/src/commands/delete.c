/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/delete.c
 *
 * Implementation of the delete command.
 *
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

#include "io/helio_bson_core.h"
#include "aggregation/bson_project.h"
#include "aggregation/bson_query.h"
#include "commands/commands_common.h"
#include "commands/delete.h"
#include "commands/parse_error.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "infrastructure/helio_plan_cache.h"
#include "sharding/sharding.h"
#include "commands/retryable_writes.h"
#include "io/pgbsonsequence.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "utils/query_utils.h"
#include "api_hooks.h"

/*
 * DeletionSpec describes a single delete operation.
 */
typedef struct
{
	DeleteOneParams deleteOneParams;

	/* delete limit (0 for all rows, 1 for 1 row) */
	int limit;
} DeletionSpec;


/*
 * BatchDeletionSpec describes a batch of delete operations.
 */
typedef struct
{
	/* collection in which to perform deletions */
	char *collectionName;

	/* DeletionSpec list describing the deletions */
	List *deletions;

	/* if ordered, stop after the first failure */
	bool isOrdered;
} BatchDeletionSpec;


/*
 * BatchDeletionResult contains the results that are sent to the
 * client after a delete command.
 */
typedef struct
{
	/* response status (seems to always be 1?) */
	double ok;

	/* number of rows deleted */
	uint64 rowsDeleted;

	/* list of write errors for each deletion, or NIL */
	List *writeErrors;
} BatchDeletionResult;


PG_FUNCTION_INFO_V1(command_delete);
PG_FUNCTION_INFO_V1(command_delete_one);
PG_FUNCTION_INFO_V1(command_delete_worker);


static BatchDeletionSpec * BuildBatchDeletionSpec(bson_iter_t *deleteCommandIter,
												  pgbsonsequence *deleteDocs);
static List * BuildDeletionSpecList(bson_iter_t *deleteArrayIter);
static List * BuildDeletionSpecListFromSequence(pgbsonsequence *sequence);
static DeletionSpec * BuildDeletionSpec(bson_iter_t *deletionIterator);
static void ProcessBatchDeletion(MongoCollection *collection,
								 BatchDeletionSpec *batchSpec,
								 text *transactionId,
								 BatchDeletionResult *batchResult);
static uint64 ProcessDeletion(MongoCollection *collection, DeletionSpec *deletionSpec,
							  text *transactionId);
static uint64 DeleteAllMatchingDocuments(MongoCollection *collection, pgbson *query,
										 bool hasShardKeyValueFilter,
										 int64 shardKeyHash);
static void DeleteOneInternal(uint64 collectionId, DeleteOneParams *deleteOneParams,
							  int64 shardKeyHash, DeleteOneResult *result);
static void DeleteOneObjectId(MongoCollection *collection,
							  DeleteOneParams *deleteOneParams,
							  bson_value_t *objectId, text *transactionId,
							  DeleteOneResult *result);
static List * ValidateQueryDocuments(BatchDeletionSpec *batchSpec);
static pgbson * BuildResponseMessage(BatchDeletionResult *batchResult);
static void CallDeleteOneCore(MongoCollection *collection,
							  DeleteOneParams *deleteOneParams,
							  int64 shardKeyHash, text *transactionId,
							  DeleteOneResult *result);
static void DeleteOneInternalCore(uint64 collectionId, int64 shardKeyHash,
								  DeleteOneParams *deleteOneParams,
								  text *transactionId, DeleteOneResult *deleteOneResult);
static void CallDeleteWorkerForDeleteOne(MongoCollection *collection,
										 DeleteOneParams *deleteOneParams,
										 int64 shardKeyHash, text *transactionId,
										 DeleteOneResult *result);
static pgbson * SerializeDeleteOneParams(const DeleteOneParams *deleteParams);
static void DeserializeDeleteWorkerSpec(pgbson *workerSpec,
										DeleteOneParams *deleteOneParams);
static void DeserializeWorkerDeleteResult(pgbson *resultBson, DeleteOneResult *result);
static pgbson * SerializeDeleteOneResult(DeleteOneResult *result);

/*
 * command_delete handles a single delete on a collection.
 */
Datum
command_delete(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("database name cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("delete document cannot be NULL")));
	}

	Datum databaseNameDatum = PG_GETARG_DATUM(0);
	pgbson *deleteSpec = PG_GETARG_PGBSON(1);

	pgbsonsequence *deleteDocs = PG_GETARG_MAYBE_NULL_PGBSON_SEQUENCE(2);

	text *transactionId = NULL;
	if (!PG_ARGISNULL(3))
	{
		transactionId = PG_GETARG_TEXT_P(3);
	}

	ReportFeatureUsage(FEATURE_COMMAND_DELETE);

	/* fetch TupleDesc for return value, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc;
	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	bson_iter_t deleteCommandIter;
	PgbsonInitIterator(deleteSpec, &deleteCommandIter);

	/*
	 * We first validate delete command BSON and build a specification.
	 */
	BatchDeletionSpec *batchSpec = BuildBatchDeletionSpec(&deleteCommandIter, deleteDocs);

	BatchDeletionResult batchResult;

	Datum collectionNameDatum = CStringGetTextDatum(batchSpec->collectionName);
	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseNameDatum, collectionNameDatum,
									  RowExclusiveLock);
	if (collection != NULL)
	{
		ProcessBatchDeletion(collection, batchSpec, transactionId,
							 &batchResult);
	}
	else
	{
		StringView collectionView = {
			.length = VARSIZE_ANY_EXHDR(collectionNameDatum),
			.string = VARDATA_ANY(collectionNameDatum)
		};

		ValidateCollectionNameForValidSystemNamespace(&collectionView,
													  databaseNameDatum);

		/*
		 * Delete on non-existent collection is a noop, but we still need to
		 * report (write) errors due to invalid query documents.
		 */
		batchResult.ok = 1;
		batchResult.rowsDeleted = 0;
		batchResult.writeErrors = ValidateQueryDocuments(batchSpec);
	}

	Datum values[2];
	bool isNulls[2] = { false, false };

	values[0] = PointerGetDatum(BuildResponseMessage(&batchResult));
	values[1] = BoolGetDatum(batchResult.writeErrors == NIL);
	HeapTuple resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
	PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
}


/*
 * BuildBatchDeletionSpec validates the delete command BSON and builds
 * a BatchDeletionSpec.
 */
static BatchDeletionSpec *
BuildBatchDeletionSpec(bson_iter_t *deleteCommandIter, pgbsonsequence *deleteDocs)
{
	const char *collectionName = NULL;
	List *deletions = NIL;
	bool isOrdered = true;
	bool hasDeletes = false;

	while (bson_iter_next(deleteCommandIter))
	{
		const char *field = bson_iter_key(deleteCommandIter);

		if (strcmp(field, "delete") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(deleteCommandIter))
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg("collection name has invalid type %s",
									   BsonIterTypeName(deleteCommandIter))));
			}

			collectionName = bson_iter_utf8(deleteCommandIter, NULL);
		}
		else if (strcmp(field, "deletes") == 0)
		{
			EnsureTopLevelFieldType("delete.deletes", deleteCommandIter, BSON_TYPE_ARRAY);

			if (deleteDocs != NULL)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg("Unexpected additional deletes")));
			}

			bson_iter_t deleteArrayIter;
			bson_iter_recurse(deleteCommandIter, &deleteArrayIter);

			deletions = BuildDeletionSpecList(&deleteArrayIter);
			hasDeletes = true;
		}
		else if (strcmp(field, "ordered") == 0)
		{
			EnsureTopLevelFieldType("delete.ordered", deleteCommandIter, BSON_TYPE_BOOL);

			isOrdered = bson_iter_bool(deleteCommandIter);
		}
		else if (IsCommonSpecIgnoredField(field))
		{
			elog(DEBUG1, "Unrecognized command field: delete.%s", field);

			/*
			 *  Silently ignore now, so that clients don't break
			 *  TODO: implement me
			 *      writeConcern
			 *      let
			 */
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							errmsg("BSON field 'delete.%s' is an unknown field",
								   field)));
		}
	}

	if (collectionName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("BSON field 'delete.delete' is missing but "
							   "a required field")));
	}

	if (deleteDocs != NULL)
	{
		deletions = BuildDeletionSpecListFromSequence(deleteDocs);
		hasDeletes = true;
	}

	if (!hasDeletes)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("BSON field 'delete.deletes' is missing but "
							   "a required field")));
	}

	int deletionCount = list_length(deletions);
	if (deletionCount == 0 || deletionCount > MaxWriteBatchSize)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("Write batch sizes must be between 1 and %d. "
							   "Got %d operations.", MaxWriteBatchSize, deletionCount)));
	}

	BatchDeletionSpec *batchSpec = palloc0(sizeof(BatchDeletionSpec));

	batchSpec->collectionName = (char *) collectionName;
	batchSpec->deletions = deletions;
	batchSpec->isOrdered = isOrdered;

	return batchSpec;
}


/*
 * BuildDeletionSpecList iterates over an array of delete operations and
 * builds a Deletion for each object.
 */
static List *
BuildDeletionSpecList(bson_iter_t *deleteArrayIter)
{
	List *deletions = NIL;

	while (bson_iter_next(deleteArrayIter))
	{
		StringInfo fieldNameStr = makeStringInfo();
		int arrIdx = list_length(deletions);
		appendStringInfo(fieldNameStr, "delete.deletes.%d", arrIdx);

		EnsureTopLevelFieldType(fieldNameStr->data, deleteArrayIter, BSON_TYPE_DOCUMENT);

		bson_iter_t deleteOperationIter;
		bson_iter_recurse(deleteArrayIter, &deleteOperationIter);

		DeletionSpec *deletion = BuildDeletionSpec(&deleteOperationIter);

		deletions = lappend(deletions, deletion);
	}

	return deletions;
}


/*
 * BuildDeletionSpecFromSequence builds a list of DeletionSpec from a BsonSequence.
 */
static List *
BuildDeletionSpecListFromSequence(pgbsonsequence *sequence)
{
	List *deletions = NIL;

	List *documents = PgbsonSequenceGetDocumentBsonValues(sequence);
	ListCell *documentCell;
	foreach(documentCell, documents)
	{
		bson_iter_t deleteOperationIter;
		BsonValueInitIterator(lfirst(documentCell), &deleteOperationIter);

		DeletionSpec *deletion = BuildDeletionSpec(&deleteOperationIter);

		deletions = lappend(deletions, deletion);
	}

	return deletions;
}


/*
 * BuildDeletionSpec builds a DeletionSpec from the BSON of a single delete
 * operation.
 */
static DeletionSpec *
BuildDeletionSpec(bson_iter_t *deletionIter)
{
	pgbson *query = NULL;
	int64 limit = -1;

	while (bson_iter_next(deletionIter))
	{
		const char *field = bson_iter_key(deletionIter);

		if (strcmp(field, "q") == 0)
		{
			EnsureTopLevelFieldType("delete.deletes.q", deletionIter, BSON_TYPE_DOCUMENT);

			query = PgbsonInitFromIterDocumentValue(deletionIter);
		}
		else if (strcmp(field, "limit") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(deletionIter))
			{
				/* for some reason, Mongo treats arbitrary types as valid limit 0 */
				limit = 0;
			}
			else
			{
				limit = bson_iter_as_int64(deletionIter);
				if (limit != 0 && limit != 1)
				{
					ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
									errmsg("The limit field in delete objects must be 0 "
										   "or 1. Got " INT64_FORMAT, limit)));
				}
			}
		}
		else if (strcmp(field, "collation") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("BSON field 'delete.deletes.collation' is not yet "
								   "supported")));
		}
		else if (strcmp(field, "hint") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("BSON field 'delete.deletes.hint' is not yet "
								   "supported")));
		}
		else if (strcmp(field, "comment") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("BSON field 'delete.deletes.comment' is not yet "
								   "supported")));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							errmsg("BSON field 'delete.deletes.%s' is an unknown field",
								   field)));
		}
	}

	if (query == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("BSON field 'delete.deletes.q' is missing but "
							   "a required field")));
	}

	if (limit == -1)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("BSON field 'delete.deletes.limit' is missing but "
							   "a required field")));
	}

	DeletionSpec *deletionSpec = palloc0(sizeof(DeletionSpec));
	deletionSpec->deleteOneParams.query = query;
	deletionSpec->limit = limit;

	return deletionSpec;
}


/*
 * ProcessBatchDeletion iterates over the deletes array and executes each
 * deletion in a subtransaction, to allow us to continue after an error.
 *
 * If batchSpec->isOrdered is false, we continue with remaining tasks an
 * error.
 *
 * Using subtransactions is slightly different from Mongo, which effectively
 * does each delete operation in a separate transaction, but it has roughly
 * the same overall UX.
 */
static void
ProcessBatchDeletion(MongoCollection *collection, BatchDeletionSpec *batchSpec,
					 text *transactionId, BatchDeletionResult *batchResult)
{
	List *deletions = batchSpec->deletions;
	bool isOrdered = batchSpec->isOrdered;

	/*
	 * Execute the query inside a sub-transaction, so we can restore order
	 * after a failure.
	 */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	batchResult->ok = 1;
	batchResult->rowsDeleted = 0;
	batchResult->writeErrors = NIL;

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile int deleteIndex = 0;

	ListCell *deletionCell = NULL;
	foreach(deletionCell, deletions)
	{
		CHECK_FOR_INTERRUPTS();

		DeletionSpec *deletionSpec = lfirst(deletionCell);

		/* declared volatile because of the longjmp in PG_CATCH */
		volatile uint64 rowsDeleted = 0;
		volatile bool isSuccess = false;

		/* use a subtransaction to correctly handle failures */
		BeginInternalSubTransaction(NULL);

		PG_TRY();
		{
			rowsDeleted = ProcessDeletion(collection, deletionSpec, transactionId);

			/* Commit the inner transaction, return to outer xact context */
			ReleaseCurrentSubTransaction();
			MemoryContextSwitchTo(oldContext);
			CurrentResourceOwner = oldOwner;

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
																		  deleteIndex));

			isSuccess = false;
		}
		PG_END_TRY();

		if (!isSuccess && isOrdered)
		{
			/* stop trying delete operations after a failure if using ordered:true */
			break;
		}

		batchResult->rowsDeleted += rowsDeleted;
		deleteIndex++;
	}
}


/*
 * ProcessDeletion processes a single deletion operation defined in
 * deletionSpec on the given collection.
 */
static uint64
ProcessDeletion(MongoCollection *collection, DeletionSpec *deletionSpec,
				text *transactionId)
{
	if (deletionSpec->deleteOneParams.returnDeletedDocument)
	{
		ereport(ERROR, (errmsg("cannot return deleted document via "
							   "regular delete")));
	}

	pgbson *query = deletionSpec->deleteOneParams.query;

	/* determine whether query filters by a single shard key value */
	int64 shardKeyHash = 0;

	/* if the collection is sharded, check whether we can use a single hash value */
	bool hasShardKeyValueFilter =
		ComputeShardKeyHashForQuery(collection->shardKey, collection->collectionId, query,
									&shardKeyHash);

	/* determine whether query filters by a single object ID */
	bson_iter_t queryDocIter;
	PgbsonInitIterator(query, &queryDocIter);

	bson_value_t idFromQueryDocument = { 0 };
	bool errorOnConflict = false;
	bool hasObjectIdFilter =
		TraverseQueryDocumentAndGetId(&queryDocIter, &idFromQueryDocument,
									  errorOnConflict);

	if (deletionSpec->limit == 0)
	{
		/*
		 * Delete as many document as match the query. This is not a retryable
		 * operation, so we ignore transactionId.
		 */
		return DeleteAllMatchingDocuments(collection, query,
										  hasShardKeyValueFilter, shardKeyHash);
	}
	else
	{
		DeleteOneResult deleteOneResult = { 0 };

		if (hasShardKeyValueFilter)
		{
			/*
			 * Delete at most 1 document that matches the query on a single shard.
			 *
			 * For unsharded collection, this is the shard that contains all the
			 * data.
			 */
			CallDeleteOne(collection, &deletionSpec->deleteOneParams, shardKeyHash,
						  transactionId, &deleteOneResult);
		}
		else if (hasObjectIdFilter)
		{
			/*
			 * Delete at most 1 document that matches an _id equality filter from
			 * a sharded collection without specifying a a shard key filter.
			 */
			DeleteOneObjectId(collection, &deletionSpec->deleteOneParams,
							  &idFromQueryDocument, transactionId, &deleteOneResult);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("delete query with limit 1 must include either "
								   "_id or shard key filter")));
		}

		return deleteOneResult.isRowDeleted ? 1 : 0;
	}
}


/*
 * DeleteAllMatchingDocuments deletes all documents that match the query.
 */
static uint64
DeleteAllMatchingDocuments(MongoCollection *collection, pgbson *queryDoc,
						   bool hasShardKeyValueFilter, int64 shardKeyHash)
{
	uint64 collectionId = collection->collectionId;

	StringInfoData deleteQuery;

	pgbson *objectIdFilter = GetObjectIdFilterFromQueryDocument(queryDoc);

	int argCount = 1;
	Oid argTypes[3];
	Datum argValues[3];
	uint64 rowsDeleted = 0;

	uint64 planId = QUERY_DELETE_WITH_FILTER;
	SPI_connect();
	initStringInfo(&deleteQuery);
	appendStringInfo(&deleteQuery,
					 "DELETE FROM %s.documents_" UINT64_FORMAT
					 " WHERE document OPERATOR(%s.@@) $1::%s",
					 ApiDataSchemaName, collectionId, ApiCatalogSchemaName,
					 FullBsonTypeName);

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[0] = BYTEAOID;
	argValues[0] = PointerGetDatum(CastPgbsonToBytea(queryDoc));

	if (hasShardKeyValueFilter)
	{
		planId = QUERY_DELETE_WITH_FILTER_SHARDKEY;
		appendStringInfo(&deleteQuery, " AND shard_key_value = $2");

		argTypes[1] = INT8OID;
		argValues[1] = Int64GetDatum(shardKeyHash);
		argCount++;
	}

	if (objectIdFilter != NULL)
	{
		int argIndex;
		if (hasShardKeyValueFilter)
		{
			argIndex = 2;
			planId = QUERY_DELETE_WITH_FILTER_SHARDKEY_ID;
			appendStringInfo(&deleteQuery,
							 " AND object_id OPERATOR(%s.=) $3::%s",
							 CoreSchemaName, FullBsonTypeName);
		}
		else
		{
			argIndex = 1;
			planId = QUERY_DELETE_WITH_FILTER_ID;
			appendStringInfo(&deleteQuery,
							 " AND object_id OPERATOR(%s.=) $2::%s",
							 CoreSchemaName, FullBsonTypeName);
		}

		argTypes[argIndex] = BYTEAOID;
		argValues[argIndex] = PointerGetDatum(CastPgbsonToBytea(objectIdFilter));
		argCount++;
	}

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;
	SPIPlanPtr plan = GetSPIQueryPlan(collectionId, planId,
									  deleteQuery.data, argTypes, argCount);

	SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
	rowsDeleted = SPI_processed;

	pfree(deleteQuery.data);
	SPI_finish();

	return rowsDeleted;
}


void
CallDeleteOne(MongoCollection *collection, DeleteOneParams *deleteOneParams,
			  int64 shardKeyHash, text *transactionId, DeleteOneResult *result)
{
	/* In single node scenarios (like HelioDB where we can inline the write, call the internal)
	 * delete functions directly.
	 * Alternatively in a distributed scenario, if the shard is colocated on the current node anyway,
	 * then we don't need to go remote - we can simply call the delete internal functions directly.
	 */
	if (DefaultInlineWriteOperations || collection->shardTableName[0] != '\0')
	{
		DeleteOneInternalCore(collection->collectionId, shardKeyHash, deleteOneParams,
							  transactionId, result);
	}
	else if (IsClusterVersionAtleastThis(1, 14, 4) ||
			 IsClusterVersionEqualToAndAtLeastPatch(1, 13, 2))
	{
		/*
		 * If the cluster supports it, and we need to go remote, call the update worker
		 * function with the appropriate spec args.
		 */
		CallDeleteWorkerForDeleteOne(collection, deleteOneParams, shardKeyHash,
									 transactionId, result);
	}
	else
	{
		/* Fall back to the existing logic of calling delete_one */
		CallDeleteOneCore(collection, deleteOneParams, shardKeyHash, transactionId,
						  result);
	}
}


static void
CallDeleteWorkerForDeleteOne(MongoCollection *collection,
							 DeleteOneParams *deleteOneParams,
							 int64 shardKeyHash, text *transactionId,
							 DeleteOneResult *result)
{
	int argCount = 6;
	Datum argValues[6];

	/* whitespace means not null, n means null */
	char argNulls[6] = { ' ', ' ', ' ', ' ', 'n', 'n' };
	Oid argTypes[6] = { INT8OID, INT8OID, REGCLASSOID, BYTEAOID, BYTEAOID, TEXTOID };

	const char *updateQuery = FormatSqlQuery(
		" SELECT helio_api_internal.delete_worker($1, $2, $3, $4::helio_core.bson, $5::helio_core.bsonsequence, $6) FROM %s.documents_"
		UINT64_FORMAT " WHERE shard_key_value = %ld",
		ApiDataSchemaName, collection->collectionId, shardKeyHash);

	argValues[0] = UInt64GetDatum(collection->collectionId);

	/* p_shard_key_value */
	argValues[1] = Int64GetDatum(shardKeyHash);

	/* p_shard_oid */
	argValues[2] = ObjectIdGetDatum(InvalidOid);

	argValues[3] = PointerGetDatum(SerializeDeleteOneParams(deleteOneParams));

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
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg("delete_worker should not return null")));
	}
	pgbson *resultPgbson = (pgbson *) DatumGetPointer(resultDatum[0]);

	DeserializeWorkerDeleteResult(resultPgbson, result);
}


/*
 * CallDeleteOne calls the __API_INTERNAL_SCHEMA__.delete_one function, which could
 * get delegated based on the shard key value.
 */
static void
CallDeleteOneCore(MongoCollection *collection, DeleteOneParams *deleteOneParams,
				  int64 shardKeyHash, text *transactionId, DeleteOneResult *result)
{
	StringInfoData deleteQuery;
	int argCount = 7;
	Oid argTypes[7];
	Datum argValues[7];

	/* whitespace means not null, n means null */
	char argNulls[7] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ' };

	SPI_connect();
	initStringInfo(&deleteQuery);
	appendStringInfo(&deleteQuery,
					 " SELECT o_is_row_deleted, o_result_deleted_document"
					 " FROM %s.delete_one($1,$2,$3::%s,$4::%s,$5,$6::%s,$7)",
					 ApiInternalSchemaName, FullBsonTypeName, FullBsonTypeName,
					 FullBsonTypeName);

	/* p_collection_id */
	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collection->collectionId);

	/* p_shard_key_value */
	argTypes[1] = INT8OID;
	argValues[1] = Int64GetDatum(shardKeyHash);

	/* p_query */
	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[2] = BYTEAOID;
	argValues[2] = PointerGetDatum(CastPgbsonToBytea(deleteOneParams->query));

	/* p_sort */
	argTypes[3] = BYTEAOID;

	if (deleteOneParams->sort != NULL)
	{
		argValues[3] = PointerGetDatum(CastPgbsonToBytea(deleteOneParams->sort));
		argNulls[3] = ' ';
	}
	else
	{
		argValues[3] = 0;
		argNulls[3] = 'n';
	}

	/* p_return_document */
	argTypes[4] = BOOLOID;
	argValues[4] = BoolGetDatum(deleteOneParams->returnDeletedDocument);

	/* p_return_fields */
	argTypes[5] = BYTEAOID;

	if (deleteOneParams->returnFields != NULL)
	{
		argValues[5] = PointerGetDatum(CastPgbsonToBytea(deleteOneParams->returnFields));
		argNulls[5] = ' ';
	}
	else
	{
		argValues[5] = 0;
		argNulls[5] = 'n';
	}

	/* p_transaction_id */
	argTypes[6] = TEXTOID;

	if (transactionId != NULL)
	{
		argValues[6] = PointerGetDatum(transactionId);
		argNulls[6] = ' ';
	}
	else
	{
		argValues[6] = 0;
		argNulls[6] = 'n';
	}

	bool readOnly = false;
	long maxTupleCount = 0;

	SPI_execute_with_args(deleteQuery.data, argCount, argTypes, argValues, argNulls,
						  readOnly, maxTupleCount);

	if (SPI_processed > 0)
	{
		bool isNull = false;
		int columnNumber = 1;

		result->isRowDeleted = DatumGetBool(SPI_getbinval(SPI_tuptable->vals[0],
														  SPI_tuptable->tupdesc,
														  columnNumber, &isNull));
		columnNumber = 2;
		Datum documentDatum = SPI_getbinval(SPI_tuptable->vals[0],
											SPI_tuptable->tupdesc,
											columnNumber, &isNull);
		if (!isNull)
		{
			bool typeByValue = false;
			int typeLength = -1;
			documentDatum = SPI_datumTransfer(documentDatum, typeByValue,
											  typeLength);

			result->resultDeletedDocument =
				(pgbson *) DatumGetPointer(documentDatum);
		}
	}

	SPI_finish();
}


static void
DeleteOneInternalCore(uint64 collectionId, int64 shardKeyHash,
					  DeleteOneParams *deleteOneParams,
					  text *transactionId, DeleteOneResult *deleteOneResult)
{
	if (transactionId != NULL)
	{
		/* transaction ID specified, use retryable write path */
		RetryableWriteResult writeResult;

		/*
		 * If a retry record exists, delete it since only a single retry is allowed.
		 */
		if (DeleteRetryRecord(collectionId, shardKeyHash, transactionId, &writeResult))
		{
			/*
			 * Get rows affected from the retry record.
			 */
			deleteOneResult->isRowDeleted = writeResult.rowsAffected > 0;

			deleteOneResult->resultDeletedDocument = writeResult.resultDocument;
		}
		else
		{
			/*
			 * No retry record exists, delete the row and get the object ID.
			 */
			DeleteOneInternal(collectionId, deleteOneParams, shardKeyHash,
							  deleteOneResult);

			/*
			 * Remember that we performed a retryable write with the given
			 * transaction ID.
			 */
			InsertRetryRecord(collectionId, shardKeyHash, transactionId,
							  deleteOneResult->objectId, deleteOneResult->isRowDeleted,
							  deleteOneResult->resultDeletedDocument);
		}
	}
	else
	{
		/*
		 * No transaction ID specified, do regular delete.
		 */
		DeleteOneInternal(collectionId, deleteOneParams, shardKeyHash, deleteOneResult);
	}
}


/*
 * command_delete_one handles a single deletion on a shard.
 */
Datum
command_delete_one(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("p_collection_id cannot be NULL")));
	}
	uint64 collectionId = PG_GETARG_INT64(0);

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("p_shard_key_value cannot be NULL")));
	}
	int64 shardKeyHash = PG_GETARG_INT64(1);

	if (PG_ARGISNULL(2))
	{
		ereport(ERROR, (errmsg("p_query cannot be NULL")));
	}
	pgbson *query = PG_GETARG_PGBSON(2);

	if (PG_ARGISNULL(4))
	{
		ereport(ERROR, (errmsg("p_return_document cannot be NULL")));
	}

	/* fetch TupleDesc for return value, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc;
	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	pgbson *sort = !PG_ARGISNULL(3) ? PG_GETARG_PGBSON(3) : NULL;
	bool returnDeletedDocument = PG_GETARG_BOOL(4);
	pgbson *returnFields = !PG_ARGISNULL(5) ? PG_GETARG_PGBSON(5) : NULL;

	if (returnFields != NULL && !returnDeletedDocument)
	{
		ereport(ERROR, (errmsg("returnFields was given but old document was "
							   "not requested")));
	}

	DeleteOneResult deleteOneResult;
	memset(&deleteOneResult, 0, sizeof(deleteOneResult));

	DeleteOneParams deleteOneParams = {
		.query = query,
		.sort = sort,
		.returnDeletedDocument = returnDeletedDocument,
		.returnFields = returnFields
	};

	text *transactionId = PG_ARGISNULL(6) ? NULL : PG_GETARG_TEXT_PP(6);

	DeleteOneInternalCore(collectionId, shardKeyHash, &deleteOneParams, transactionId,
						  &deleteOneResult);

	/* prepare result tuple */
	Datum values[2];
	bool isNulls[2];

	/* o_is_row_deleted */
	values[0] = BoolGetDatum(deleteOneResult.isRowDeleted);
	isNulls[0] = false;

	/* o_result_deleted_document */
	if (deleteOneResult.resultDeletedDocument != NULL)
	{
		values[1] = PointerGetDatum(deleteOneResult.resultDeletedDocument);
		isNulls[1] = false;
	}
	else
	{
		values[1] = 0;
		isNulls[1] = true;
	}

	HeapTuple resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
	PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
}


Datum
command_delete_worker(PG_FUNCTION_ARGS)
{
	uint64 collectionId = PG_GETARG_INT64(0);
	int64 shardKeyHash = PG_GETARG_INT64(1);
	Oid shardOid = PG_GETARG_OID(2);

	pgbson *deleteInternalSpec = PG_GETARG_PGBSON_PACKED(3);

	if (shardOid == InvalidOid)
	{
		/* The planner is expected to replace this */
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg("Explicit shardOid must be set - this is a server bug"),
						errhint("Explicit shardOid must be set - this is a server bug")));
	}

	text *transactionId = PG_ARGISNULL(5) ? NULL : PG_GETARG_TEXT_PP(5);

	DeleteOneParams deleteOneParams = { 0 };
	DeserializeDeleteWorkerSpec(deleteInternalSpec, &deleteOneParams);

	DeleteOneResult result;
	memset(&result, 0, sizeof(result));

	DeleteOneInternalCore(collectionId, shardKeyHash, &deleteOneParams, transactionId,
						  &result);

	pgbson *serializedResult = SerializeDeleteOneResult(&result);
	PG_RETURN_POINTER(serializedResult);
}


/*
 * DeleteOneInternal deletes a single row with a specific shard key value filter.
 *
 * Returns 1 if a row was deleted, and 0 if no row matched the query.
 */
static void
DeleteOneInternal(uint64 collectionId, DeleteOneParams *deleteOneParams,
				  int64 shardKeyHash, DeleteOneResult *result)
{
	List *sortFieldDocuments = deleteOneParams->sort == NULL ? NIL :
							   PgbsonDecomposeFields(deleteOneParams->sort);

	pgbson *objectIdFilter = GetObjectIdFilterFromQueryDocument(deleteOneParams->query);

	int argCount = 2 + list_length(sortFieldDocuments);
	argCount += objectIdFilter != NULL ? 1 : 0;
	int varArgPosition = 2;

	Oid *argTypes = palloc(sizeof(Oid) * argCount);
	Datum *argValues = palloc(sizeof(Datum) * argCount);
	SPI_connect();

	/*
	 * We construct a query that Citus can route to a single shard, which
	 * also allows us to use LIMIT 1 and FOR UPDATE in a subquery.
	 *
	 * The LIMIT 1 ensures we only delete a single row even if there are many
	 * that match the query.
	 *
	 * The FOR UPDATE ensures that the matching row does not change or disappear
	 * concurrently. Otherwise, the DELETE might incorrectly become a noop.
	 *
	 * Note that we cannot directly place the SELECT query into the USING clause
	 * due to the reason discussed in the pgsql-bug thread:
	 * https://www.postgresql.org/message-id/3798786.1655133396%40sss.pgh.pa.us.
	 * For this reason, here we use a materialized cte to compute the ctid of the
	 * tuple that needs to be deleted.
	 */
	StringInfoData selectQuery;
	initStringInfo(&selectQuery);
	appendStringInfo(&selectQuery,
					 "WITH s AS MATERIALIZED ("
					 " SELECT ctid FROM %s.documents_" UINT64_FORMAT
					 " WHERE document OPERATOR(%s.@@) $2::%s"
					 " AND shard_key_value = $1",
					 ApiDataSchemaName, collectionId, ApiCatalogSchemaName,
					 FullBsonTypeName);

	if (objectIdFilter != NULL)
	{
		appendStringInfo(&selectQuery,
						 " AND object_id OPERATOR(%s.=) $%d::%s",
						 CoreSchemaName, (varArgPosition + 1), FullBsonTypeName);

		argTypes[varArgPosition] = BYTEAOID;
		argValues[varArgPosition] = PointerGetDatum(CastPgbsonToBytea(objectIdFilter));
		varArgPosition++;
	}

	if (list_length(sortFieldDocuments) > 0)
	{
		appendStringInfoString(&selectQuery, " ORDER BY");

		for (int i = 0; i < list_length(sortFieldDocuments); i++)
		{
			pgbson *sortDoc = list_nth(sortFieldDocuments, i);
			bool isAscending = ValidateOrderbyExpressionAndGetIsAscending(sortDoc);
			int sqlArgPosition = i + varArgPosition + 1;
			appendStringInfo(&selectQuery,
							 "%s %s.bson_orderby(document, $%d::%s) %s",
							 i > 0 ? "," : "", ApiCatalogSchemaName,
							 sqlArgPosition, FullBsonTypeName,
							 isAscending ? "ASC" : "DESC");

			argTypes[i + varArgPosition] = BYTEAOID;
			argValues[i + varArgPosition] =
				PointerGetDatum(CastPgbsonToBytea(sortDoc));
		}
	}

	appendStringInfo(&selectQuery,
					 " LIMIT 1 FOR UPDATE)");


	StringInfoData deleteQuery;
	initStringInfo(&deleteQuery);
	appendStringInfo(&deleteQuery,
					 "%s DELETE FROM %s.documents_" UINT64_FORMAT
					 " d USING s WHERE d.ctid = s.ctid AND shard_key_value = $1"
					 " RETURNING object_id %s",
					 selectQuery.data, ApiDataSchemaName, collectionId,
					 deleteOneParams->returnDeletedDocument ? ", document" : "");

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyHash);

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(deleteOneParams->query));

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;

	SPI_execute_with_args(deleteQuery.data, argCount, argTypes, argValues, argNulls,
						  readOnly, maxTupleCount);

	uint64 rowsDeleted = SPI_processed;
	Assert(rowsDeleted <= 1);

	if (rowsDeleted > 0)
	{
		result->isRowDeleted = true;

		bool isNull = false;
		int columnNumber = 1;
		Datum objectIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
											SPI_tuptable->tupdesc, columnNumber,
											&isNull);

		/* copy object ID into outer memory context */
		bool typeByValue = false;
		int typeLength = -1;
		objectIdDatum = SPI_datumTransfer(objectIdDatum, typeByValue, typeLength);

		result->objectId = (pgbson *) DatumGetPointer(objectIdDatum);
	}
	else
	{
		/* no row matched the query */
		result->isRowDeleted = false;
		result->objectId = NULL;
	}

	if (deleteOneParams->returnDeletedDocument)
	{
		if (rowsDeleted > 0)
		{
			bool isNull = false;
			int columnNumber = 2;
			Datum documentDatum = SPI_getbinval(SPI_tuptable->vals[0],
												SPI_tuptable->tupdesc, columnNumber,
												&isNull);

			pgbson *resultDeletedDocument = (pgbson *) DatumGetPointer(documentDatum);

			if (deleteOneParams->returnFields)
			{
				bool forceProjectId = false;
				bool allowInclusionExclusion = false;
				bson_iter_t projectIter;
				PgbsonInitIterator(deleteOneParams->returnFields, &projectIter);

				const BsonProjectionQueryState *projectionState =
					GetProjectionStateForBsonProject(&projectIter,
													 forceProjectId,
													 allowInclusionExclusion);
				resultDeletedDocument = ProjectDocumentWithState(resultDeletedDocument,
																 projectionState);
			}

			bool typeByValue = false;
			int typeLength = -1;
			result->resultDeletedDocument = (pgbson *) DatumGetPointer(
				SPI_datumTransfer(PointerGetDatum(resultDeletedDocument),
								  typeByValue, typeLength));
		}
		else
		{
			result->resultDeletedDocument = NULL;
		}
	}

	SPI_finish();
}


static pgbson *
SerializeDeleteOneParams(const DeleteOneParams *deleteParams)
{
	pgbson_writer commandWriter;
	pgbson_writer writer;
	PgbsonWriterInit(&commandWriter);

	PgbsonWriterStartDocument(&commandWriter, "deleteOne", 9, &writer);

	if (deleteParams->query != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "query", 5, deleteParams->query);
	}

	if (deleteParams->sort != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "sort", 4, deleteParams->sort);
	}

	PgbsonWriterAppendBool(&writer, "returnDeletedDocument", 21,
						   deleteParams->returnDeletedDocument);

	if (deleteParams->returnFields != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "returnFields", 12,
								   deleteParams->returnFields);
	}

	PgbsonWriterEndDocument(&commandWriter, &writer);
	return PgbsonWriterGetPgbson(&commandWriter);
}


static void
DeserializeDeleteWorkerSpec(pgbson *workerSpec, DeleteOneParams *deleteOneParams)
{
	pgbsonelement commandElement;
	PgbsonToSinglePgbsonElement(workerSpec, &commandElement);

	if (strcmp(commandElement.path, "deleteOne") != 0)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg("Delete worker only supports deleteOne call")));
	}

	bson_iter_t commandIter;
	BsonValueInitIterator(&commandElement.bsonValue, &commandIter);

	while (bson_iter_next(&commandIter))
	{
		const char *key = bson_iter_key(&commandIter);
		if (strcmp(key, "query") == 0)
		{
			deleteOneParams->query = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		 &commandIter));
		}
		else if (strcmp(key, "sort") == 0)
		{
			deleteOneParams->sort = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																		&commandIter));
		}
		else if (strcmp(key, "returnDeletedDocument") == 0)
		{
			deleteOneParams->returnDeletedDocument = bson_iter_bool(&commandIter);
		}
		else if (strcmp(key, "returnFields") == 0)
		{
			deleteOneParams->returnFields = PgbsonInitFromDocumentBsonValue(
				bson_iter_value(&commandIter));
		}
	}
}


static void
DeserializeWorkerDeleteResult(pgbson *resultBson, DeleteOneResult *result)
{
	bson_iter_t deleteResultIter;
	PgbsonInitIterator(resultBson, &deleteResultIter);

	while (bson_iter_next(&deleteResultIter))
	{
		const char *key = bson_iter_key(&deleteResultIter);
		if (strcmp(key, "isRowDeleted") == 0)
		{
			result->isRowDeleted = bson_iter_bool(&deleteResultIter);
		}
		else if (strcmp(key, "objectId") == 0)
		{
			result->objectId = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																   &deleteResultIter));
		}
		else if (strcmp(key, "resultDeletedDocument") == 0)
		{
			result->resultDeletedDocument = PgbsonInitFromDocumentBsonValue(
				bson_iter_value(&deleteResultIter));
		}
	}
}


static pgbson *
SerializeDeleteOneResult(DeleteOneResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendBool(&writer, "isRowDeleted", 12, result->isRowDeleted);

	if (result->objectId != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "objectId", 8, result->objectId);
	}

	if (result->resultDeletedDocument != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "resultDeletedDocument", 21,
								   result->resultDeletedDocument);
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * DeleteOneObjectId handles the case where we are deleting a single document
 * by _id from a collection that is sharded on some other key. In this case,
 * we need to look across all shards for a matching _id, then delete only that
 * one.
 *
 * Citus does not support SELECT .. FOR UPDATE, and it is very difficult to
 * support efficiently without running into frequent deadlocks. Therefore,
 * we instead do a regular SELECT. The implication is that the document might
 * be deleted or updated concurrently. In that case, we try again.
 */
static void
DeleteOneObjectId(MongoCollection *collection, DeleteOneParams *deleteOneParams,
				  bson_value_t *objectId, text *transactionId, DeleteOneResult *result)
{
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
			/* found a record, return the previous result */
			result->isRowDeleted = writeResult.rowsAffected > 0;
			return;
		}
	}

	for (int tryNumber = 0; tryNumber < maxTries; tryNumber++)
	{
		int64 shardKeyValue = 0;

		if (!FindShardKeyValueForDocumentId(collection, deleteOneParams->query, objectId,
											&shardKeyValue))
		{
			/* no document matches both the query and the object ID */
			return;
		}

		CallDeleteOne(collection, deleteOneParams, shardKeyValue, transactionId, result);

		if (result->isRowDeleted)
		{
			/* deleted the document */
			return;
		}
	}

	ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
					errmsg("failed to delete document after %d tries", maxTries)));
}


/*
 * ValidateQueryDocuments validates query document of each deletion specified
 * by given BatchDeletionSpec and returns a list of write errors.
 *
 * Stops after the first failure if the deletion mode is ordered.
 *
 * This is useful for performing query document validations when we certainly
 * know that the delete operation would become a noop due to non-existent
 * collection.
 *
 * Otherwise, i.e. if the delete operaton wouldn't become a noop, then it
 * doesn't make sense to call this function both because we already perform
 * those validations at the runtime and also because this is quite expensive,
 * meaning that this function indeed processes "query" documents as if we're
 * in the run-time to implicitly perform necessary validations.
 */
static List *
ValidateQueryDocuments(BatchDeletionSpec *batchSpec)
{
	/* declared volatile because of the longjmp in PG_CATCH */
	List *volatile writeErrorList = NIL;

	/*
	 * Weirdly, compiler complains that writeErrorIdx might be clobbered by
	 * longjmp in PG_CATCH, so declare writeErrorIdx as volatile as well.
	 */
	for (volatile int writeErrorIdx = 0;
		 writeErrorIdx < list_length(batchSpec->deletions);
		 writeErrorIdx++)
	{
		DeletionSpec *deletionSpec = list_nth(batchSpec->deletions, writeErrorIdx);

		/* declared volatile because of the longjmp in PG_CATCH */
		volatile bool isSuccess = false;

		MemoryContext oldContext = CurrentMemoryContext;
		PG_TRY();
		{
			ValidateQueryDocument(deletionSpec->deleteOneParams.query);
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
			 * Stop validating query documents after a failure if using
			 * ordered:true.
			 */
			break;
		}
	}

	return writeErrorList;
}


/*
 * BuildResponseMessage builds the response BSON for a delete command.
 */
static pgbson *
BuildResponseMessage(BatchDeletionResult *batchResult)
{
	pgbson_writer resultWriter;
	PgbsonWriterInit(&resultWriter);
	PgbsonWriterAppendInt32(&resultWriter, "n", 1, batchResult->rowsDeleted);
	PgbsonWriterAppendDouble(&resultWriter, "ok", 2, batchResult->ok);

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
