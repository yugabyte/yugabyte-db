/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/insert.c
 *
 * Implementation of the insert operation.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <funcapi.h>
#include <nodes/makefuncs.h>
#include <utils/timestamp.h>
#include <utils/portal.h>
#include <tcop/dest.h>
#include <tcop/pquery.h>
#include <tcop/tcopprot.h>
#include <commands/portalcmds.h>
#include <utils/snapmgr.h>
#include <catalog/pg_class.h>
#include <parser/parse_relation.h>
#include <utils/lsyscache.h>

#include "access/xact.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"

#include "io/helio_bson_core.h"
#include "commands/commands_common.h"
#include "commands/insert.h"
#include "commands/parse_error.h"
#include "metadata/collection.h"
#include "infrastructure/helio_plan_cache.h"
#include "sharding/sharding.h"
#include "commands/retryable_writes.h"
#include "io/pgbsonsequence.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "metadata/metadata_cache.h"
#include "utils/version_utils.h"
#include "utils/helio_errors.h"
#include "api_hooks.h"
#include "schema_validation/schema_validation.h"
#include "operators/bson_expr_eval.h"
#include "utils/mongo_errors.h"

/*
 * BatchInsertionSpec describes a batch of insert operations.
 */
typedef struct BatchInsertionSpec
{
	/* collection in which to perform insertions */
	char *collectionName;

	/* list of documents to insert */
	List *documents;

	/* if ordered, stop after the first failure */
	bool isOrdered;

	/* The shard OID if available */
	Oid insertShardOid;

	/* if true, bypass document validation */
	bool bypassDocumentValidation;
} BatchInsertionSpec;

/*
 * BatchInsertionResult contains the results that are sent to the
 * client after a insert command.
 */
typedef struct BatchInsertionResult
{
	/* response status (seems to always be 1?) */
	double ok;

	/* number of rows insert */
	uint64 rowsInserted;

	/* list of write errors for each insertion, or NIL */
	List *writeErrors;
} BatchInsertionResult;

PG_FUNCTION_INFO_V1(command_insert);
PG_FUNCTION_INFO_V1(command_insert_one);
PG_FUNCTION_INFO_V1(command_insert_worker);


static BatchInsertionSpec * BuildBatchInsertionSpec(bson_iter_t *insertCommandIter,
													pgbsonsequence *insertDocs);
static List * BuildInsertionList(bson_iter_t *insertArrayIter, bool *hasSkippedDocuments);
static List * BuildInsertionListFromPgbsonSequence(pgbsonsequence *docSequence,
												   bool *hasSkippedDocuments);
static void ProcessBatchInsertion(MongoCollection *collection,
								  BatchInsertionSpec *batchSpec,
								  text *transactionId, BatchInsertionResult *batchResult);
static void DoBatchInsertNoTransactionId(MongoCollection *collection,
										 BatchInsertionSpec *batchSpec,
										 BatchInsertionResult *batchResult,
										 ExprEvalState *evalState);

static uint64 ProcessInsertion(MongoCollection *collection, Oid insertShardOid, const
							   bson_value_t *document,
							   text *transactionId, ExprEvalState *evalState);
static pgbson * BuildResponseMessage(BatchInsertionResult *batchResult);
static uint64_t RunInsertQuery(Query *insertQuery, ParamListInfo paramListInfo);
static Query * CreateInsertQuery(MongoCollection *collection, Oid shardOid,
								 List *valuesLists);
static pgbson * PreprocessInsertionDoc(const bson_value_t *docValue,
									   MongoCollection *collection,
									   int64 *shardKeyHash, pgbson **objectId,
									   ExprEvalState *evalState);
static uint64 InsertOneWithTransactionCore(uint64 collectionId, const
										   char *shardTableName,
										   int64 shardKeyValue, text *transactionId,
										   pgbson *objectId, pgbson *document);
static uint64 CallInsertWorkerForInsertOne(MongoCollection *collection, int64
										   shardKeyHash,
										   pgbson *document, text *transactionId);

/*
 * helio_api.enable_create_collection_on_insert GUC determines whether
 * an insert into a non-existent collection should create a collection.
 */
bool EnableCreateCollectionOnInsert = true;
extern bool UseLocalExecutionShardQueries;
extern bool EnableBypassDocumentValidation;
extern bool EnableSchemaValidation;

/*
 * command_insert implements the insert command.
 */
