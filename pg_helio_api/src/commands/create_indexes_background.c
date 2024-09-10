/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/create_indexes_background.c
 *
 * Implementation of the create index / reindex operation in background.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>
#include <math.h>
#include <miscadmin.h>
#include <access/xact.h>
#include <catalog/namespace.h>
#include <executor/executor.h>
#include <executor/spi.h>
#include <lib/stringinfo.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <optimizer/optimizer.h>
#include <storage/lmgr.h>
#include <storage/lockdefs.h>
#include <storage/proc.h>
#include <tcop/pquery.h>
#include <tcop/utility.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/ruleutils.h>
#include <utils/snapmgr.h>
#include <utils/syscache.h>
#include "utils/backend_status.h"
#include "catalog/pg_authid.h"

#include "api_hooks.h"
#include "io/helio_bson_core.h"
#include "aggregation/bson_projection_tree.h"
#include "io/helio_bson_core.h"
#include "commands/commands_common.h"
#include "commands/create_indexes.h"
#include "commands/diagnostic_commands_common.h"
#include "commands/drop_indexes.h"
#include "commands/lock_tags.h"
#include "utils/helio_errors.h"
#include "commands/parse_error.h"
#include "geospatial/bson_geospatial_common.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "metadata/index.h"
#include "planner/mongo_query_operator.h"
#include "query/query_operator.h"
#include "utils/guc_utils.h"
#include "utils/list_utils.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "utils/index_utils.h"
#include "utils/version_utils.h"
#include "utils/query_utils.h"
#include "vector/vector_utilities.h"

#define FinishKey "finish"
#define FinishKeyLength 6


/*
 * Contains the data used when waiting for background create index to finish.
 */
typedef struct
{
	/* All index requests have been processed */
	bool finish;

	/* At-least one create index request failed */
	bool ok;

	/* error reporting; valid only when "ok" is false */
	char *errmsg;
	int errcode;
} BuildIndexesResult;


/* Skippable errors, to skip retry by index build background */
typedef struct
{
	int errCode;
	char *errMsg;
} SkippableError;

extern int MaxIndexBuildAttempts;
extern int IndexQueueEvictionIntervalInSec;
extern bool EnableIndexBuildBackground;

/* Do not retry the index build if error code belongs to following list. */
static const SkippableError SkippableErrors[] = {
	{ 16908482 /* Postgres ERRCODE_EXCLUSION_VIOLATION */, NULL },
	{ ERRCODE_HELIO_DUPLICATEKEY /* Postgres ERRCODE_HELIO_DUPLICATEKEY */, NULL },
	{ 2600, "column cannot have more than 2000 dimensions for ivfflat index" },
	{ 2600, "column cannot have more than 2000 dimensions for hnsw index" },
	{ 261 /* Postgres ERRCODE_PROGRAM_LIMIT_EXCEEDED */, "index row size " },
	{ 261 /* ERRCODE_PROGRAM_LIMIT_EXCEEDED */, "memory required is " },
	{ ERRCODE_HELIO_CANNOTCREATEINDEX, "unsupported language: " },
	{ 687882611, "Can't extract geo keys" }
};
static const int NumberOfSkippableErrors = sizeof(SkippableErrors) /
										   sizeof(SkippableError);

/* for background Index build */
PG_FUNCTION_INFO_V1(command_build_index_concurrently);
PG_FUNCTION_INFO_V1(command_create_indexes_background);
PG_FUNCTION_INFO_V1(command_create_indexes_background_internal);
PG_FUNCTION_INFO_V1(command_check_build_index_status);
PG_FUNCTION_INFO_V1(command_check_build_index_status_internal);

static pgbson * RunIndexCommandOnMetadataCoordinator(const char *query, int
													 expectedSpiOk);
static CreateIndexesResult SubmitCreateIndexesRequest(Datum dbNameDatum,
													  pgbson *createIndexesMessage,
													  bool *volatile snapshotSet);
static IndexJobOpId * GetIndexBuildJobOpId(void);
static void MarkIndexAsValid(int indexId);
static bool IsSkippableError(int targetErrorCode, char *errMsg);
static LockAcquireResult AcquireAdvisoryExclusiveSessionLockForCreateIndexBackground(
	uint64 collectionId);
static void ReleaseAdvisoryExclusiveSessionLockForCreateIndexBackground(uint64
																		collectionId);
static LOCKTAG CreateLockTagAdvisoryForCreateIndexBackground(uint64 collectionId);

static BuildIndexesResult * CheckForIndexCmdToFinish(const List *indexIdList, char
													 cmdType);
static void ParseErrorCommentFromQueue(pgbson *comment, BuildIndexesResult *result);
static int IndexIdListGetValidCount(const List *indexIdList);
static pgbson * MakeBuildIndexesMsg(BuildIndexesResult *result);

static Datum ComposeBuildIndexResponse(FunctionCallInfo fcinfo, pgbson *buildIndexBson,
									   pgbson *requestDetailBson, bool ok);
static Datum ComposeCheckIndexStatusResponse(FunctionCallInfo fcinfo, pgbson *bson, bool
											 ok, bool finish);
static void TryDropCollectionIndex(int indexId);

/*
 * command_build_index_concurrently is the implementation of the internal logic
 * where we create index concurrently.
 */