Datum
command_insert(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("database name cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("insert document cannot be NULL")));
	}

	Datum databaseNameDatum = PG_GETARG_DATUM(0);
	pgbson *insertSpec = PG_GETARG_PGBSON(1);

	pgbsonsequence *insertDocs = PG_GETARG_MAYBE_NULL_PGBSON_SEQUENCE(2);

	text *transactionId = NULL;
	if (!PG_ARGISNULL(3))
	{
		transactionId = PG_GETARG_TEXT_P(3);
	}

	ReportFeatureUsage(FEATURE_COMMAND_INSERT);

	/* fetch TupleDesc for return value, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc;
	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	bson_iter_t insertCommandIter;
	PgbsonInitIterator(insertSpec, &insertCommandIter);

	/* we first validate insert command BSON and build a specification */
	BatchInsertionSpec *batchSpec = BuildBatchInsertionSpec(&insertCommandIter,
															insertDocs);

	BatchInsertionResult batchResult;

	if (list_length(batchSpec->documents) == 0)
	{
		/* If there's no documents to insert exit and don't create the collection */
		batchResult.rowsInserted = 0;
		batchResult.ok = 1;
		batchResult.writeErrors = NIL;
	}
	else
	{
		/* open the collection */
		Datum collectionNameDatum = CStringGetTextDatum(batchSpec->collectionName);
		MongoCollection *collection =
			GetMongoCollectionByNameDatum(databaseNameDatum, collectionNameDatum,
										  RowExclusiveLock);

		if (collection == NULL)
		{
			collection = CreateCollectionForInsert(databaseNameDatum,
												   collectionNameDatum);
		}
		else
		{
			batchSpec->insertShardOid = TryGetCollectionShardTable(collection,
																   RowExclusiveLock);
		}

		/* do the inserts */
		ProcessBatchInsertion(collection, batchSpec, transactionId, &batchResult);
	}

	Datum values[2];
	bool isNulls[2] = { false, false };

	values[0] = PointerGetDatum(BuildResponseMessage(&batchResult));
	values[1] = BoolGetDatum(batchResult.writeErrors == NIL);
	HeapTuple resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
	PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
}


/*
 * CreateCollectionForInsert creates a new collection and takes the approriate
 * lock for insert, or errors if we disabled automatic creation creation.
 */
MongoCollection *
CreateCollectionForInsert(Datum databaseNameDatum, Datum collectionNameDatum)
{
	/*
	 * If the collection does not exist, the client might prefer to handle that.
	 *
	 * This is primarily relevant when routing inserts via worker nodes. As long
	 * as Citus does not support create_distributed_table via worker nodes, we
	 * need to fall back to doing the insert via the coordinator when we get
	 * this error.
	 */
	if (!EnableCreateCollectionOnInsert)
	{
		char *collectionName = TextDatumGetCString(collectionNameDatum);

		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_TABLE),
						errmsg("collection %s does not exist",
							   quote_literal_cstr(collectionName))));
	}

	/*
	 * Call ApiSchemaName.create_collection. It internally handles concurrent
	 * calls in an idempotent manner, which means that if a concurrent insert raced
	 * to create the collection before us, this will be a noop.
	 */
	CreateCollection(databaseNameDatum, collectionNameDatum);

	MongoCollection *collection = GetMongoCollectionByNameDatum(databaseNameDatum,
																collectionNameDatum,
																RowExclusiveLock);

	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("failed to create collection"),
						errdetail_log(
							"Could not get collection from cache after creating the collection")));
	}

	return collection;
}


/*
 * BuildBatchInsertionSpec validates the insert command BSON and builds
 * a BatchInsertionSpec.
 */
static BatchInsertionSpec *
BuildBatchInsertionSpec(bson_iter_t *insertCommandIter, pgbsonsequence *insertDocs)
{
	const char *collectionName = NULL;
	List *documents = NIL;
	bool isOrdered = true;
	bool hasDocuments = false;
	bool hasSkippedDocuments = false;
	bool bypassDocumentValidation = false;

	while (bson_iter_next(insertCommandIter))
	{
		const char *field = bson_iter_key(insertCommandIter);

		if (strcmp(field, "insert") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(insertCommandIter))
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg("collection name has invalid type %s",
									   BsonIterTypeName(insertCommandIter))));
			}

			collectionName = bson_iter_utf8(insertCommandIter, NULL);
		}
		else if (strcmp(field, "documents") == 0)
		{
			EnsureTopLevelFieldType("insert.documents", insertCommandIter,
									BSON_TYPE_ARRAY);

			/* if both docs and spec are provided, fail */
			if (insertDocs != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
								errmsg("Unexpected additional documents")));
			}

			bson_iter_t insertArrayIter;
			bson_iter_recurse(insertCommandIter, &insertArrayIter);

			documents = BuildInsertionList(&insertArrayIter, &hasSkippedDocuments);
			hasDocuments = true;
		}
		else if (strcmp(field, "ordered") == 0)
		{
			EnsureTopLevelFieldType("insert.ordered", insertCommandIter, BSON_TYPE_BOOL);

			isOrdered = bson_iter_bool(insertCommandIter);
		}
		else if (strcmp(field, "bypassDocumentValidation") == 0)
		{
			/* TODO: unsupport by default */
			if (!EnableBypassDocumentValidation)
			{
				continue;
			}

			EnsureTopLevelFieldType("insert.bypassDocumentValidation", insertCommandIter,
									BSON_TYPE_BOOL);

			bypassDocumentValidation = bson_iter_bool(insertCommandIter);
		}
		else if (IsCommonSpecIgnoredField(field))
		{
			elog(DEBUG1, "Unrecognized command field: insert.%s", field);

			/*
			 *  Silently ignore now, so that clients don't break
			 *  TODO: implement me
			 *      writeConcern
			 *      comment
			 */
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							errmsg("BSON field 'insert.%s' is an unknown field",
								   field)));
		}
	}

	if (collectionName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("BSON field 'insert.insert' is missing but "
							   "a required field")));
	}

	if (insertDocs != NULL)
	{
		documents = BuildInsertionListFromPgbsonSequence(insertDocs,
														 &hasSkippedDocuments);
		hasDocuments = true;
	}

	if (!hasDocuments)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("BSON field 'insert.documents' is missing but "
							   "a required field")));
	}

	int insertionCount = list_length(documents);
	if ((!hasSkippedDocuments && insertionCount == 0) ||
		insertionCount > MaxWriteBatchSize)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("Write batch sizes must be between 1 and %d. "
							   "Got %d operations.", MaxWriteBatchSize, insertionCount)));
	}

	BatchInsertionSpec *batchSpec = palloc0(sizeof(BatchInsertionSpec));

	batchSpec->collectionName = (char *) collectionName;
	batchSpec->documents = documents;
	batchSpec->isOrdered = isOrdered;
	batchSpec->bypassDocumentValidation = bypassDocumentValidation;

	return batchSpec;
}


/*
 * Validates a given document value in the insert spec and checks
 * that it can be inserted. This currently does size validation,
 * and whether or not it's a valid document to be inserted.
 * Returns true if the document should be inserted.
 */
inline static bool
ValidateAndCheckShouldInsertDocument(const bson_value_t *docValue)
{
	pgbsonelement docElement;
	if (TryGetBsonValueToPgbsonElement(docValue, &docElement) &&
		strcmp(docElement.path, "_id") == 0 &&
		docElement.bsonValue.value_type == BSON_TYPE_UNDEFINED)
	{
		/* Skip documents that are { "_id": undefined } */
		return false;
	}

	/* Validate size of incoming docs in insert */
	uint32_t size = docValue->value.v_doc.data_len;
	if (size > BSON_MAX_ALLOWED_SIZE)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("Size %u is larger than MaxDocumentSize %u",
							   size, BSON_MAX_ALLOWED_SIZE)));
	}

	return true;
}


/*
 * BuildInsertionList iterates over an array of documents in an insert spec and
 * returns a list of pgbson documents to insert.
 */
static List *
BuildInsertionList(bson_iter_t *insertArrayIter, bool *hasSkippedDocuments)
{
	List *documents = NIL;
	*hasSkippedDocuments = false;

	while (bson_iter_next(insertArrayIter))
	{
		StringInfo fieldNameStr = makeStringInfo();
		int arrIdx = list_length(documents);
		appendStringInfo(fieldNameStr, "insert.documents.%d", arrIdx);

		EnsureTopLevelFieldType(fieldNameStr->data, insertArrayIter, BSON_TYPE_DOCUMENT);
		const bson_value_t *docValue = bson_iter_value(insertArrayIter);
		if (ValidateAndCheckShouldInsertDocument(docValue))
		{
			bson_value_t *clonedValue = palloc(sizeof(bson_value_t));
			*clonedValue = *docValue;
			documents = lappend(documents, clonedValue);
		}
		else
		{
			*hasSkippedDocuments = true;
		}
	}

	return documents;
}


/*
 * BuildInsertionListFromPgbsonSequence iterates over an
 * array of documents specified in the given pgbsonsequence
 * and returns a list of pgbson documents to insert.
 */
static List *
BuildInsertionListFromPgbsonSequence(pgbsonsequence *docSequence,
									 bool *hasSkippedDocuments)
{
	*hasSkippedDocuments = false;

	List *sequenceValues = PgbsonSequenceGetDocumentBsonValues(docSequence);

	ListCell *cell;
	foreach(cell, sequenceValues)
	{
		bson_value_t *docValue = lfirst(cell);
		if (!ValidateAndCheckShouldInsertDocument(docValue))
		{
			*hasSkippedDocuments = true;
			break;
		}
	}

	/* If there were no skipped docs, no need to allocate a new
	 * array, just return the original.
	 */
	if (!*hasSkippedDocuments)
	{
		return sequenceValues;
	}

	List *documents = NIL;
	foreach(cell, sequenceValues)
	{
		bson_value_t *docValue = lfirst(cell);
		if (ValidateAndCheckShouldInsertDocument(docValue))
		{
			documents = lappend(documents, docValue);
		}
	}

	return documents;
}


/*
 * Creates the Param values for the BSON types.
 * We do this as a BYTEA param so that Citus can
 * force the bson as a binary to the worker nodes
 * which can improve perf in multi-node scenarios.
 */
inline static Expr *
CreateBsonParam(int paramIndex, ParamListInfo paramListInfo, pgbson *bsonValue)
{
	Assert(paramListInfo->numParams > paramIndex);

	Param *bsonValueParam = makeNode(Param);
	bsonValueParam->paramid = paramIndex + 1;
	bsonValueParam->paramkind = PARAM_EXTERN;
	bsonValueParam->paramtype = BYTEAOID;
	bsonValueParam->paramtypmod = -1;
	paramListInfo->params[paramIndex].isnull = false;
	paramListInfo->params[paramIndex].pflags = PARAM_FLAG_CONST;
	paramListInfo->params[paramIndex].ptype = BYTEAOID;
	paramListInfo->params[paramIndex].value = PointerGetDatum(bsonValue);
	paramIndex++;

	return (Expr *) makeRelabelType((Expr *) bsonValueParam, BsonTypeId(), -1, InvalidOid,
									COERCE_IMPLICIT_CAST);
}