Datum
command_build_index_concurrently(PG_FUNCTION_ARGS)
{
	if (!EnableIndexBuildBackground)
	{
		PG_RETURN_VOID();
	}

	List *excludeCollectionIds = NIL;
	uint64 *collectionIds = GetCollectionIdsForIndexBuild(
		CREATE_INDEX_COMMAND_TYPE, excludeCollectionIds);

	volatile uint64_t collectionId = 0;
	IndexCmdRequest *volatile indexCmdRequest = NULL;

	while (collectionIds != NULL && collectionIds[0] != 0 && !indexCmdRequest)
	{
		/* iterate over collectionIds array */
		for (int i = 0; i < MaxNumActiveUsersIndexBuilds; i++)
		{
			if (collectionIds[i] == 0)
			{
				break;
			}
			collectionId = collectionIds[i];
			if (AcquireAdvisoryExclusiveSessionLockForCreateIndexBackground(
					collectionId) != LOCKACQUIRE_NOT_AVAIL)
			{
				indexCmdRequest = GetRequestFromIndexQueue(CREATE_INDEX_COMMAND_TYPE,
														   collectionId);
				if (!indexCmdRequest)
				{
					ReleaseAdvisoryExclusiveSessionLockForCreateIndexBackground(
						collectionId);
					continue;
				}
				break;
			}
			else
			{
				uint64 *collectionIdPtr = palloc(sizeof(uint64));
				*collectionIdPtr = collectionId;
				excludeCollectionIds = lappend(excludeCollectionIds, collectionIdPtr);

				ereport(DEBUG1,
						(errmsg("Excluded collectionId "UINT64_FORMAT, collectionId),
						 errdetail_log("Excluded collectionId "UINT64_FORMAT,
									   collectionId)));
			}
		}

		if (excludeCollectionIds == NIL)
		{
			break;
		}

		/* excluded collectionids so far */
		collectionIds = GetCollectionIdsForIndexBuild(CREATE_INDEX_COMMAND_TYPE,
													  excludeCollectionIds);
	}

	if (!indexCmdRequest)
	{
		PG_RETURN_VOID();
	}

	if (indexCmdRequest->attemptCount >= MaxIndexBuildAttempts)
	{
		/* mark the request as skipped (pruned at a later point) */
		MarkIndexRequestStatus(indexCmdRequest->indexId,
							   CREATE_INDEX_COMMAND_TYPE,
							   IndexCmdStatus_Skippable, indexCmdRequest->comment, NULL,
							   indexCmdRequest->attemptCount);
		DeleteCollectionIndexRecord(indexCmdRequest->collectionId,
									indexCmdRequest->indexId);
		PG_RETURN_VOID();
	}

	if (indexCmdRequest->status == IndexCmdStatus_Skippable)
	{
		TimestampTz nowTime = GetCurrentTimestamp();
		if (TimestampDifferenceExceeds(indexCmdRequest->updateTime, nowTime,
									   (IndexQueueEvictionIntervalInSec) * 1000))
		{
			ereport(LOG, (errmsg(
							  "Removing skippable request permanently index_id: %d and collectionId: "
							  UINT64_FORMAT,
							  indexCmdRequest->indexId, collectionId),
						  errdetail_log(
							  "Removing skippable request permanently index_id: %d and collectionId: "
							  UINT64_FORMAT,
							  indexCmdRequest->indexId, collectionId)));

			/* remove any stale entry from PG */
			TryDropCollectionIndex(indexCmdRequest->indexId);

			/* remove the request permanently */
			RemoveRequestFromIndexQueue(indexCmdRequest->indexId,
										CREATE_INDEX_COMMAND_TYPE);
			DeleteCollectionIndexRecord(indexCmdRequest->collectionId,
										indexCmdRequest->indexId);
		}
		PG_RETURN_VOID();
	}
	ereport(LOG, (errmsg(
					  "Found one request for CreateIndex with index_id: %d and collectionId: "
					  UINT64_FORMAT,
					  indexCmdRequest->indexId, collectionId),
				  errdetail_log(
					  "Found one request for CreateIndex with index_id: %d and collectionId: "
					  UINT64_FORMAT,
					  indexCmdRequest->indexId, collectionId)));

	IndexJobOpId *opId = GetIndexBuildJobOpId();

	/* Mark index inprogress. */
	pgbson *emptyComment = PgbsonInitEmpty();
	int16 attemptCount = indexCmdRequest->attemptCount + 1;

	MarkIndexRequestStatus(indexCmdRequest->indexId, CREATE_INDEX_COMMAND_TYPE,
						   IndexCmdStatus_Inprogress, emptyComment, opId, attemptCount);

	StringInfo queryStringInfo = makeStringInfo();
	appendStringInfo(queryStringInfo,
					 "SELECT shard_key FROM %s.collections WHERE collection_id = %lu",
					 ApiCatalogSchemaName, collectionId);
	char *shardKeyStr = ExtensionExecuteQueryOnLocalhostViaLibPQ(queryStringInfo->data);
	bool useSerialExecution = shardKeyStr == NULL || strlen(shardKeyStr) == 0;

	/* In case of abrupt kill of the cron job running this, we may end up with the state as "InProgress" for index.
	 * To handle such request, we are cleaning the partial index state first.
	 */
	if (indexCmdRequest->status == IndexCmdStatus_Inprogress)
	{
		ereport(LOG, (errmsg(
						  "Try dropping old index entry before CreateIndex for index_id: %d and collectionId: "
						  UINT64_FORMAT,
						  indexCmdRequest->indexId, collectionId),
					  errdetail_log(
						  "Try dropping old index entry before CreateIndex for index_id: %d and collectionId: "
						  UINT64_FORMAT,
						  indexCmdRequest->indexId, collectionId)));
		TryDropCollectionIndex(indexCmdRequest->indexId);
	}

	/* save the memory context before committing the transaction */
	MemoryContext oldMemContext = CurrentMemoryContext;

	/*
	 * We commit the transaction to prevent concurrent index creation
	 * getting blocked on that transaction due to any snapshots that
	 * we might have grabbed so far. Also, changes done by above MarkIndexRequestStatus should reflect.
	 */
	PopAllActiveSnapshots();
	CommitTransactionCommand();
	StartTransactionCommand();

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool indexCreated = false;
	volatile char *volatile errorMessage = NULL;
	volatile int errorCode = 0;
	volatile ErrorData *edata = NULL;
	PG_TRY();
	{
		char *cmd = indexCmdRequest->cmd;

		/*
		 * Tell other backends to ignore us, even if we grab any
		 * snapshots later.
		 */
		set_indexsafe_procflags();

		ereport(LOG, (errmsg(
						  "Trying to create index with serial %d for index_id: %d and collectionId: "
						  UINT64_FORMAT, useSerialExecution,
						  indexCmdRequest->indexId, collectionId),
					  errdetail_log(
						  "Trying to create index with serial %d for index_id: %d and collectionId: "
						  UINT64_FORMAT, useSerialExecution,
						  indexCmdRequest->indexId, collectionId)));
		bool concurrently = true;
		ExecuteCreatePostgresIndexCmd(cmd, concurrently, indexCmdRequest->userOid,
									  useSerialExecution);
		indexCreated = true;
	}
	PG_CATCH();
	{
		/* save error info into right context */
		MemoryContextSwitchTo(oldMemContext);
		edata = CopyErrorDataAndFlush();
		errorMessage = edata->message;
		errorCode = edata->sqlerrcode;

		ereport(DEBUG1, (errmsg("couldn't create some of the (invalid) "
								"collection indexes")));

		/*
		 * Couldn't complete creating invalid indexes, need to abort the
		 * outer transaction itself to fire abort handler.
		 */
		PopAllActiveSnapshots();
		AbortCurrentTransaction();
		StartTransactionCommand();
	}
	PG_END_TRY();

	if (!indexCreated && edata != NULL)
	{
		/* Try to get a friendlier error message */
		MemoryContext switchContext = MemoryContextSwitchTo(oldMemContext);
		int errorCodeInternal = 0;
		char *errorMessageInternal = NULL;
		if (TryGetErrorMessageAndCode((ErrorData *) edata, &errorCodeInternal,
									  &errorMessageInternal))
		{
			errorCode = errorCodeInternal;
			errorMessage = errorMessageInternal;
		}
		MemoryContextSwitchTo(switchContext);
	}

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool markedIndexAsValid = false;
	if (indexCreated)
	{
		/*
		 * Now try marking entries inserted for collection index as valid.
		 *
		 * Use a subtransaction	so that we can automatically rollback the changes
		 * made by MarkIndexAsValid and RemoveRequestFromIndexQueue if something goes wrong when doing that.
		 */
		MemoryContext oldContext = CurrentMemoryContext;
		ResourceOwner oldOwner = CurrentResourceOwner;
		BeginInternalSubTransaction(NULL);

		PG_TRY();
		{
			ereport(LOG, (errmsg(
							  "Trying to mark invalid index as valid and remove index build request from queue for index_id: %d and collectionId: "
							  UINT64_FORMAT,
							  indexCmdRequest->indexId, collectionId),
						  errdetail_log(
							  "Trying to mark invalid index as valid and remove index build request from queue for index_id: %d and collectionId: "
							  UINT64_FORMAT,
							  indexCmdRequest->indexId, collectionId)));
			MarkIndexAsValid(indexCmdRequest->indexId);
			RemoveRequestFromIndexQueue(indexCmdRequest->indexId,
										CREATE_INDEX_COMMAND_TYPE);

			/*
			 * All done, commit the subtransaction and return to outer
			 * transaction context.
			 */
			ReleaseCurrentSubTransaction();
			MemoryContextSwitchTo(oldContext);
			CurrentResourceOwner = oldOwner;

			markedIndexAsValid = true;
		}
		PG_CATCH();
		{
			/*
			 * Abort the subtransaction to rollback any changes that
			 * MarkIndexAsValid might have done.
			 */
			ereport(WARNING, (errmsg(
								  "Failure happened during marking the index metadata valid for index_id: %d and collectionId: "
								  UINT64_FORMAT,
								  indexCmdRequest->indexId, collectionId),
							  errdetail_log(
								  "Failure happened during marking the index metadata valid for index_id: %d and collectionId: "
								  UINT64_FORMAT,
								  indexCmdRequest->indexId, collectionId)));
			RollbackAndReleaseCurrentSubTransaction();

			/* save error info into right context */
			MemoryContextSwitchTo(oldContext);
			CurrentResourceOwner = oldOwner;
			ErrorData *edata = CopyErrorDataAndFlush();
			errorCode = edata->sqlerrcode;
			errorMessage = pstrdup("Failure during marking Index valid");
		}
		PG_END_TRY();
	}

	if (!markedIndexAsValid)
	{
		/* Drop the index here */

		/*
		 * TODO : In case, when partial index metadata is not deleted, we might need the cron job to clean such stale indexes.
		 * Check TryDropCollectionIndexes API doc.
		 */
		ereport(LOG, (errmsg(
						  "Something failed during create-index, drop partial state of index for index_id: %d and collectionId: "
						  UINT64_FORMAT,
						  indexCmdRequest->indexId, collectionId),
					  errdetail_log(
						  "Something failed during create-index, drop partial state of index for index_id: %d and collectionId: "
						  UINT64_FORMAT,
						  indexCmdRequest->indexId, collectionId)));

		TryDropCollectionIndex(indexCmdRequest->indexId);

		/* MarkIndexRequestStatus to failure for the indexId and cmdType = CREATE_INDEX_COMMAND_TYPE */
		ereport(LOG, (errmsg(
						  "Marking Index status Failed for index with index_id: %d and collectionId: "
						  UINT64_FORMAT,
						  indexCmdRequest->indexId, collectionId),
					  errdetail_log(
						  "Marking Index status Failed for index with index_id: %d and collectionId: "
						  UINT64_FORMAT,
						  indexCmdRequest->indexId, collectionId)));

		if (attemptCount >= MaxIndexBuildAttempts)
		{
			ereport(LOG, (errmsg(
							  "Removing request permanently index_id: %d and collectionId: "
							  UINT64_FORMAT,
							  indexCmdRequest->indexId, collectionId),
						  errdetail_log(
							  "Removing request permanently index_id: %d and collectionId: "
							  UINT64_FORMAT,
							  indexCmdRequest->indexId, collectionId)));

			/* mark the request skippable (removed after the TTL window) */
			MarkIndexRequestStatus(indexCmdRequest->indexId,
								   CREATE_INDEX_COMMAND_TYPE,
								   IndexCmdStatus_Skippable, indexCmdRequest->comment,
								   NULL,
								   attemptCount);
			DeleteCollectionIndexRecord(indexCmdRequest->collectionId,
										indexCmdRequest->indexId);
		}
		else
		{
			/* Create comment bson */
			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendUtf8(&writer, ErrMsgKey, ErrMsgLength,
								   (char *) errorMessage);
			PgbsonWriterAppendInt32(&writer, ErrCodeKey, ErrCodeLength, errorCode);
			pgbson *newComment = PgbsonWriterGetPgbson(&writer);

			if (IsSkippableError(errorCode, (char *) errorMessage))
			{
				ereport(LOG, (errmsg(
								  "Saving Skippable comment index_id: %d and collectionId: "
								  UINT64_FORMAT,
								  indexCmdRequest->indexId, collectionId),
							  errdetail_log(
								  "Saving Skippable comment index_id: %d and collectionId: "
								  UINT64_FORMAT,
								  indexCmdRequest->indexId, collectionId)));
				MarkIndexRequestStatus(indexCmdRequest->indexId,
									   CREATE_INDEX_COMMAND_TYPE,
									   IndexCmdStatus_Skippable, newComment, NULL,
									   attemptCount);

				/* The request will be removed during the next cron job when the time elapsed since the request's last update exceeds the specified IndexQueueEvictionIntervalInSec
				 * we have to remove this request from index metadata to avoid any conflict if user app immediately tries to submit the same request
				 */
				DeleteCollectionIndexRecord(indexCmdRequest->collectionId,
											indexCmdRequest->indexId);
			}
			else
			{
				ereport(LOG, (errmsg(
								  "Saving comment index_id: %d and collectionId: "
								  UINT64_FORMAT,
								  indexCmdRequest->indexId, collectionId),
							  errdetail_log(
								  "Saving comment index_id: %d and collectionId: "
								  UINT64_FORMAT,
								  indexCmdRequest->indexId, collectionId)));
				MarkIndexRequestStatus(indexCmdRequest->indexId,
									   CREATE_INDEX_COMMAND_TYPE,
									   IndexCmdStatus_Failed, newComment, NULL,
									   attemptCount);
			}
		}
		PopAllActiveSnapshots();
		CommitTransactionCommand();
		StartTransactionCommand();
	}

	ReleaseAdvisoryExclusiveSessionLockForCreateIndexBackground(collectionId);
	PG_RETURN_VOID();
}