/*
 * Applies a set of inserts in a single transaction.
 * This applies without the case of retriable writes. In this case we just
 * directly call INSERT from the coordinator on a batch of documents.
 * This is an optimistic batch. On failures, we simply bail and go back to
 * A single document insert. This is because Mongo requires that we return
 * the failures associated with each insert back to the client while inserting
 * everything before it. Postgres rollsback the entire sub-transaction.
 * TODO: While we can optimize this by treating the sub-batch that succeeded first
 * and moving forward, this is left as an optimization for the future.
 */
static bool
DoMultiInsertWithoutTransactionId(MongoCollection *collection, List *inserts, Oid
								  shardOid,
								  BatchInsertionResult *batchResult, int insertIndex,
								  int *insertCountResult, ExprEvalState *evalState)
{
	/* declared volatile because of the longjmp in PG_CATCH */
	volatile int insertInnerIndex = insertIndex;
	volatile int insertCount = 0;

	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		List *valuesList = NIL;

		ListCell *insertCell;

		TimestampTz nowValueTime = GetCurrentTimestamp();
		Const *nowValue = makeConst(TIMESTAMPTZOID, -1, InvalidOid, 8,
									TimestampTzGetDatum(nowValueTime), false, true);

		/* Make params for all the BSONs - we have 2 per insert - objectId/insertDoc */
		int expectedNumParams = Min(list_length(inserts), BatchWriteSubTransactionCount);
		ParamListInfo paramListInfo = makeParamList(expectedNumParams * 2);
		int paramIndex = 0;
		while (insertInnerIndex < list_length(inserts) &&
			   insertCount < BatchWriteSubTransactionCount)
		{
			insertCell = list_nth_cell(inserts, insertInnerIndex);
			const bson_value_t *documentValue = lfirst(insertCell);

			int64_t shardKeyValue;
			pgbson *objectId;
			pgbson *insertDoc =
				PreprocessInsertionDoc(documentValue, collection, &shardKeyValue,
									   &objectId, evalState);

			/* Generate a values lists for the insert as
			 * VALUES(shard_key_value, object_id, document, creationTime)
			 */
			Const *shardKeyConst = makeConst(INT8OID, -1, InvalidOid, 8,
											 Int64GetDatum(shardKeyValue), false, true);
			Expr *objectidParam = CreateBsonParam(paramIndex, paramListInfo, objectId);
			paramIndex++;

			Expr *documentParam = CreateBsonParam(paramIndex, paramListInfo, insertDoc);
			paramIndex++;
			List *values = list_make4(shardKeyConst, objectidParam, documentParam,
									  nowValue);

			valuesList = lappend(valuesList, values);
			insertCount++;
			insertInnerIndex++;
		}

		paramListInfo->numParams = paramIndex;
		Query *query = CreateInsertQuery(collection, shardOid, valuesList);
		uint64_t rowsProcessed = RunInsertQuery(query, paramListInfo);

		/* Merge inner batchResult with outer batchResult */
		batchResult->rowsInserted += rowsProcessed;
		*insertCountResult = rowsProcessed;
		insertCount = rowsProcessed;
		list_free_deep(valuesList);
		pfree(paramListInfo);

		/* Commit the inner transaction, return to outer xact context */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
		insertCount = 0;

		int errorCode = errorData->sqlerrcode;
		const char *errorCodeStr = unpack_sql_state(errorCode);
		if (EreportCodeIsMongoError(errorCode))
		{
			errorCode = errorCode - ERRCODE_HELIO_INTERNALERROR;
			ereport(LOG, (
						errmsg(
							"Optimistic Batch Insert failed. Retrying with single insert. mongoErrorCode %d - sqlstate %s",
							errorCode, errorCodeStr)));
		}
		else
		{
			ereport(LOG, (
						errmsg(
							"Optimistic Batch Insert failed. Retrying with single insert. SQL Error %s",
							errorCodeStr),
						errdetail_log(
							"Optimistic Batch Insert failed. Retrying with single insert. SQL Error %s",
							errorCodeStr)));
		}
	}
	PG_END_TRY();

	return insertCount != 0;
}


/*
 * Applies a single insert in a single sub-transaction.
 */
static bool
DoSingleInsert(MongoCollection *collection,
			   Oid insertShardOid,
			   const bson_value_t *document,
			   text *transactionId,
			   BatchInsertionResult *batchResult, int insertIndex,
			   ExprEvalState *evalState)
{
	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool isSuccess = false;
	volatile uint64 numDocsInserted = 0;

	/* use a subtransaction to correctly handle failures */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;
	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		numDocsInserted = ProcessInsertion(collection, insertShardOid, document,
										   transactionId, evalState);

		/* Commit the inner transaction, return to outer xact context */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
		batchResult->rowsInserted += numDocsInserted;
		isSuccess = true;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();

		/* Rollback changes MemoryContext */
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		batchResult->writeErrors = lappend(batchResult->writeErrors,
										   GetWriteErrorFromErrorData(errorData,
																	  insertIndex));
		FreeErrorData(errorData);
		isSuccess = false;
	}
	PG_END_TRY();

	return isSuccess;
}