/*
 * Queues the Index creation request(command_create_indexes_background_internal) and
 * waits(check_build_index_status) for it to finish.
 */
Datum
command_create_indexes_background(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("dbName cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("arg cannot be NULL")));
	}

	text *databaseDatum = PG_GETARG_TEXT_P(0);
	pgbson *indexSpec = PG_GETARG_PGBSON(1);

	StringInfo submitIndexBuildRequestQuery = makeStringInfo();
	appendStringInfo(submitIndexBuildRequestQuery,
					 "SELECT %s.create_indexes_background_internal(%s,%s)",
					 ApiInternalSchemaName,
					 quote_literal_cstr(text_to_cstring(databaseDatum)),
					 quote_literal_cstr(PgbsonToHexadecimalString(indexSpec)));
	pgbson *submitIndexBuildResponse = RunIndexCommandOnMetadataCoordinator(
		submitIndexBuildRequestQuery->data, SPI_OK_SELECT);

	/* Seperate the CreateIndex response and internal keys i.e. "indexRequest" */
	pgbson_writer buildIndexResponseWriter;
	PgbsonWriterInit(&buildIndexResponseWriter);

	pgbson_writer requestDetailWriter;
	PgbsonWriterInit(&requestDetailWriter);

	bson_iter_t documentIter;
	PgbsonInitIterator(submitIndexBuildResponse, &documentIter);
	bool ok = false;

	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, "ok") == 0)
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			ok = value->value.v_bool;
			PgbsonWriterAppendValue(&buildIndexResponseWriter, key, strlen(key), value);
		}
		else if (strcmp(key, IndexRequestKey) == 0)
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			PgbsonWriterAppendValue(&requestDetailWriter, key, strlen(key), value);
		}
		else
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			PgbsonWriterAppendValue(&buildIndexResponseWriter, key, strlen(key), value);
		}
	}

	pgbson *requestDetailBson = PgbsonWriterGetPgbson(&requestDetailWriter);
	pgbson *buildIndexResponseBson = PgbsonWriterGetPgbson(&buildIndexResponseWriter);

	PG_RETURN_DATUM(ComposeBuildIndexResponse(fcinfo,
											  buildIndexResponseBson, requestDetailBson,
											  ok));
}


/*
 * Queues the Index creation request and returns.
 * If there are any unique index builds, performs them
 * inline. This needs to run on the metadata coordinator.
 */
Datum
command_create_indexes_background_internal(PG_FUNCTION_ARGS)
{
	Datum dbNameDatum = PG_GETARG_DATUM(0);

	/*
	 * Deduplicate elements of given bson object recursively so that we don't
	 * have multiple definitions for any bson field at any level.
	 *
	 * That way, we will not throw an error;
	 * - if we encounter with definition of a field more than once, or
	 * - if there is a syntax error in the prior definitions. e.g.:
	 *   {"createIndexes": 1, "createIndexes": "my_collection_name"}
	 * as Mongo does.
	 */
	pgbson *arg = PgbsonDeduplicateFields(PG_GETARG_PGBSON(1));
	bool *snapshotSet = (bool *) palloc0(sizeof(bool));
	CreateIndexesResult *result = palloc0(sizeof(CreateIndexesResult));
	MemoryContext savedMemoryContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	/* use a subtransaction to correctly handle failures */
	BeginInternalSubTransaction(NULL);
	PG_TRY();
	{
		*result = SubmitCreateIndexesRequest(dbNameDatum, arg, snapshotSet);

		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(savedMemoryContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_CATCH();
	{
		/* run_command_on_coordinator does not return error code in case of failure inside the called function
		 * i.e. ApiInternalSchema.create_indexes_background_internal.
		 */
		MemoryContextSwitchTo(savedMemoryContext);
		ErrorData *edata = CopyErrorDataAndFlush();
		result->errcode = edata->sqlerrcode;
		result->errmsg = edata->message;

		/* Snapshot pushed inside SubmitCreateIndexesRequest may not be popped. */
		if (*snapshotSet)
		{
			PopActiveSnapshot();
		}

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();

		/* Rollback changes MemoryContext */
		MemoryContextSwitchTo(savedMemoryContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_END_TRY();

	PG_RETURN_POINTER(MakeCreateIndexesMsg(result));
}


/*
 * command_check_build_index_status calls command_check_build_index_status_internal via RunCommandOnMetadataCoordinator
 */
Datum
command_check_build_index_status(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON(0);
	StringInfo checkIndexBuildQuery = makeStringInfo();
	appendStringInfo(checkIndexBuildQuery,
					 "SELECT %s.check_build_index_status_internal(%s)",
					 ApiInternalSchemaName,
					 quote_literal_cstr(PgbsonToHexadecimalString(bson)));
	pgbson *statusBson = RunIndexCommandOnMetadataCoordinator(checkIndexBuildQuery->data,
															  SPI_OK_SELECT);

	/* Remove FinishKey from the bson and construct statusResponse */
	pgbson_writer statusResponseWriter;
	PgbsonWriterInit(&statusResponseWriter);

	bson_iter_t documentIter;
	PgbsonInitIterator(statusBson, &documentIter);
	bool ok = false;
	bool finish = false;

	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, "ok") == 0)
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			PgbsonWriterAppendValue(&statusResponseWriter, key, strlen(key), value);
			ok = value->value.v_bool;
		}
		else if (strcmp(key, FinishKey) == 0)
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			finish = value->value.v_bool;
		}
		else
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			PgbsonWriterAppendValue(&statusResponseWriter, key, strlen(key), value);
		}
	}

	pgbson *statusResponse = PgbsonWriterGetPgbson(&statusResponseWriter);
	PG_RETURN_DATUM(ComposeCheckIndexStatusResponse(fcinfo, statusResponse, ok, finish));
}