/*
 * ProcessBatchInsertion iterates over the inserts array and executes each
 * insertion in a subtransaction, to allow us to continue after an error.
 *
 * If batchSpec->isOrdered is false, we continue with remaining tasks and
 * track the error for the response.
 *
 * Using subtransactions is slightly different from Mongo, which effectively
 * does each insert operation in a separate transaction, but it has roughly
 * the same overall UX.
 */
static void
ProcessBatchInsertion(MongoCollection *collection, BatchInsertionSpec *batchSpec,
					  text *transactionId, BatchInsertionResult *batchResult)
{
	batchResult->ok = 1;
	batchResult->rowsInserted = 0;
	batchResult->writeErrors = NIL;

	ExprEvalState *evalState = NULL;

	/* In MongoDB, document validation occurs regardless of whether the validation action is set to error or warn.
	 * If validation fails and the action is error, an error is thrown; if the action is warn, a warning is logged.
	 * Since we do not need to log a warning in this context, we will avoid calling ValidateSchemaOnDocumentInsert when the validation action is set to warn.
	 */
	if (EnableSchemaValidation && !batchSpec->bypassDocumentValidation &&
		collection->schemaValidator.validator != NULL &&
		collection->schemaValidator.validationAction == ValidationAction_Error)
	{
		evalState = PrepareForSchemaValidation(collection->schemaValidator.validator,
											   CurrentMemoryContext);
	}

	/*
	 * We cannot pass the same transactionId to ProcessUpdate when there are
	 * multiple updates, since they would be considered retries of each
	 * other. We pass NULL for now to disable retryable writes.
	 */
	if (transactionId != NULL && list_length(batchSpec->documents) == 1)
	{
		/* So at this point, we have a single document and transactionId != NULL */
		int insertIndex = 0;
		DoSingleInsert(collection, batchSpec->insertShardOid, linitial(
						   batchSpec->documents),
					   transactionId, batchResult, insertIndex, evalState);
	}
	else
	{
		/* The else scenario - we have no transactionId (or we ignore it)
		 * and/or we have more than 1 document. Do a batch insert directly.
		 */
		DoBatchInsertNoTransactionId(collection, batchSpec, batchResult, evalState);
	}

	if (evalState != NULL)
	{
		FreeExprEvalState(evalState, CurrentMemoryContext);
	}
}


/*
 * Process an insertion for batch of inserts using the INSERT command.
 */
static void
DoBatchInsertNoTransactionId(MongoCollection *collection, BatchInsertionSpec *batchSpec,
							 BatchInsertionResult *batchResult, ExprEvalState *evalState)
{
	List *insertions = batchSpec->documents;
	bool isOrdered = batchSpec->isOrdered;

	int insertIndex = 0;
	bool hasBatchedInsertFailed = false;

	ListCell *insertCell = NULL;
	while (insertIndex < list_length(insertions))
	{
		CHECK_FOR_INTERRUPTS();

		if (list_length(insertions) > 1 && !hasBatchedInsertFailed)
		{
			/* Optimistically try to do multiple updates together, if it fails, try again one by one to figure out which one failed */
			int incrementCount = 0;
			bool performedBatchInsert = DoMultiInsertWithoutTransactionId(collection,
																		  insertions,
																		  batchSpec->
																		  insertShardOid,
																		  batchResult,
																		  insertIndex,
																		  &incrementCount,
																		  evalState);

			Assert(!performedBatchInsert || incrementCount > 0);
			if (!performedBatchInsert)
			{
				/* Has a failure, set hasFailures and retry */
				hasBatchedInsertFailed = true;
			}

			insertIndex += incrementCount;
			continue;
		}

		insertCell = list_nth_cell(insertions, insertIndex);
		const bson_value_t *document = lfirst(insertCell);

		text *transactionId = NULL;
		bool isSuccess = DoSingleInsert(collection, batchSpec->insertShardOid, document,
										transactionId, batchResult,
										insertIndex, evalState);
		insertIndex++;

		if (!isSuccess && isOrdered)
		{
			/* stop trying insert operations after a failure if using ordered:true */
			break;
		}
	}
}


/*
 * ProcessInsertion processes a single insertion operation.
 */