/*
 * command_check_build_index_status_internal is the implementation of the internal logic
 * where we check the status of build index requests running in background.
 */
Datum
command_check_build_index_status_internal(PG_FUNCTION_ARGS)
{
	const pgbson *document = PG_GETARG_PGBSON(0);
	char cmdType = 0;
	List *indexIds = NIL;

	bson_iter_t documentIter;
	PgbsonInitIterator(document, &documentIter);
	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, IndexRequestKey) == 0)
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&documentIter))
			{
				ereport(ERROR, (errmsg(
									"indexRequest expecting entry to contain document")));
			}

			const bson_value_t *indexRequestVal = bson_iter_value(&documentIter);
			bson_iter_t innerDocIter;
			BsonValueInitIterator(indexRequestVal, &innerDocIter);
			while (bson_iter_next(&innerDocIter))
			{
				const char *innerKey = bson_iter_key(&innerDocIter);
				if (strcmp(innerKey, CmdTypeKey) == 0)
				{
					const bson_value_t *value = bson_iter_value(&innerDocIter);
					cmdType = value->value.v_utf8.str[0];
				}
				else if (strcmp(innerKey, IdsKey) == 0)
				{
					bson_iter_t idsArrayIter;
					if (!BSON_ITER_HOLDS_ARRAY(&innerDocIter) ||
						!bson_iter_recurse(&innerDocIter, &idsArrayIter))
					{
						ereport(ERROR, (errmsg("Could not iterate through ids array.")));
					}
					while (bson_iter_next(&idsArrayIter))
					{
						const bson_value_t *idValue = bson_iter_value(&idsArrayIter);
						int32_t indexId = BsonValueAsInt32(idValue);
						indexIds = lappend_int(indexIds, indexId);
					}
				}
			}
		}
	}

	if (cmdType == 0)
	{
		ereport(ERROR, (errmsg("Command must provide valid cmdType."),
						errdetail_log("Command must provide valid cmdType.")));
	}
	BuildIndexesResult *result;
	if (indexIds != NIL)
	{
		result = CheckForIndexCmdToFinish(indexIds, cmdType);
	}
	else
	{
		result = palloc0(sizeof(BuildIndexesResult));
		result->finish = true;
		result->ok = true;
	}
	PG_RETURN_POINTER(MakeBuildIndexesMsg(result));
}


/*
 * SubmitCreateIndexesRequest is the function that submits the create index request to local table
 * and submits indexes into metadata as invalid.
 */
static CreateIndexesResult
SubmitCreateIndexesRequest(Datum dbNameDatum,
						   pgbson *createIndexesMessage, bool *volatile snapshotSet)
{
	CreateIndexesArg createIndexesArg = ParseCreateIndexesArg(dbNameDatum,
															  createIndexesMessage);

	char *collectionName = createIndexesArg.collectionName;
	Datum collectionNameDatum = CStringGetTextDatum(collectionName);

	CreateIndexesResult result = {
		.ok = true /* we will throw an error otherwise */
	};

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
									  AccessShareLock);
	if (collection)
	{
		result.createdCollectionAutomatically = false;
	}
	else
	{
		/* collection does not exist, create it (or race for creating it) */
		result.createdCollectionAutomatically =
			CreateCollection(dbNameDatum, collectionNameDatum);

		collection = GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
												   AccessShareLock);
	}

	uint64 collectionId = collection->collectionId;

	/*
	 * Please note that the advisory lock will be released after first transaction commit.
	 * This is mainly useful to prevent concurrent index metadata insertions
	 * for given collection while checking for name / option conflicting indexes
	 **/
	AcquireAdvisoryExclusiveLockForCreateIndexes(collectionId);

	/*
	 * For both CheckForConflictsAndPruneExistingIndexes() and
	 * CollectionIdGetIndexCount(), we need to take built-in _id index
	 * (that we might have just created together with the collection itself)
	 * into the account too, so need to use a snapshot to which changes made
	 * by CreateCollection() are visible.
	 */
	PushActiveSnapshot(GetTransactionSnapshot());
	*snapshotSet = true;

	/*
	 * Prune away the indexes that already exist and throw an error if any
	 * of them would cause a name / option conflict.
	 *
	 * And before doing so, save the number of indexes that user actually
	 * wants to create to later determine whether we decided to not create
	 * some of them due to an identical index.
	 */

	int nindexesRequested = list_length(createIndexesArg.indexDefList);

	List *indexIdList = NIL;
	createIndexesArg.indexDefList =
		CheckForConflictsAndPruneExistingIndexes(collectionId,
												 createIndexesArg.indexDefList,
												 &indexIdList);

	result.numIndexesBefore = CollectionIdGetIndexCount(collectionId);

	if (result.numIndexesBefore + list_length(createIndexesArg.indexDefList) >
		MaxIndexesPerCollection)
	{
		int reportIndexDefIdx = MaxIndexesPerCollection - result.numIndexesBefore;
		const IndexDef *reportIndexDef = list_nth(createIndexesArg.indexDefList,
												  reportIndexDefIdx);
		ereport(ERROR, (errcode(ERRCODE_HELIO_CANNOTCREATEINDEX),
						errmsg("add index fails, too many indexes for %s.%s key:%s",
							   collection->name.databaseName,
							   collection->name.collectionName,
							   PgbsonToJsonForLogging(reportIndexDef->keyDocument))));
	}

	/* pop the snapshot that we've just pushed above */
	PopActiveSnapshot();
	*snapshotSet = false;

	/*
	 * Record indexes into metadata as invalid.
	 */
	ListCell *indexDefCell = NULL;

	foreach(indexDefCell, createIndexesArg.indexDefList)
	{
		IndexDef *indexDef = (IndexDef *) lfirst(indexDefCell);
		const IndexSpec indexSpec = MakeIndexSpecForIndexDef(indexDef);
		bool indexIsValid = false;
		int indexId = RecordCollectionIndex(collectionId, &indexSpec, indexIsValid);
		bool createIndexesConcurrently = true;
		bool isTempCollection = false;
		char *cmd = CreatePostgresIndexCreationCmd(collectionId, indexDef, indexId,
												   createIndexesConcurrently,
												   isTempCollection);

		Oid userOid = GetAuthenticatedUserId();
		AddRequestInIndexQueue(cmd, indexId, collectionId, CREATE_INDEX_COMMAND_TYPE,
							   userOid);
		indexIdList = lappend_int(indexIdList, indexId);
	}

	ereport(DEBUG1, (errmsg(
						 "Submitted all requests for collection indexes creation successfully")));
	result.ok = true;
	int indexCount = list_length(createIndexesArg.indexDefList);

	/*
	 * Set "note" field of the response message based on whether we
	 * rejected creating any indexes.
	 */
	if (indexCount == 0)
	{
		/*
		 * We don't allow "indexes" array to be empty, so this means that
		 * all the indexes already exist ..
		 */
		result.note = "all indexes already exist";
	}
	else if (indexCount < nindexesRequested)
	{
		/* then not all but some indexes already exist */
		result.note = "index already exists";
	}

	result.numIndexesAfter = result.numIndexesBefore + indexCount;
	if (indexIdList != NIL)
	{
		result.request = palloc0(sizeof(SubmittedIndexRequests));
		result.request->cmdType = CREATE_INDEX_COMMAND_TYPE;
		result.request->indexIds = indexIdList;
	}

	return result;
}


/*
 * GetIndexBuildJobOpId gets the IndexJobOpId for current job
 */
static IndexJobOpId *
GetIndexBuildJobOpId()
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT citus_backend_gpid(), query_start"
					 " FROM pg_stat_activity where pid = pg_backend_pid();");

	bool readOnly = false;
	int numValues = 2;
	bool isNull[2];
	Datum results[2];
	ExtensionExecuteMultiValueQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT, results,
										  isNull, numValues);
	if (isNull[0] || isNull[1])
	{
		ereport(ERROR, (errmsg(
							"Global Pid or start time should not be null for current process")));
	}
	IndexJobOpId *opId = palloc(sizeof(IndexJobOpId));
	opId->global_pid = DatumGetInt64(results[0]);
	opId->start_time = DatumGetTimestampTz(results[1]);

	return opId;
}


/*
 * MarkIndexAsValid marks the index metadata entry associated with given index as valid (if inserted already).
 */
static void
MarkIndexAsValid(int indexId)
{
	const char *cmdStr = FormatSqlQuery(
		"UPDATE %s.collection_indexes SET index_is_valid = true"
		" WHERE index_id = $1;", ApiCatalogSchemaName);

	int argCount = 1;
	Oid argTypes[1];
	Datum argValues[1];
	char argNulls[1] = { ' ' };

	argTypes[0] = INT4OID;
	argValues[0] = Int32GetDatum(indexId);

	bool isNull = true;
	RunQueryWithCommutativeWrites(cmdStr, argCount, argTypes,
								  argValues, argNulls,
								  SPI_OK_UPDATE, &isNull);
}


/* Returns true if the targetErrorCode and errMsg are from SkippableErrors */
static bool
IsSkippableError(int targetErrorCode, char *errMsg)
{
	if (targetErrorCode != -1)
	{
		if (EreportCodeIsMongoError(targetErrorCode) &&
			targetErrorCode != MongoInternalError &&
			targetErrorCode != ERRCODE_HELIO_INTERNALERROR)
		{
			/* Mongo errors that are not internal errors are skippable */
			return true;
		}

		for (int i = 0; i < NumberOfSkippableErrors; i++)
		{
			if (SkippableErrors[i].errCode == targetErrorCode)
			{
				if (SkippableErrors[i].errMsg == NULL || errMsg == NULL)
				{
					return true;
				}
				else if (strncmp(errMsg, SkippableErrors[i].errMsg, strlen(
									 SkippableErrors[i].errMsg)) == 0)
				{
					return true;
				}
			}
		}
	}
	return false;
}


/*
 * AcquireAdvisoryExclusiveSessionLockForCreateIndexBackground acquires an advisory
 * ShareUpdateExclusiveLock for given collection. Note that the only reason for
 * acquiring a ShareUpdateExclusiveLock here is that it's the lowest lock
 * mode that cannot be acquired by more than one backend.
 *
 * This is mainly used to make sure no concurrent createIndex(concurrently : true) requests
 * execute for same collection. This is because concurrently createIndex requests on same collection causes deadlock.
 * We are taking session level advisory lock, so that it survives the entire sesssion when called from command_build_index_concurrently
 * This lock is held until explicitly released or the session ends.
 *
 * Note that the lock type acquired by this function --advisory-- is different
 * than the usual collection lock that we acquire on the data table, so this
 * cannot anyhow conflict with a collection lock. It is also different than
 * advisory lock that we acquire in AcquireAdvisoryExclusiveLockForCreateIndexes because of field4 tag.
 */
static LockAcquireResult
AcquireAdvisoryExclusiveSessionLockForCreateIndexBackground(uint64 collectionId)
{
	bool dontWait = true;
	bool sessionLock = true;
	LOCKTAG locktag = CreateLockTagAdvisoryForCreateIndexBackground(
		collectionId);
	return LockAcquire(&locktag, ShareUpdateExclusiveLock, sessionLock, dontWait);
}


/**
 * ReleaseAdvisoryExclusiveSessionLockForCreateIndexBackground releases the lock acquired by AcquireAdvisoryExclusiveSessionLockForCreateIndexBackground
 */
static void
ReleaseAdvisoryExclusiveSessionLockForCreateIndexBackground(uint64 collectionId)
{
	bool sessionLock = true;
	LOCKTAG locktag = CreateLockTagAdvisoryForCreateIndexBackground(
		collectionId);
	(void) LockRelease(&locktag, ShareUpdateExclusiveLock, sessionLock);
}


/**
 * CreateLockTagAdvisoryForCreateIndexBackground creates the lock tag for create index in background.
 */
static LOCKTAG
CreateLockTagAdvisoryForCreateIndexBackground(uint64 collectionId)
{
	LOCKTAG locktag;
	SET_LOCKTAG_ADVISORY(locktag, MyDatabaseId,
						 (collectionId & 0xFFFFFFFF),
						 (collectionId & (((uint64) 0xFFFFFFFF) << 32)) >> 32,
						 LT_FIELD4_EXCL_CREATE_INDEX_BACKGROUND);
	return locktag;
}