static uint64
ProcessInsertion(MongoCollection *collection,
				 Oid optionalInsertShardOid,
				 const bson_value_t *documentValue,
				 text *transactionId, ExprEvalState *evalState)
{
	if (transactionId != NULL &&
		!DocumentBsonValueHasDocumentId(documentValue) &&
		collection->shardKey != NULL &&
		PgbsonHasDocumentId(collection->shardKey))
	{
		RetryableWriteResult writeResult;

		/*
		 * This edge case is slightly problematic: We have a collection that is
		 * sharded by _id, but the document does not specify an _id so we will
		 * generate one randomly. If this is the second try, we do not know which
		 * shard holds the retry record, so we search all of them.
		 *
		 * Clients can prevent this by setting the object ID.
		 */
		if (FindRetryRecordInAnyShard(collection->collectionId, transactionId,
									  &writeResult))
		{
			return writeResult.rowsAffected;
		}
	}

	int64 shardKeyHash;
	pgbson *objectIdPtr = NULL;
	pgbson *insertDoc = PreprocessInsertionDoc(documentValue, collection, &shardKeyHash,
											   &objectIdPtr, evalState);

	/* make sure the document has an _id and it is in the right place */
	if (transactionId == NULL)
	{
		/*
		 * If retry is NULL then we don't really need to call insert_one on the worker, and then
		 * have that call INSERT - we can just do that directly from the coordinator (which probably
		 * saves one query parsing and planning per document).
		 */
		ParamListInfo paramListInfo = makeParamList(2);
		paramListInfo->numParams = 2;
		Const *shardKeyConst = makeConst(INT8OID, -1, InvalidOid, 8,
										 Int64GetDatum(shardKeyHash), false, true);
		Expr *objectidParam = CreateBsonParam(0, paramListInfo, objectIdPtr);
		Expr *documentParam = CreateBsonParam(1, paramListInfo, insertDoc);
		TimestampTz nowValueTime = GetCurrentTimestamp();
		Const *nowValue = makeConst(TIMESTAMPTZOID, -1, InvalidOid, 8,
									TimestampTzGetDatum(nowValueTime), false, true);
		List *singleInsertList = list_make4(shardKeyConst, objectidParam, documentParam,
											nowValue);
		Query *query = CreateInsertQuery(collection, optionalInsertShardOid, list_make1(
											 singleInsertList));
		uint64_t insertResult = RunInsertQuery(query, paramListInfo);
		pfree(paramListInfo);
		list_free_deep(singleInsertList);
		return insertResult;
	}
	else
	{
		return CallInsertWorkerForInsertOne(collection, shardKeyHash, insertDoc,
											transactionId);
	}
}


static uint64
CallInsertWorkerForInsertOne(MongoCollection *collection, int64 shardKeyHash,
							 pgbson *document, text *transactionId)
{
	int argCount = 6;
	Datum argValues[6];

	/* whitespace means not null, n means null */
	char argNulls[6] = { ' ', ' ', ' ', ' ', 'n', ' ' };
	Oid argTypes[6] = { INT8OID, INT8OID, REGCLASSOID, BYTEAOID, BYTEAOID, TEXTOID };

	const char *updateQuery = FormatSqlQuery(
		" SELECT helio_api_internal.insert_worker($1, $2, $3, $4::helio_core.bson, $5::helio_core.bsonsequence, $6) FROM %s.documents_"
		UINT64_FORMAT " WHERE shard_key_value = %ld",
		ApiDataSchemaName, collection->collectionId, shardKeyHash);

	argValues[0] = UInt64GetDatum(collection->collectionId);

	/* p_shard_key_value */
	argValues[1] = Int64GetDatum(shardKeyHash);

	/* p_shard_oid */
	argValues[2] = ObjectIdGetDatum(InvalidOid);

	/* We just send the document for now */
	pgbsonelement element = { 0 };
	element.path = "insertOne";
	element.pathLength = 9;
	element.bsonValue = ConvertPgbsonToBsonValue(document);
	argValues[3] = PointerGetDatum(PgbsonElementToPgbson(&element));

	argValues[5] = PointerGetDatum(transactionId);
	argNulls[5] = ' ';

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
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("insert_worker should not return null")));
	}

	/* If we got here, then it succeeded and inserted */
	return 1;
}


bool
TryInsertOne(MongoCollection *collection, pgbson *document, int64 shardKeyHash, bool
			 sameSourceAndTarget, WriteError *writeError)
{
	volatile bool rowsInserted = false;
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		if (sameSourceAndTarget)
		{
			rowsInserted = InsertDocumentToTempCollection(collection,
														  shardKeyHash, document);
		}
		else
		{
			rowsInserted = InsertDocument(collection->collectionId,
										  collection->shardTableName,
										  shardKeyHash, PgbsonGetDocumentId(document),
										  document);
		}
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);

		ErrorData *errorData = CopyErrorDataAndFlush();
		if (writeError != NULL)
		{
			writeError->code = errorData->sqlerrcode;
			writeError->errmsg = errorData->message;
		}

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		rowsInserted = false;
	}
	PG_END_TRY();

	return rowsInserted;
}


/*
 * command_insert_one is the internal implementation of the db.collection.insertOne() API.
 */
Datum
command_insert_one(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errmsg("insert_one is deprecated and should not be called")));
}


static uint64
InsertOneWithTransactionCore(uint64 collectionId, const char *shardTableName,
							 int64 shardKeyValue, text *transactionId,
							 pgbson *objectId, pgbson *document)
{
	RetryableWriteResult writeResult;

	/*
	 * If a retry record exists, delete it since only a single retry is allowed.
	 */
	if (DeleteRetryRecord(collectionId, shardKeyValue, transactionId, &writeResult))
	{ }
	else
	{
		/* no retry record exists, insert the document */
		InsertDocument(collectionId, shardTableName, shardKeyValue, objectId, document);

		/* we always insert 1 row */
		bool rowsAffected = true;

		/* remember that transaction performed the insert */
		InsertRetryRecord(collectionId, shardKeyValue, transactionId, objectId,
						  rowsAffected, NULL);
	}

	return 1;
}