/*
 * CheckForIndexCmdToFinish checks for the index command to finish for all indexIds present in indexIdList.
 * The count(indexId) will match the length(indexIdList) only if for each indexId in indexIdList, any one of following conditions are met:
 * 1. Request is already picked second time (attempt count >= 2) by cron-job before CheckForIndexCmdToFinish function could check its failure status.
 * 2. Request is not there in helio_api_catalog.helio_index_queue, which means it is completed successfully.
 * 3. Request is there in helio_api_catalog.helio_index_queue and index_cmd_status >= 3 (Failed, Skippable).
 * 4. Request is there in helio_api_catalog.helio_index_queue and index_cmd_status = 2 (InProgress)
 *    and corresponding global_pid (opid) is not present in pg_stat_activity. This means that process was abruptly failed.
 */
static BuildIndexesResult *
CheckForIndexCmdToFinish(const List *indexIdList, char cmdType)
{
	Assert(cmdType == CREATE_INDEX_COMMAND_TYPE || cmdType == REINDEX_COMMAND_TYPE);

	BuildIndexesResult *result = palloc0(sizeof(BuildIndexesResult));
	result->finish = false;
	result->ok = true;


	/*  WITH query AS
	 *      (SELECT index_cmd_status::int4, comment, attempt
	 *      FROM helio_api_catalog.helio_index_queue piq
	 *      WHERE cmd_type = 'C' AND index_id =ANY(ARRAY[32001, 32002, 32003, 32004, 32005, 32006])
	 *  )
	 *  SELECT COALESCE(mongo_catalog.bson_array_agg(mongo_catalog.row_get_bson(query), ''), '{ "": [] }'::mongo_catalog.bson) FROM query
	 */
	const char *query =
		FormatSqlQuery("WITH query AS (SELECT index_cmd_status::int4, comment, attempt"
					   " FROM %s piq "
					   " WHERE cmd_type = $1 AND index_id =ANY($2)) "
					   " SELECT COALESCE(%s.bson_array_agg(%s.row_get_bson(query), ''), '{ \"\": [] }'::%s) FROM query",
					   GetIndexQueueName(), ApiCatalogSchemaName,
					   ApiCatalogSchemaName, FullBsonTypeName);

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];
	char argNulls[2] = { ' ', ' ' };

	argTypes[0] = CHAROID;
	argValues[0] = CharGetDatum(cmdType);
	argTypes[1] = INT4ARRAYOID;
	argValues[1] = PointerGetDatum(IntListGetPgIntArray(indexIdList));

	bool readOnly = true;
	bool isNull = false;

	Datum resultDatum = ExtensionExecuteQueryWithArgsViaSPI(query, argCount, argTypes,
															argValues, argNulls,
															readOnly,
															SPI_OK_SELECT, &isNull);
	if (isNull)
	{
		return result;
	}

	pgbson *resultBson = DatumGetPgBson(resultDatum);
	pgbsonelement singleElement;
	PgbsonToSinglePgbsonElement(resultBson, &singleElement);

	bson_iter_t arrayIterator;
	BsonValueInitIterator(&singleElement.bsonValue, &arrayIterator);

	/* index queue documents */
	bson_value_t failedIndexComment = { 0 };
	bool isAnyIndexFailed = false;
	int numIndexBuilds = 0;
	while (bson_iter_next(&arrayIterator))
	{
		bson_iter_t docIterator;
		BsonValueInitIterator(bson_iter_value(&arrayIterator), &docIterator);

		int cmdStatus = IndexCmdStatus_Unknown;
		bson_value_t comment = { 0 };
		int attemptCount = 0;
		while (bson_iter_next(&docIterator))
		{
			const char *key = bson_iter_key(&docIterator);
			if (strcmp(key, "index_cmd_status") == 0)
			{
				cmdStatus = BsonValueAsInt32(bson_iter_value(&docIterator));
			}
			else if (strcmp(key, "comment") == 0)
			{
				comment = *bson_iter_value(&docIterator);
			}
			else if (strcmp(key, "attempt") == 0)
			{
				attemptCount = BsonValueAsInt32(bson_iter_value(&docIterator));
			}
		}

		if (cmdStatus >= IndexCmdStatus_Failed)
		{
			failedIndexComment = comment;
			isAnyIndexFailed = true;
		}

		if (attemptCount >= 2)
		{
			if (comment.value_type != BSON_TYPE_EOD)
			{
				failedIndexComment = comment;
			}
			else
			{
				result->errmsg = "Index creation attempt failed";
				result->errcode = MongoInternalError;
			}
			isAnyIndexFailed = true;
		}

		numIndexBuilds++;
	}

	if (failedIndexComment.value_type != BSON_TYPE_EOD || isAnyIndexFailed)
	{
		result->ok = false;
		result->finish = true;
		pgbson *comment = NULL;
		if (failedIndexComment.value_type != BSON_TYPE_EOD)
		{
			comment = PgbsonInitFromDocumentBsonValue(&failedIndexComment);
			ParseErrorCommentFromQueue(comment, result);
		}

		/* Make sure that we always set errmsg and errcode because ok = false*/
		if (result->errmsg == NULL)
		{
			/* index failed but empty comment in queue. */
			result->errmsg = "Index Creation failed";
			result->errcode = MongoInternalError;
		}

		return result;
	}

	if (numIndexBuilds == 0)
	{
		/* There's none in the queue, get index build status from the main index queue */
		int validIndexCount = IndexIdListGetValidCount(indexIdList);
		if (validIndexCount == list_length(indexIdList) || validIndexCount == 0)
		{
			result->ok = true;
			result->finish = true;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Build Index failed because there are not enough valid indexes in metadata table. Requested %d, found %d",
								list_length(indexIdList), validIndexCount),
							errdetail_log(
								"Build Index failed. Index builds was empty, but main index table didn't have the index. Requested %d, found %d",
								list_length(indexIdList), validIndexCount)));
		}
	}

	/* Some or all are in progress and no errors. */
	return result;
}


/* Parses the error comment bson and adds it in the BuildIndexesResult */
void
ParseErrorCommentFromQueue(pgbson *comment, BuildIndexesResult *result)
{
	bson_iter_t iter;
	PgbsonInitIterator(comment, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		if (strcmp(key, ErrMsgKey) == 0)
		{
			const char *string = bson_iter_utf8(&iter, NULL);
			result->errmsg = pstrdup(string);
		}
		else if (strcmp(key, ErrCodeKey) == 0)
		{
			result->errcode = BsonValueAsInt32(bson_iter_value(&iter));
		}
		else
		{
			ereport(ERROR, (errmsg("unknown field received from comment %s", key),
							errdetail_log("unknown field received from comment %s",
										  key)));
		}
	}
}


/*
 * Given a list of index_id's gets the count of indexes
 * in the main index table that are valid.
 */