Datum
command_insert_worker(PG_FUNCTION_ARGS)
{
	uint64 collectionId = PG_GETARG_INT64(0);
	int64 shardKeyValue = PG_GETARG_INT64(1);
	Oid shardOid = PG_GETARG_OID(2);

	pgbson *insertInternalSpec = PG_GETARG_PGBSON_PACKED(3);
	text *transactionId = PG_GETARG_TEXT_P(5);

	if (shardOid == InvalidOid)
	{
		/* The planner is expected to replace this */
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("Explicit shardOid must be set - this is a server bug"),
						errdetail_log(
							"Explicit shardOid must be set - this is a server bug")));
	}

	pgbsonelement element = { 0 };
	PgbsonToSinglePgbsonElement(insertInternalSpec, &element);

	if (strcmp(element.path, "insertOne") != 0 ||
		element.bsonValue.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"Only insertOne with a single document on the worker is supported currently")));
	}

	const char *localShardTable = NULL;
	if (UseLocalExecutionShardQueries)
	{
		localShardTable = get_rel_name(shardOid);
	}

	pgbson *document = PgbsonInitFromDocumentBsonValue(&element.bsonValue);
	pgbson *objectId = PgbsonGetDocumentId(document);
	InsertOneWithTransactionCore(collectionId, localShardTable, shardKeyValue,
								 transactionId, objectId,
								 document);
	PG_RETURN_POINTER(PgbsonInitEmpty());
}


/*
 * InsertDocument inserts a document into a collection.
 */
bool
InsertDocument(uint64 collectionId, const char *shardTableName,
			   int64 shardKeyValue, pgbson *objectId,
			   pgbson *document)
{
	StringInfoData query;
	const int argCount = 3;
	Oid argTypes[3];
	Datum argValues[3];
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query, "INSERT INTO %s.", ApiDataSchemaName);

	if (shardTableName != NULL && shardTableName[0] != '\0')
	{
		appendStringInfoString(&query, shardTableName);
	}
	else
	{
		appendStringInfo(&query, "documents_" UINT64_FORMAT, collectionId);
	}

	appendStringInfo(&query, " (shard_key_value, object_id, document) "
							 " VALUES ($1, %s.bson_from_bytea($2), "
							 "%s.bson_from_bytea($3))",
					 CoreSchemaName, CoreSchemaName);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyValue);
	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(objectId));
	argTypes[2] = BYTEAOID;
	argValues[2] = PointerGetDatum(CastPgbsonToBytea(document));

	SPIPlanPtr plan = GetSPIQueryPlanWithLocalShard(collectionId, shardTableName,
													QUERY_ID_INSERT, query.data, argTypes,
													argCount);

	spiStatus = SPI_execute_plan(plan, argValues, NULL, false, 1);
	pfree(query.data);

	SPI_finish();

	return spiStatus == SPI_OK_INSERT;
}


bool
InsertDocumentToTempCollection(MongoCollection *collection, int64 shardKeyValue,
							   pgbson *document)
{
	const int argCount = 3;
	Oid argTypes[3];
	Datum argValues[3];
	pgbson *objectId = PgbsonGetDocumentId(document);
	StringInfoData query;
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;

	SPI_connect();
	initStringInfo(&query);
	appendStringInfo(&query,
					 "INSERT INTO \"%s\""
					 " (shard_key_value, object_id, document) "
					 " VALUES ($1, %s.bson_from_bytea($2), "
					 "%s.bson_from_bytea($3))", collection->tableName,
					 CoreSchemaName, CoreSchemaName);
	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyValue);
	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(objectId));
	argTypes[2] = BYTEAOID;
	argValues[2] = PointerGetDatum(CastPgbsonToBytea(document));

	SPIPlanPtr plan = GetSPIQueryPlan(collection->collectionId, QUERY_ID_INSERT,
									  query.data, argTypes, argCount);

	spiStatus = SPI_execute_plan(plan, argValues, NULL, false, 1);
	pfree(query.data);

	SPI_finish();

	return spiStatus == SPI_OK_INSERT;
}


/*
 * BuildResponseMessage builds the response BSON for an insert command.
 */
static pgbson *
BuildResponseMessage(BatchInsertionResult *batchResult)
{
	pgbson_writer resultWriter;
	PgbsonWriterInit(&resultWriter);
	PgbsonWriterAppendInt32(&resultWriter, "n", 1, batchResult->rowsInserted);
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


/*
 * Given a bson_value for an insert that was in the insert.documents or bson sequence
 * ensures it's of a proper form, validates the document, and extracts required
 * fields from it (shard_key, object_id) and creates a pgbson for insertion.
 */
static pgbson *
PreprocessInsertionDoc(const bson_value_t *docValue, MongoCollection *collection,
					   int64 *shardKeyHash, pgbson **objectId, ExprEvalState *evalState)
{
	if (evalState != NULL)
	{
		ValidateSchemaOnDocumentInsert(
			evalState, docValue);
	}

	/* make sure the document has an _id and it is in the right place */
	pgbson *insertDoc = RewriteDocumentValueAddObjectId(docValue);

	PgbsonValidateInputBson(insertDoc, BSON_VALIDATE_NONE);

	/*
	 * It's possible that the document does not specify the full shard key.
	 * In that case we will only hash the parts that are specified. That
	 * is not problematic in terms of querying, because it means this
	 * object can only be found by queries that do not specify a full
	 * shard key filter and those queries scan all the shards.
	 */
	*shardKeyHash = ComputeShardKeyHashForDocument(collection->shardKey,
												   collection->collectionId,
												   insertDoc);

	if (objectId != NULL)
	{
		*objectId = PgbsonGetDocumentId(insertDoc);
	}

	return insertDoc;
}


/*
 * Creates a Query for the purposes of inserting a document. This takes
 * the form
 * INSERT INTO <collection> (shard_key_value, object_id, document, creation_time)
 * VALUES ( <list of values> )
 */
static Query *
CreateInsertQuery(MongoCollection *collection, Oid shardOid, List *valuesLists)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_INSERT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	/* Make the base table RTE */
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	List *colNames = list_make4(makeString("shard_key_value"), makeString("object_id"),
								makeString("document"),
								makeString("creation_time"));

	/* If "creation_time" is the fifth column, then we should include "change_description" in the RTE. */
	if (collection->mongoDataCreationTimeVarAttrNumber == 5)
	{
		colNames = ModifyTableColumnNames(colNames);
	}

	rte->rtekind = RTE_RELATION;
	rte->relid = collection->relationId;

	/* If there is a shardOid and we can thunk directly to the shard,
	 * then set it. This will point the insert to the shard directly and avoid
	 * going through citus distributed planning.
	 */
	if (shardOid != InvalidOid)
	{
		rte->relid = shardOid;
	}

	rte->alias = rte->eref = makeAlias("collection", colNames);
	rte->lateral = false;
	rte->inFromCl = false;
	rte->relkind = RELKIND_RELATION;
	rte->functions = NIL;
	rte->inh = true;
#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&query->rteperminfos, rte);
	permInfo->requiredPerms = ACL_INSERT;
#else
	rte->requiredPerms = ACL_INSERT;
#endif

	rte->rellockmode = RowExclusiveLock;
	query->rtable = lappend(query->rtable, rte);
	query->resultRelation = 1;

	/* Make the VALUES RTE */
	List *valuesColNames = list_make4(makeString("shard_key_value"),
									  makeString("object_id"),
									  makeString("document"),
									  makeString("creation_time"));
	RangeTblEntry *valuesRte = makeNode(RangeTblEntry);
	valuesRte->rtekind = RTE_VALUES;
	valuesRte->alias = valuesRte->eref = makeAlias("values", valuesColNames);
	valuesRte->lateral = false;
	valuesRte->inFromCl = false;
	valuesRte->values_lists = valuesLists;
	valuesRte->inh = false;
	valuesRte->inFromCl = true;

	valuesRte->coltypes = list_make4_oid(INT8OID, BsonTypeId(), BsonTypeId(),
										 TIMESTAMPTZOID);
	valuesRte->coltypmods = list_make4_int(-1, -1, -1, -1);
	valuesRte->colcollations = list_make4_oid(InvalidOid, InvalidOid, InvalidOid,
											  InvalidOid);
	query->rtable = lappend(query->rtable, valuesRte);

	RangeTblRef *valuesRteRef = makeNode(RangeTblRef);
	valuesRteRef->rtindex = 2;
	List *fromList = list_make1(valuesRteRef);

	query->jointree = makeFromExpr(fromList, NULL);

	/* Now create the targetlist */
	query->targetList = list_make4(
		makeTargetEntry((Expr *) makeVar(2, 1, INT8OID, -1, InvalidOid, 0),
						MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
						"shard_key_value", false),
		makeTargetEntry((Expr *) makeVar(2, 2, BsonTypeId(), -1, InvalidOid, 0),
						MONGO_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER, "object_id", false),
		makeTargetEntry((Expr *) makeVar(2, 3, BsonTypeId(), -1, InvalidOid, 0),
						MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER, "document", false),
		makeTargetEntry((Expr *) makeVar(2, 4, TIMESTAMPTZOID, -1, InvalidOid, 0),
						collection->mongoDataCreationTimeVarAttrNumber, "creation_time",
						false)
		);

	/* In order to use a portal & SPI we create a returning list of a const */
	query->returningList = list_make1(
		makeTargetEntry((Expr *) makeConst(INT4OID, -1, InvalidOid, 4, Int32GetDatum(1),
										   false, true), 1, "intVal", false)
		);
	return query;
}


/*
 * Executes the Insert query and returns the number of results processed.
 */
static uint64_t
RunInsertQuery(Query *insertQuery, ParamListInfo paramListInfo)
{
	uint64_t numRowsProcessed = 0;

	int cursorOptions = CURSOR_OPT_NO_SCROLL | CURSOR_OPT_BINARY;
	Portal queryPortal = CreateNewPortal();
	queryPortal->visible = false;
	queryPortal->cursorOptions = cursorOptions;

	PlannedStmt *queryPlan = pg_plan_query(insertQuery, NULL, cursorOptions,
										   paramListInfo);

	/* Set the plan in the cursor for this iteration */
	PortalDefineQuery(queryPortal, NULL, "",
					  CMDTAG_SELECT,
					  list_make1(queryPlan),
					  NULL);

	/* Trigger execution (Start the ExecEngine etc.) */
	PortalStart(queryPortal, paramListInfo, 0, GetActiveSnapshot());

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	/* run through everything but load no results. */
	SPI_cursor_move(queryPortal, true, FETCH_ALL);

	numRowsProcessed = SPI_processed;
	SPI_cursor_close(queryPortal);

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, (errmsg("could not complete SPI query")));
	}

	return numRowsProcessed;
}