static int
IndexIdListGetValidCount(const List *indexIdList)
{
	const char *cmdStr =
		FormatSqlQuery("SELECT COUNT(*)::int4 FROM %s.collection_indexes "
					   "WHERE index_id =ANY($1) AND index_is_valid = true",
					   ApiCatalogSchemaName);

	bool readOnly = true;
	int argCount = 1;
	Oid argTypes[1];
	Datum argValues[1];
	char argNulls[1] = { ' ' };

	argTypes[0] = INT4ARRAYOID;
	argValues[0] = PointerGetDatum(IntListGetPgIntArray(indexIdList));

	bool isNull;
	Datum resultDatum = ExtensionExecuteQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
															argValues, argNulls,
															readOnly,
															SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		return 0;
	}

	return DatumGetInt32(resultDatum);
}


static pgbson *
MakeBuildIndexesMsg(BuildIndexesResult *result)
{
	/* { "raw" :
	 *      { "defaultShard" :
	 *          {
	 *              "ok" : { "$numberInt" : "0" },
	 *              "errmsg" : "error",
	 *              "code" : { "$numberInt" : "1" }
	 *          }
	 *      },
	 *      "finish" : { "$numberInt" : "1" },
	 *      "ok" : { "$numberInt" : "0" }
	 *  }
	 */
	pgbson_writer outerWriter;
	PgbsonWriterInit(&outerWriter);

	if (!result->ok)
	{
		pgbson_writer rawShardResultWriter;
		PgbsonWriterStartDocument(&outerWriter, "raw", strlen("raw"),
								  &rawShardResultWriter);

		pgbson_writer writer;
		PgbsonWriterStartDocument(&rawShardResultWriter, "defaultShard", strlen(
									  "defaultShard"), &writer);

		PgbsonWriterAppendInt32(&writer, "ok", strlen("ok"), result->ok);
		if (result->errcode == ERRCODE_T_R_DEADLOCK_DETECTED)
		{
			result->errmsg = "deadlock detected. createIndexes() command "
							 "might cause deadlock when there is a "
							 "concurrent operation that require exclusive "
							 "access on the same collection";
		}
		else if (result->errcode == ERRCODE_UNDEFINED_TABLE)
		{
			result->errcode = MongoIndexBuildAborted;
			result->errmsg = COLLIDX_CONCURRENTLY_DROPPED_RECREATED_ERRMSG;
		}

		PgbsonWriterAppendUtf8(&writer, "errmsg", strlen("errmsg"), result->errmsg);
		PgbsonWriterAppendInt32(&writer, "code", strlen("code"), result->errcode);

		PgbsonWriterEndDocument(&rawShardResultWriter, &writer);
		PgbsonWriterEndDocument(&outerWriter, &rawShardResultWriter);
	}
	PgbsonWriterAppendInt32(&outerWriter, FinishKey, FinishKeyLength, result->finish);
	PgbsonWriterAppendInt32(&outerWriter, "ok", strlen("ok"), result->ok);
	return PgbsonWriterGetPgbson(&outerWriter);
}


/*
 * Builds response for command_create_indexes_background using buildIndexBson, requestDetailBson and ok.
 */
static Datum
ComposeBuildIndexResponse(FunctionCallInfo fcinfo, pgbson *buildIndexBson,
						  pgbson *requestDetailBson, bool ok)
{
	Datum values[3] = { 0 };
	bool isNulls[3] = { false, false, false };
	values[0] = PointerGetDatum(buildIndexBson);
	values[1] = BoolGetDatum(ok);
	values[2] = PointerGetDatum(requestDetailBson);

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	return HeapTupleGetDatum(heap_form_tuple(resultTupDesc, values, isNulls));
}


/*
 * Builds response for command_check_build_index_status using bson, ok and finish.
 */
static Datum
ComposeCheckIndexStatusResponse(FunctionCallInfo fcinfo, pgbson *bson, bool ok, bool
								finish)
{
	Datum values[3] = { 0 };
	bool isNulls[3] = { false, false, false };
	values[0] = PointerGetDatum(bson);
	values[1] = BoolGetDatum(ok);
	values[2] = BoolGetDatum(finish);

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	return HeapTupleGetDatum(heap_form_tuple(resultTupDesc, values, isNulls));
}


/*
 * Wrapper around DropPostgresIndex which takes indexId as input and deletes the corresponding postgres Index.
 */
static void
TryDropCollectionIndex(int indexId)
{
	PG_TRY();
	{
		IndexDetails *indexDetails = IndexIdGetIndexDetails(indexId);
		if (indexDetails != NULL)
		{
			/* we might or might not have created the pg index .. */
			bool missingOk = true;
			bool concurrently = true;
			DropPostgresIndex(indexDetails->collectionId, indexId,
							  indexDetails->indexSpec.indexUnique,
							  concurrently, missingOk);
		}
	}
	PG_CATCH();
	{
		ereport(DEBUG1, (errmsg("couldn't perform clean-up for some of the "
								"invalid indexes left behind")));

		/*
		 * We don't much expect any error condition to happen here, but
		 * we still need to be defensive against any kind of failures, such
		 * as OOM.
		 *
		 * For this reason, here we swallow any errors that we could get
		 * during cleanup.
		 */
		FlushErrorState();

		/* need to abort the outer transaction to fire abort handler */
		PopAllActiveSnapshots();
		AbortCurrentTransaction();
		StartTransactionCommand();
	}
	PG_END_TRY();
}


/*
 * RunIndexCommandOnMetadataCoordinator runs the passed in query on coordinator only.
 */
static pgbson *
RunIndexCommandOnMetadataCoordinator(const char *query, int expectedSpiOk)
{
	if (IsMetadataCoordinator())
	{
		bool isNull = false;
		bool readOnly = false;
		Datum result = ExtensionExecuteQueryViaSPI(query, readOnly, expectedSpiOk,
												   &isNull);
		if (isNull)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"No response"),
							errdetail_log(
								"No response")));
		}

		return DatumGetPgBson(result);
	}
	else
	{
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(query);
		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Error submitting background index %s",
								text_to_cstring(result.response)),
							errdetail_log(
								"Error submitting index request %s",
								text_to_cstring(result.response))));
		}

		pgbson *responseBson;
		char *response = text_to_cstring(result.response);
		if (IsBsonHexadecimalString(response))
		{
			responseBson = PgbsonInitFromHexadecimalString(response);
		}
		else
		{
			/* It's a json string use json deserialization */
			responseBson = PgbsonInitFromJson(response);
		}

		return responseBson;
	}
}
